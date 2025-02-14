/**
 *    Copyright (C) 2018-present MongoDB, Inc.
 *
 *    This program is free software: you can redistribute it and/or modify
 *    it under the terms of the Server Side Public License, version 1,
 *    as published by MongoDB, Inc.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    Server Side Public License for more details.
 *
 *    You should have received a copy of the Server Side Public License
 *    along with this program. If not, see
 *    <http://www.mongerdb.com/licensing/server-side-public-license>.
 *
 *    As a special exception, the copyright holders give permission to link the
 *    code of portions of this program with the OpenSSL library under certain
 *    conditions as described in each individual source file and distribute
 *    linked combinations including the program with the OpenSSL library. You
 *    must comply with the Server Side Public License in all respects for
 *    all of the code used other than as permitted herein. If you modify file(s)
 *    with this exception, you may extend this exception to your version of the
 *    file(s), but you are not obligated to do so. If you do not wish to do so,
 *    delete this exception statement from your version. If you delete this
 *    exception statement from all source files in the program, then also delete
 *    it in the license file.
 */

#define MONGO_LOG_DEFAULT_COMPONENT ::monger::logger::LogComponent::kNetwork

#include "monger/platform/basic.h"

#include "monger/client/monger_uri.h"

#include <utility>

#include <boost/algorithm/string/case_conv.hpp>
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/find_iterator.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/range/algorithm/count.hpp>

#include "monger/base/status_with.h"
#include "monger/bson/bsonobjbuilder.h"
#include "monger/client/sasl_client_authenticate.h"
#include "monger/db/namespace_string.h"
#include "monger/stdx/utility.h"
#include "monger/util/dns_name.h"
#include "monger/util/dns_query.h"
#include "monger/util/hex.h"
#include "monger/util/str.h"

using namespace std::literals::string_literals;

namespace {
constexpr std::array<char, 16> hexits{
    '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'};

// This vector must remain sorted.  It is over pairs to facilitate a call to `std::includes` using
// a `std::map<std::string, std::string>` as the other parameter.
const std::vector<std::pair<std::string, std::string>> permittedTXTOptions = {{"authSource"s, ""s},
                                                                              {"replicaSet"s, ""s}};
}  // namespace

/**
 * RFC 3986 Section 2.1 - Percent Encoding
 *
 * Encode data elements in a way which will allow them to be embedded
 * into a mongerdb:// URI safely.
 */
void monger::uriEncode(std::ostream& ss, StringData toEncode, StringData passthrough) {
    for (const auto& c : toEncode) {
        if ((c == '-') || (c == '_') || (c == '.') || (c == '~') || isalnum(c) ||
            (passthrough.find(c) != std::string::npos)) {
            ss << c;
        } else {
            // Encoding anything not included in section 2.3 "Unreserved characters"
            ss << '%' << hexits[(c >> 4) & 0xF] << hexits[c & 0xF];
        }
    }
}

monger::StatusWith<std::string> monger::uriDecode(StringData toDecode) {
    StringBuilder out;
    for (size_t i = 0; i < toDecode.size(); ++i) {
        const char c = toDecode[i];
        if (c == '%') {
            if (i + 2 > toDecode.size()) {
                return Status(ErrorCodes::FailedToParse,
                              "Encountered partial escape sequence at end of string");
            }
            auto swHex = fromHex(toDecode.substr(i + 1, 2));
            if (swHex.isOK()) {
                out << swHex.getValue();
            } else {
                return Status(ErrorCodes::Error(51040),
                              "The characters after the % do not form a hex value. Please escape "
                              "the % or pass a valid hex value. ");
            }
            i += 2;
        } else {
            out << c;
        }
    }
    return out.str();
}

