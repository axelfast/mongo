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

#include "monger/platform/basic.h"

#include "monger/config.h"

#include "monger/util/net/ssl_options.h"

#include <boost/range/size.hpp>
#include <ostream>

#include "monger/base/global_initializer.h"
#include "monger/base/init.h"
#include "monger/base/initializer.h"
#include "monger/db/server_options_base.h"
#include "monger/db/server_options_server_helpers.h"
#include "monger/unittest/unittest.h"
#include "monger/util/cmdline_utils/censor_cmdline_test.h"
#include "monger/util/net/ssl_options.h"
#include "monger/util/options_parser/environment.h"
#include "monger/util/options_parser/option_section.h"
#include "monger/util/options_parser/options_parser.h"
#include "monger/util/options_parser/startup_options.h"

namespace moe = monger::optionenvironment;

namespace monger {
namespace {

MONGO_INITIALIZER(ServerLogRedirection)(InitializerContext*) {
    // ssl_options_server.cpp has an initializer which depends on logging.
    // We can stub that dependency out for unit testing purposes.
    return Status::OK();
}

Status executeInitializer(const std::string& name) try {
    const auto* node =
        getGlobalInitializer().getInitializerDependencyGraph().getInitializerNode(name);
    if (!node) {
        return {ErrorCodes::BadValue, str::stream() << "Unknown initializer: '" << name << "'"};
    }

    const auto& fn = node->getInitializerFunction();
    if (!fn) {
        return {ErrorCodes::InternalError,
                str::stream() << "Initializer node '" << name << "' has no associated function."};
    }

    // The initializers we call don't actually need a context currently.
    return fn(nullptr);
} catch (const DBException& ex) {
    return ex.toStatus();
}

Status addSSLServerOptions() {
    return executeInitializer("SSLServerOptionsIDL_Register");
}

Status storeSSLServerOptions() {
    auto status = executeInitializer("SSLServerOptionsIDL_Store");
    if (!status.isOK()) {
        return status;
    }

    return executeInitializer("SSLServerOptions_Post");
}

namespace test {
struct Vector : public std::vector<uint8_t> {
    Vector(std::vector<uint8_t> v) : std::vector<uint8_t>(std::move(v)) {}
};
std::ostream& operator<<(std::ostream& ss, const Vector& val) {
    ss << '{';
    std::string comma;
    for (const auto& b : val) {
        ss << comma << b;
        comma = ", ";
    }
    ss << '}';
    return ss;
}
}  // namespace test

TEST(SSLOptions, validCases) {
    SSLParams::CertificateSelector selector;

    ASSERT_OK(parseCertificateSelector(&selector, "subj", "subject=test.example.com"));
    ASSERT_EQ(selector.subject, "test.example.com");

    ASSERT_OK(parseCertificateSelector(&selector, "hash", "thumbprint=0123456789"));
    ASSERT_EQ(test::Vector(selector.thumbprint), test::Vector({0x01, 0x23, 0x45, 0x67, 0x89}));
}

TEST(SSLOptions, invalidCases) {
    SSLParams::CertificateSelector selector;

    auto status = parseCertificateSelector(&selector, "option", "bogus=nothing");
    ASSERT_NOT_OK(status);
    ASSERT_EQ(status.reason(), "Unknown certificate selector property for 'option': 'bogus'");

    status = parseCertificateSelector(&selector, "option", "thumbprint=0123456");
    ASSERT_NOT_OK(status);
    ASSERT_EQ(status.reason(),
              "Invalid certificate selector value for 'option': Not an even number of hexits");

    status = parseCertificateSelector(&selector, "option", "thumbprint=bogus");
    ASSERT_NOT_OK(status);
    ASSERT_EQ(status.reason(),
              "Invalid certificate selector value for 'option': Not a valid hex string");
}

class OptionsParserTester : public moe::OptionsParser {
public:
    Status readConfigFile(const std::string& filename, std::string* config, ConfigExpand) {
        if (filename != _filename) {
            ::monger::StringBuilder sb;
            sb << "Parser using filename: " << filename
               << " which does not match expected filename: " << _filename;
            return Status(ErrorCodes::InternalError, sb.str());
        }
        *config = _config;
        return Status::OK();
    }
    void setConfig(const std::string& filename, const std::string& config) {
        _filename = filename;
        _config = config;
    }

private:
    std::string _filename;
    std::string _config;
};

TEST(SetupOptions, tlsModeDisabled) {
    moe::startupOptions = moe::OptionSection();
    moe::startupOptionsParsed = moe::Environment();

    ASSERT_OK(::monger::addGeneralServerOptions(&moe::startupOptions));
    ASSERT_OK(addSSLServerOptions());

    std::vector<std::string> argv;
    argv.push_back("binaryname");
    argv.push_back("--tlsMode");
    argv.push_back("disabled");
    std::map<std::string, std::string> env_map;

    OptionsParserTester parser;
    ASSERT_OK(parser.run(moe::startupOptions, argv, env_map, &moe::startupOptionsParsed));
    ASSERT_OK(storeSSLServerOptions());
    ASSERT_EQ(::monger::sslGlobalParams.sslMode.load(), ::monger::sslGlobalParams.SSLMode_disabled);
}

TEST(SetupOptions, sslModeDisabled) {
    moe::startupOptions = moe::OptionSection();
    moe::startupOptionsParsed = moe::Environment();

    ASSERT_OK(::monger::addGeneralServerOptions(&moe::startupOptions));
    ASSERT_OK(addSSLServerOptions());

    std::vector<std::string> argv;
    argv.push_back("binaryname");
    argv.push_back("--sslMode");
    argv.push_back("disabled");
    std::map<std::string, std::string> env_map;

    OptionsParserTester parser;
    ASSERT_OK(parser.run(moe::startupOptions, argv, env_map, &moe::startupOptionsParsed));
    ASSERT_OK(storeSSLServerOptions());
    ASSERT_EQ(::monger::sslGlobalParams.sslMode.load(), ::monger::sslGlobalParams.SSLMode_disabled);
}

TEST(SetupOptions, tlsModeRequired) {
    moe::startupOptions = moe::OptionSection();
    moe::startupOptionsParsed = moe::Environment();

    ASSERT_OK(::monger::addGeneralServerOptions(&moe::startupOptions));
    ASSERT_OK(addSSLServerOptions());

    std::string sslPEMKeyFile = "jstests/libs/server.pem";
    std::string sslCAFFile = "jstests/libs/ca.pem";
    std::string sslCRLFile = "jstests/libs/crl.pem";
    std::string sslClusterFile = "jstests/libs/cluster_cert.pem";

    std::vector<std::string> argv;
    argv.push_back("binaryname");
    argv.push_back("--tlsMode");
    argv.push_back("requireTLS");
    argv.push_back("--tlsCertificateKeyFile");
    argv.push_back(sslPEMKeyFile);
    argv.push_back("--tlsCAFile");
    argv.push_back(sslCAFFile);
    argv.push_back("--tlsCRLFile");
    argv.push_back(sslCRLFile);
    argv.push_back("--tlsClusterFile");
    argv.push_back(sslClusterFile);
    argv.push_back("--tlsAllowInvalidHostnames");
    argv.push_back("--tlsAllowInvalidCertificates");
    argv.push_back("--tlsWeakCertificateValidation");
    argv.push_back("--tlsFIPSMode");
    argv.push_back("--tlsCertificateKeyFilePassword");
    argv.push_back("pw1");
    argv.push_back("--tlsClusterPassword");
    argv.push_back("pw2");
    argv.push_back("--tlsDisabledProtocols");
    argv.push_back("TLS1_1");
    argv.push_back("--tlsLogVersions");
    argv.push_back("TLS1_2");
    std::map<std::string, std::string> env_map;

    OptionsParserTester parser;
    ASSERT_OK(parser.run(moe::startupOptions, argv, env_map, &moe::startupOptionsParsed));
    ASSERT_OK(storeSSLServerOptions());

    ASSERT_EQ(::monger::sslGlobalParams.sslMode.load(), ::monger::sslGlobalParams.SSLMode_requireSSL);
    ASSERT_EQ(::monger::sslGlobalParams.sslPEMKeyFile.substr(
                  ::monger::sslGlobalParams.sslPEMKeyFile.length() - sslPEMKeyFile.length()),
              sslPEMKeyFile);
    ASSERT_EQ(::monger::sslGlobalParams.sslCAFile.substr(
                  ::monger::sslGlobalParams.sslCAFile.length() - sslCAFFile.length()),
              sslCAFFile);
    ASSERT_EQ(::monger::sslGlobalParams.sslCRLFile.substr(
                  ::monger::sslGlobalParams.sslCRLFile.length() - sslCRLFile.length()),
              sslCRLFile);
    ASSERT_EQ(::monger::sslGlobalParams.sslClusterFile.substr(
                  ::monger::sslGlobalParams.sslClusterFile.length() - sslClusterFile.length()),
              sslClusterFile);
    ASSERT_EQ(::monger::sslGlobalParams.sslAllowInvalidHostnames, true);
    ASSERT_EQ(::monger::sslGlobalParams.sslAllowInvalidCertificates, true);
    ASSERT_EQ(::monger::sslGlobalParams.sslWeakCertificateValidation, true);
    ASSERT_EQ(::monger::sslGlobalParams.sslFIPSMode, true);
    ASSERT_EQ(::monger::sslGlobalParams.sslPEMKeyPassword, "pw1");
    ASSERT_EQ(::monger::sslGlobalParams.sslClusterPassword, "pw2");
    ASSERT_EQ(static_cast<int>(::monger::sslGlobalParams.sslDisabledProtocols.back()),
              static_cast<int>(::monger::SSLParams::Protocols::TLS1_1));
    ASSERT_EQ(static_cast<int>(::monger::sslGlobalParams.tlsLogVersions.back()),
              static_cast<int>(::monger::SSLParams::Protocols::TLS1_2));
}

TEST(SetupOptions, sslModeRequired) {
    moe::startupOptions = moe::OptionSection();
    moe::startupOptionsParsed = moe::Environment();

    ASSERT_OK(::monger::addGeneralServerOptions(&moe::startupOptions));
    ASSERT_OK(addSSLServerOptions());

    std::string sslPEMKeyFile = "jstests/libs/server.pem";
    std::string sslCAFFile = "jstests/libs/ca.pem";
    std::string sslCRLFile = "jstests/libs/crl.pem";
    std::string sslClusterFile = "jstests/libs/cluster_cert.pem";

    std::vector<std::string> argv;
    argv.push_back("binaryname");
    argv.push_back("--sslMode");
    argv.push_back("requireSSL");
    argv.push_back("--sslPEMKeyFile");
    argv.push_back(sslPEMKeyFile);
    argv.push_back("--sslCAFile");
    argv.push_back(sslCAFFile);
    argv.push_back("--sslCRLFile");
    argv.push_back(sslCRLFile);
    argv.push_back("--sslClusterFile");
    argv.push_back(sslClusterFile);
    argv.push_back("--sslAllowInvalidHostnames");
    argv.push_back("--sslAllowInvalidCertificates");
    argv.push_back("--sslWeakCertificateValidation");
    argv.push_back("--sslFIPSMode");
    argv.push_back("--sslPEMKeyPassword");
    argv.push_back("pw1");
    argv.push_back("--sslClusterPassword");
    argv.push_back("pw2");
    argv.push_back("--sslDisabledProtocols");
    argv.push_back("TLS1_1");
    argv.push_back("--tlsLogVersions");
    argv.push_back("TLS1_0");
    std::map<std::string, std::string> env_map;

    OptionsParserTester parser;
    ASSERT_OK(parser.run(moe::startupOptions, argv, env_map, &moe::startupOptionsParsed));
    ASSERT_OK(storeSSLServerOptions());

    ASSERT_EQ(::monger::sslGlobalParams.sslMode.load(), ::monger::sslGlobalParams.SSLMode_requireSSL);
    ASSERT_EQ(::monger::sslGlobalParams.sslPEMKeyFile.substr(
                  ::monger::sslGlobalParams.sslPEMKeyFile.length() - sslPEMKeyFile.length()),
              sslPEMKeyFile);
    ASSERT_EQ(::monger::sslGlobalParams.sslCAFile.substr(
                  ::monger::sslGlobalParams.sslCAFile.length() - sslCAFFile.length()),
              sslCAFFile);
    ASSERT_EQ(::monger::sslGlobalParams.sslCRLFile.substr(
                  ::monger::sslGlobalParams.sslCRLFile.length() - sslCRLFile.length()),
              sslCRLFile);
    ASSERT_EQ(::monger::sslGlobalParams.sslClusterFile.substr(
                  ::monger::sslGlobalParams.sslClusterFile.length() - sslClusterFile.length()),
              sslClusterFile);
    ASSERT_EQ(::monger::sslGlobalParams.sslAllowInvalidHostnames, true);
    ASSERT_EQ(::monger::sslGlobalParams.sslAllowInvalidCertificates, true);
    ASSERT_EQ(::monger::sslGlobalParams.sslWeakCertificateValidation, true);
    ASSERT_EQ(::monger::sslGlobalParams.sslFIPSMode, true);
    ASSERT_EQ(::monger::sslGlobalParams.sslPEMKeyPassword, "pw1");
    ASSERT_EQ(::monger::sslGlobalParams.sslClusterPassword, "pw2");
    ASSERT_EQ(static_cast<int>(::monger::sslGlobalParams.sslDisabledProtocols.back()),
              static_cast<int>(::monger::SSLParams::Protocols::TLS1_1));
    ASSERT_EQ(static_cast<int>(::monger::sslGlobalParams.tlsLogVersions.back()),
              static_cast<int>(::monger::SSLParams::Protocols::TLS1_0));
}

#ifdef MONGO_CONFIG_SSL_CERTIFICATE_SELECTORS
TEST(SetupOptions, tlsModeRequiredCertificateSelector) {
    moe::startupOptions = moe::OptionSection();
    moe::startupOptionsParsed = moe::Environment();

    ASSERT_OK(::monger::addGeneralServerOptions(&moe::startupOptions));
    ASSERT_OK(addSSLServerOptions());

    std::vector<std::string> argv;
    argv.push_back("binaryname");
    argv.push_back("--tlsMode");
    argv.push_back("requireTLS");
    argv.push_back("--tlsCertificateSelector");
    argv.push_back("subject=Subject 1");
    argv.push_back("--tlsClusterCertificateSelector");
    argv.push_back("subject=Subject 2");
    std::map<std::string, std::string> env_map;

    OptionsParserTester parser;
    ASSERT_OK(parser.run(moe::startupOptions, argv, env_map, &moe::startupOptionsParsed));
    ASSERT_OK(storeSSLServerOptions());

    ASSERT_EQ(::monger::sslGlobalParams.sslMode.load(), ::monger::sslGlobalParams.SSLMode_requireSSL);
    ASSERT_EQ(::monger::sslGlobalParams.sslCertificateSelector.subject, "Subject 1");
    ASSERT_EQ(::monger::sslGlobalParams.sslClusterCertificateSelector.subject, "Subject 2");
}

TEST(SetupOptions, sslModeRequiredCertificateSelector) {
    moe::startupOptions = moe::OptionSection();
    moe::startupOptionsParsed = moe::Environment();

    ASSERT_OK(::monger::addGeneralServerOptions(&moe::startupOptions));
    ASSERT_OK(addSSLServerOptions());

    std::vector<std::string> argv;
    argv.push_back("binaryname");
    argv.push_back("--sslMode");
    argv.push_back("requireSSL");
    argv.push_back("--sslCertificateSelector");
    argv.push_back("subject=Subject 1");
    argv.push_back("--sslClusterCertificateSelector");
    argv.push_back("subject=Subject 2");
    std::map<std::string, std::string> env_map;

    OptionsParserTester parser;
    ASSERT_OK(parser.run(moe::startupOptions, argv, env_map, &moe::startupOptionsParsed));
    ASSERT_OK(storeSSLServerOptions());

    ASSERT_EQ(::monger::sslGlobalParams.sslMode.load(), ::monger::sslGlobalParams.SSLMode_requireSSL);
    ASSERT_EQ(::monger::sslGlobalParams.sslCertificateSelector.subject, "Subject 1");
    ASSERT_EQ(::monger::sslGlobalParams.sslClusterCertificateSelector.subject, "Subject 2");
}
#endif

TEST(SetupOptions, disableNonSSLConnectionLoggingFalse) {
    OptionsParserTester parser;
    moe::Environment environment;
    moe::OptionSection options;

    ASSERT_OK(::monger::addGeneralServerOptions(&options));

    std::vector<std::string> argv;
    argv.push_back("binaryname");
    argv.push_back("--setParameter");
    argv.push_back("disableNonSSLConnectionLogging=false");
    std::map<std::string, std::string> env_map;

    ASSERT_OK(parser.run(options, argv, env_map, &environment));
    ASSERT_OK(monger::storeServerOptions(environment));

    ASSERT_EQ(::monger::sslGlobalParams.disableNonSSLConnectionLogging, false);
}

TEST(SetupOptions, disableNonTLSConnectionLoggingFalse) {
    OptionsParserTester parser;
    moe::Environment environment;
    moe::OptionSection options;

    ::monger::sslGlobalParams.disableNonSSLConnectionLoggingSet = false;
    ASSERT_OK(::monger::addGeneralServerOptions(&options));

    std::vector<std::string> argv;
    argv.push_back("binaryname");
    argv.push_back("--setParameter");
    argv.push_back("disableNonTLSConnectionLogging=false");
    std::map<std::string, std::string> env_map;

    ASSERT_OK(parser.run(options, argv, env_map, &environment));
    Status storeRet = monger::storeServerOptions(environment);

    ASSERT_EQ(::monger::sslGlobalParams.disableNonSSLConnectionLogging, false);
}

TEST(SetupOptions, disableNonSSLConnectionLoggingTrue) {
    OptionsParserTester parser;
    moe::Environment environment;
    moe::OptionSection options;

    ::monger::sslGlobalParams.disableNonSSLConnectionLoggingSet = false;
    ASSERT_OK(::monger::addGeneralServerOptions(&options));

    std::vector<std::string> argv;
    argv.push_back("binaryname");
    argv.push_back("--setParameter");
    argv.push_back("disableNonSSLConnectionLogging=true");
    std::map<std::string, std::string> env_map;

    ASSERT_OK(parser.run(options, argv, env_map, &environment));
    Status storeRet = monger::storeServerOptions(environment);

    ASSERT_EQ(::monger::sslGlobalParams.disableNonSSLConnectionLogging, true);
}

TEST(SetupOptions, disableNonTLSConnectionLoggingTrue) {
    OptionsParserTester parser;
    moe::Environment environment;
    moe::OptionSection options;

    ::monger::sslGlobalParams.disableNonSSLConnectionLoggingSet = false;
    ASSERT_OK(::monger::addGeneralServerOptions(&options));

    std::vector<std::string> argv;
    argv.push_back("binaryname");
    argv.push_back("--setParameter");
    argv.push_back("disableNonTLSConnectionLogging=true");
    std::map<std::string, std::string> env_map;

    ASSERT_OK(parser.run(options, argv, env_map, &environment));
    Status storeRet = monger::storeServerOptions(environment);

    ASSERT_EQ(::monger::sslGlobalParams.disableNonSSLConnectionLogging, true);
}

TEST(SetupOptions, disableNonTLSConnectionLoggingInvalid) {
    OptionsParserTester parser;
    moe::Environment environment;
    moe::OptionSection options;

    ASSERT_OK(::monger::addGeneralServerOptions(&options));

    std::vector<std::string> argv;
    argv.push_back("binaryname");
    argv.push_back("--setParameter");
    argv.push_back("disableNonTLSConnectionLogging=false");
    argv.push_back("--setParameter");
    argv.push_back("disableNonSSLConnectionLogging=false");
    std::map<std::string, std::string> env_map;

    ASSERT_OK(parser.run(options, argv, env_map, &environment));
    ASSERT_NOT_OK(monger::storeServerOptions(environment));
}

TEST(SetupOptions, RedactionSingleName) {
    const std::vector<std::string> argv({"mongerd",
                                         "--tlsMode",
                                         "requireTLS",
                                         "--tlsCertificateKeyFilePassword=qwerty",
                                         "--tlsClusterPassword",
                                         "Lose Me.",
                                         "--sslPEMKeyPassword=qwerty",
                                         "--sslClusterPassword=qwerty"});

    const std::vector<std::string> expected({"mongerd",
                                             "--tlsMode",
                                             "requireTLS",
                                             "--tlsCertificateKeyFilePassword=<password>",
                                             "--tlsClusterPassword",
                                             "<password>",
                                             "--sslPEMKeyPassword=<password>",
                                             "--sslClusterPassword=<password>"});

    ASSERT_EQ(expected.size(), argv.size());
    ::monger::test::censoringVector(expected, argv);
}

TEST(SetupOptions, RedactionDottedName) {
    auto obj = BSON("net" << BSON("tls" << BSON("mode"
                                                << "requireTLS"
                                                << "certificateKeyFilePassword"
                                                << "qwerty"
                                                << "ClusterPassword"
                                                << "qwerty")
                                        << "ssl"
                                        << BSON("mode"
                                                << "requireSSL"
                                                << "PEMKeyPassword"
                                                << "qwerty"
                                                << "ClusterPassword"
                                                << "qwerty")));

    auto res = BSON("net" << BSON("tls" << BSON("mode"
                                                << "requireTLS"
                                                << "certificateKeyFilePassword"
                                                << "<password>"
                                                << "ClusterPassword"
                                                << "<password>")
                                        << "ssl"
                                        << BSON("mode"
                                                << "requireSSL"
                                                << "PEMKeyPassword"
                                                << "<password>"
                                                << "ClusterPassword"
                                                << "<password>")));

    cmdline_utils::censorBSONObj(&obj);
    ASSERT_BSONOBJ_EQ(res, obj);
}

}  // namespace
}  // namespace monger
