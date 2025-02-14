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

#pragma once

#include <boost/optional.hpp>
#include <string>
#include <vector>

#include "monger/db/jsobj.h"
#include "monger/db/namespace_string.h"
#include "monger/util/time_support.h"

namespace monger {

/**
 * This class represents the layout and contents of documents contained in the
 * config.mongers collection. All manipulation of documents coming from that
 * collection should be done with this class.
 */
class MongersType {
public:
    // Name of the mongers collection in the config server.
    static const NamespaceString ConfigNS;

    // Field names and types in the mongers collection type.
    static const BSONField<std::string> name;
    static const BSONField<Date_t> ping;
    static const BSONField<long long> uptime;
    static const BSONField<bool> waiting;
    static const BSONField<std::string> mongerVersion;
    static const BSONField<long long> configVersion;
    static const BSONField<BSONArray> advisoryHostFQDNs;

    /**
     * Returns the BSON representation of the entry.
     */
    BSONObj toBSON() const;

    /**
     * Constructs a new MongersType object from BSON.
     * Also does validation of the contents.
     */
    static StatusWith<MongersType> fromBSON(const BSONObj& source);

    /**
     * Returns OK if all fields have been set. Otherwise, returns NoSuchKey
     * and information about the first field that is missing.
     */
    Status validate() const;

    /**
     * Returns a std::string representation of the current internal state.
     */
    std::string toString() const;

    const std::string& getName() const {
        return _name.get();
    }
    void setName(const std::string& name);

    const Date_t& getPing() const {
        return _ping.get();
    }
    void setPing(const Date_t& ping);

    long long getUptime() const {
        return _uptime.get();
    }
    void setUptime(const long long uptime);

    bool getWaiting() const {
        return _waiting.get();
    }
    bool isWaitingSet() const {
        return _waiting.is_initialized();
    }
    void setWaiting(const bool waiting);

    const std::string& getMongerVersion() const {
        return _mongerVersion.get();
    }
    bool isMongerVersionSet() const {
        return _mongerVersion.is_initialized();
    }
    void setMongerVersion(const std::string& mongerVersion);

    long long getConfigVersion() const {
        return _configVersion.get();
    }
    void setConfigVersion(const long long configVersion);

    std::vector<std::string> getAdvisoryHostFQDNs() const {
        return _advisoryHostFQDNs.value_or(std::vector<std::string>());
    }
    void setAdvisoryHostFQDNs(const std::vector<std::string>& advisoryHostFQDNs);

private:
    // Convention: (M)andatory, (O)ptional, (S)pecial rule.

    // (M) "host:port" for this mongers
    boost::optional<std::string> _name;
    // (M) last time it was seen alive
    boost::optional<Date_t> _ping;
    // (M) uptime at the last ping
    boost::optional<long long> _uptime;
    // (M) used to indicate if we are going to sleep after ping. For testing purposes
    boost::optional<bool> _waiting;
    // (O) the mongerdb version of the pinging mongers
    boost::optional<std::string> _mongerVersion;
    // (O) the config version of the pinging mongers
    boost::optional<long long> _configVersion;
    // (O) the results of hostname canonicalization on the pinging mongers
    boost::optional<std::vector<std::string>> _advisoryHostFQDNs;
};

}  // namespace monger