namespace monger {

namespace {

constexpr StringData kURIPrefix = "mongerdb://"_sd;
constexpr StringData kURISRVPrefix = "mongerdb+srv://"_sd;
constexpr StringData kDefaultMongerHost = "127.0.0.1:27017"_sd;

/**
 * Helper Method for MongerURI::parse() to split a string into exactly 2 pieces by a char
 * delimiter.
 */
std::pair<StringData, StringData> partitionForward(StringData str, const char c) {
    const auto delim = str.find(c);
    if (delim == std::string::npos) {
        return {str, StringData()};
    }
    return {str.substr(0, delim), str.substr(delim + 1)};
}

/**
 * Helper method for MongerURI::parse() to split a string into exactly 2 pieces by a char
 * delimiter searching backward from the end of the string.
 */
std::pair<StringData, StringData> partitionBackward(StringData str, const char c) {
    const auto delim = str.rfind(c);
    if (delim == std::string::npos) {
        return {StringData(), str};
    }
    return {str.substr(0, delim), str.substr(delim + 1)};
}

/**
 * Breakout method for parsing application/x-www-form-urlencoded option pairs
 *
 * foo=bar&baz=qux&...
 *
 * A `std::map<std::string, std::string>` is returned, to facilitate setwise operations from the STL
 * on multiple parsed option sources.  STL setwise operations require sorted lists.  A map is used
 * instead of a vector of pairs to permit insertion-is-not-overwrite behavior.
 */
MongerURI::OptionsMap parseOptions(StringData options, StringData url) {
    MongerURI::OptionsMap ret;
    if (options.empty()) {
        return ret;
    }

    if (options.find('?') != std::string::npos) {
        uasserted(
            ErrorCodes::FailedToParse,
            str::stream() << "URI Cannot Contain multiple questions marks for mongerdb:// URL: "
                          << url);
    }

    const auto optionsStr = options.toString();
    for (auto i =
             boost::make_split_iterator(optionsStr, boost::first_finder("&", boost::is_iequal()));
         i != std::remove_reference<decltype((i))>::type{};
         ++i) {
        const auto opt = boost::copy_range<std::string>(*i);
        if (opt.empty()) {
            uasserted(ErrorCodes::FailedToParse,
                      str::stream()
                          << "Missing a key/value pair in the options for mongerdb:// URL: "
                          << url);
        }

        const auto kvPair = partitionForward(opt, '=');
        const auto keyRaw = kvPair.first;
        if (keyRaw.empty()) {
            uasserted(ErrorCodes::FailedToParse,
                      str::stream()
                          << "Missing a key for key/value pair in the options for mongerdb:// URL: "
                          << url);
        }
        const auto key = uassertStatusOKWithContext(
            uriDecode(keyRaw),
            str::stream() << "Key '" << keyRaw
                          << "' in options cannot properly be URL decoded for mongerdb:// URL: "
                          << url);
        const auto valRaw = kvPair.second;
        if (valRaw.empty()) {
            uasserted(ErrorCodes::FailedToParse,
                      str::stream() << "Missing value for key '" << keyRaw
                                    << "' in the options for mongerdb:// URL: "
                                    << url);
        }
        const auto val = uassertStatusOKWithContext(
            uriDecode(valRaw),
            str::stream() << "Value '" << valRaw << "' for key '" << keyRaw
                          << "' in options cannot properly be URL decoded for mongerdb:// URL: "
                          << url);

        ret[key] = val;
    }

    return ret;
}

MongerURI::OptionsMap addTXTOptions(MongerURI::OptionsMap options,
                                   const std::string& host,
                                   const StringData url,
                                   const bool isSeedlist) {
    // If there is no seedlist mode, then don't add any TXT options.
    if (!isSeedlist)
        return options;
    options.insert({"ssl", "true"});

    // Get all TXT records and parse them as options, adding them to the options set.
    auto txtRecords = dns::getTXTRecords(host);
    if (txtRecords.empty()) {
        return {std::make_move_iterator(begin(options)), std::make_move_iterator(end(options))};
    }

    if (txtRecords.size() > 1) {
        uasserted(ErrorCodes::FailedToParse, "Encountered multiple TXT records for: "s + url);
    }

    auto txtOptions = parseOptions(txtRecords.front(), url);
    if (!std::includes(
            begin(permittedTXTOptions),
            end(permittedTXTOptions),
            begin(stdx::as_const(txtOptions)),
            end(stdx::as_const(txtOptions)),
            [](const auto& lhs, const auto& rhs) { return std::get<0>(lhs) < std::get<0>(rhs); })) {
        uasserted(ErrorCodes::FailedToParse, "Encountered invalid options in TXT record.");
    }

    options.insert(std::make_move_iterator(begin(txtOptions)),
                   std::make_move_iterator(end(txtOptions)));

    return {std::make_move_iterator(begin(options)), std::make_move_iterator(end(options))};
}

// Contains the parts of a MongerURI as unowned StringData's. Any code that needs to break up
// URIs into their basic components without fully parsing them can use this struct.
// Internally, MongerURI uses this to do basic parsing of the input URI string.
struct URIParts {
    explicit URIParts(StringData uri);
    StringData scheme;
    StringData username;
    StringData password;
    StringData hostIdentifiers;
    StringData database;
    StringData options;
};

URIParts::URIParts(StringData uri) {
    // 1. Strip off the scheme ("monger://")
    auto schemeEnd = uri.find("://");
    if (schemeEnd == std::string::npos) {
        uasserted(ErrorCodes::FailedToParse,
                  str::stream() << "URI must begin with " << kURIPrefix << " or " << kURISRVPrefix
                                << ": "
                                << uri);
    }
    const auto uriWithoutPrefix = uri.substr(schemeEnd + 3);
    scheme = uri.substr(0, schemeEnd);

    // 2. Split the string by the first, unescaped / (if any), yielding:
    // split[0]: User information and host identifers
    // split[1]: Auth database and connection options
    const auto userAndDb = partitionForward(uriWithoutPrefix, '/');
    const auto userAndHostInfo = userAndDb.first;

    // 2.b Make sure that there are no question marks in the left side of the /
    //     as any options after the ? must still have the / delimiter
    if (userAndDb.second.empty() && userAndHostInfo.find('?') != std::string::npos) {
        uasserted(
            ErrorCodes::FailedToParse,
            str::stream()
                << "URI must contain slash delimiter between hosts and options for mongerdb:// URL: "
                << uri);
    }

    // 3. Split the user information and host identifiers string by the last, unescaped @,
    const auto userAndHost = partitionBackward(userAndHostInfo, '@');
    const auto userInfo = userAndHost.first;
    hostIdentifiers = userAndHost.second;

    // 4. Split up the username and password
    const auto userAndPass = partitionForward(userInfo, ':');
    username = userAndPass.first;
    password = userAndPass.second;

    // 5. Split the database name from the list of options
    const auto databaseAndOptions = partitionForward(userAndDb.second, '?');
    database = databaseAndOptions.first;
    options = databaseAndOptions.second;
}
}  // namespace

MongerURI::CaseInsensitiveString::CaseInsensitiveString(std::string str)
    : _original(std::move(str)), _lowercase(boost::algorithm::to_lower_copy(_original)) {}

bool MongerURI::isMongerURI(StringData uri) {
    return (uri.startsWith(kURIPrefix) || uri.startsWith(kURISRVPrefix));
}

std::string MongerURI::redact(StringData url) {
    uassert(50892, "String passed to MongerURI::redact wasn't a MongerURI", isMongerURI(url));
    URIParts parts(url);
    std::ostringstream out;

    out << parts.scheme << "://";
    if (!parts.username.empty()) {
        out << parts.username << "@";
    }
    out << parts.hostIdentifiers;
    if (!parts.database.empty()) {
        out << "/" << parts.database;
    }

    return out.str();
}

MongerURI MongerURI::parseImpl(const std::string& url) {
    const StringData urlSD(url);

    // 1. Validate and remove the scheme prefix `mongerdb://` or `mongerdb+srv://`
    const bool isSeedlist = urlSD.startsWith(kURISRVPrefix);
    if (!(urlSD.startsWith(kURIPrefix) || isSeedlist)) {
        return MongerURI(uassertStatusOK(ConnectionString::parse(url)));
    }

    // 2. Split up the URI into its components for further parsing and validation
    URIParts parts(url);
    const auto hostIdentifiers = parts.hostIdentifiers;
    const auto usernameSD = parts.username;
    const auto passwordSD = parts.password;
    const auto databaseSD = parts.database;
    const auto connectionOptions = parts.options;

    // 3. URI decode and validate the username/password
    const auto containsColonOrAt = [](StringData str) {
        return (str.find(':') != std::string::npos) || (str.find('@') != std::string::npos);
    };

    if (containsColonOrAt(usernameSD)) {
        uasserted(ErrorCodes::FailedToParse,
                  str::stream() << "Username must be URL Encoded for mongerdb:// URL: " << url);
    }

    if (containsColonOrAt(passwordSD)) {
        uasserted(ErrorCodes::FailedToParse,
                  str::stream() << "Password must be URL Encoded for mongerdb:// URL: " << url);
    }

    // Get the username and make sure it did not fail to decode
    const auto username = uassertStatusOKWithContext(
        uriDecode(usernameSD),
        str::stream() << "Username cannot properly be URL decoded for mongerdb:// URL: " << url);

    // Get the password and make sure it did not fail to decode
    const auto password = uassertStatusOKWithContext(
        uriDecode(passwordSD),
        str::stream() << "Password cannot properly be URL decoded for mongerdb:// URL: " << url);

    // 4. Validate, split, and URL decode the host identifiers.
    const auto hostIdentifiersStr = hostIdentifiers.toString();
    std::vector<HostAndPort> servers;
    for (auto i = boost::make_split_iterator(hostIdentifiersStr,
                                             boost::first_finder(",", boost::is_iequal()));
         i != std::remove_reference<decltype((i))>::type{};
         ++i) {
        const auto host = uassertStatusOKWithContext(
            uriDecode(boost::copy_range<std::string>(*i)),
            str::stream() << "Host cannot properly be URL decoded for mongerdb:// URL: " << url);

        if (host.empty()) {
            continue;
        }

        if ((host.find('/') != std::string::npos) && !StringData(host).endsWith(".sock")) {
            uasserted(
                ErrorCodes::FailedToParse,
                str::stream() << "'" << host << "' in '" << url
                              << "' appears to be a unix socket, but does not end in '.sock'");
        }

        servers.push_back(uassertStatusOK(HostAndPort::parse(host)));
    }
    if (servers.empty()) {
        uasserted(ErrorCodes::FailedToParse, "No server(s) specified");
    }

    const std::string canonicalHost = servers.front().host();
    // If we're in seedlist mode, lookup the SRV record for `_mongerdb._tcp` on the specified
    // domain name.  Take that list of servers as the new list of servers.
    if (isSeedlist) {
        if (servers.size() > 1) {
            uasserted(ErrorCodes::FailedToParse,
                      "Only a single server may be specified with a monger+srv:// url.");
        }

        const monger::dns::HostName host(canonicalHost);

        if (host.nameComponents().size() < 3) {
            uasserted(ErrorCodes::FailedToParse,
                      "A server specified with a monger+srv:// url must have at least 3 hostname "
                      "components separated by dots ('.')");
        }

        const monger::dns::HostName srvSubdomain("_mongerdb._tcp");

        const auto srvEntries =
            dns::lookupSRVRecords(srvSubdomain.resolvedIn(host).canonicalName());

        auto makeFQDN = [](dns::HostName hostName) {
            hostName.forceQualification();
            return hostName;
        };

        const monger::dns::HostName domain = makeFQDN(host.parentDomain());
        servers.clear();
        using std::begin;
        using std::end;
        std::transform(
            begin(srvEntries), end(srvEntries), back_inserter(servers), [&domain](auto&& srv) {
                const dns::HostName target(srv.host);  // FQDN

                if (!domain.contains(target)) {
                    uasserted(ErrorCodes::FailedToParse,
                              str::stream() << "Hostname " << target << " is not within the domain "
                                            << domain);
                }
                return HostAndPort(target.noncanonicalName(), srv.port);
            });
    }

    // 5. Decode the database name
    const auto database =
        uassertStatusOKWithContext(uriDecode(databaseSD),
                                   str::stream() << "Database name cannot properly be URL "
                                                    "decoded for mongerdb:// URL: "
                                                 << url);

    // 6. Validate the database contains no prohibited characters
    // Prohibited characters:
    // slash ("/"), backslash ("\"), space (" "), double-quote ("""), or dollar sign ("$")
    // period (".") is also prohibited, but drivers MAY allow periods
    if (!database.empty() &&
        !NamespaceString::validDBName(database,
                                      NamespaceString::DollarInDbNameBehavior::Disallow)) {
        uasserted(ErrorCodes::FailedToParse,
                  str::stream() << "Database name cannot have reserved "
                                   "characters for mongerdb:// URL: "
                                << url);
    }

    // 7. Validate, split, and URL decode the connection options
    auto options =
        addTXTOptions(parseOptions(connectionOptions, url), canonicalHost, url, isSeedlist);

    // If a replica set option was specified, store it in the 'setName' field.
    auto optIter = options.find("replicaSet");
    std::string setName;
    if (optIter != end(options)) {
        setName = optIter->second;
        invariant(!setName.empty());
    }

    // If an appName option was specified, validate that is 128 bytes or less.
    optIter = options.find("appName");
    if (optIter != end(options) && optIter->second.length() > 128) {
        uasserted(ErrorCodes::FailedToParse,
                  str::stream() << "appName cannot exceed 128 characters: " << optIter->second);
    }

    boost::optional<bool> retryWrites = boost::none;
    optIter = options.find("retryWrites");
    if (optIter != end(options)) {
        if (optIter->second == "true") {
            retryWrites.reset(true);
        } else if (optIter->second == "false") {
            retryWrites.reset(false);
        } else {
            uasserted(ErrorCodes::FailedToParse,
                      str::stream() << "retryWrites must be either \"true\" or \"false\"");
        }
    }

    transport::ConnectSSLMode sslMode = transport::kGlobalSSLMode;
    auto sslModeIter = std::find_if(options.begin(), options.end(), [](auto pred) {
        return pred.first == CaseInsensitiveString("ssl") ||
            pred.first == CaseInsensitiveString("tls");
    });
    if (sslModeIter != options.end()) {
        const auto& val = sslModeIter->second;
        if (val == "true") {
            sslMode = transport::kEnableSSL;
        } else if (val == "false") {
            sslMode = transport::kDisableSSL;
        } else {
            uasserted(51041, str::stream() << "tls must be either 'true' or 'false', not" << val);
        }
    }

    ConnectionString cs(
        setName.empty() ? ConnectionString::MASTER : ConnectionString::SET, servers, setName);
    return MongerURI(std::move(cs),
                    username,
                    password,
                    database,
                    std::move(retryWrites),
                    sslMode,
                    std::move(options));
}

StatusWith<MongerURI> MongerURI::parse(const std::string& url) try {
    return parseImpl(url);
} catch (const std::exception&) {
    return exceptionToStatus();
}

const boost::optional<std::string> MongerURI::getAppName() const {
    const auto optIter = _options.find("appName");
    if (optIter != end(_options)) {
        return optIter->second;
    }
    return boost::none;
}

std::string MongerURI::canonicalizeURIAsString() const {
    StringBuilder uri;
    uri << kURIPrefix;
    if (!_user.empty()) {
        uri << uriEncode(_user);
        if (!_password.empty()) {
            uri << ":" << uriEncode(_password);
        }
        uri << "@";
    }

    const auto& servers = _connectString.getServers();
    if (!servers.empty()) {
        auto delimeter = "";
        for (auto& hostAndPort : servers) {
            if (boost::count(hostAndPort.host(), ':') > 1) {
                uri << delimeter << "[" << uriEncode(hostAndPort.host()) << "]"
                    << ":" << uriEncode(std::to_string(hostAndPort.port()));
            } else if (StringData(hostAndPort.host()).endsWith(".sock")) {
                uri << delimeter << uriEncode(hostAndPort.host());
            } else {
                uri << delimeter << uriEncode(hostAndPort.host()) << ":"
                    << uriEncode(std::to_string(hostAndPort.port()));
            }
            delimeter = ",";
        }
    } else {
        uri << kDefaultMongerHost;
    }

    uri << "/";
    if (!_database.empty()) {
        uri << uriEncode(_database);
    }

    if (!_options.empty()) {
        auto delimeter = "";
        uri << "?";
        for (const auto& pair : _options) {
            uri << delimeter << uriEncode(pair.first.original()) << "=" << uriEncode(pair.second);
            delimeter = "&";
        }
    }
    return uri.str();
}
}  // namespace monger
