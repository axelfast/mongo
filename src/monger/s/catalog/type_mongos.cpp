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
#include "monger/s/catalog/type_mongers.h"

#include "monger/base/status_with.h"
#include "monger/bson/bsonobj.h"
#include "monger/bson/bsonobjbuilder.h"
#include "monger/bson/util/bson_extract.h"
#include "monger/util/assert_util.h"
#include "monger/util/str.h"

namespace monger {
const NamespaceString MongersType::ConfigNS("config.mongers");

const BSONField<std::string> MongersType::name("_id");
const BSONField<Date_t> MongersType::ping("ping");
const BSONField<long long> MongersType::uptime("up");
const BSONField<bool> MongersType::waiting("waiting");
const BSONField<std::string> MongersType::mongerVersion("mongerVersion");
const BSONField<long long> MongersType::configVersion("configVersion");
const BSONField<BSONArray> MongersType::advisoryHostFQDNs("advisoryHostFQDNs");

StatusWith<MongersType> MongersType::fromBSON(const BSONObj& source) {
    MongersType mt;

    {
        std::string mtName;
        Status status = bsonExtractStringField(source, name.name(), &mtName);
        if (!status.isOK())
            return status;
        mt._name = mtName;
    }

    {
        BSONElement mtPingElem;
        Status status = bsonExtractTypedField(source, ping.name(), BSONType::Date, &mtPingElem);
        if (!status.isOK())
            return status;
        mt._ping = mtPingElem.date();
    }

    {
        long long mtUptime;
        Status status = bsonExtractIntegerField(source, uptime.name(), &mtUptime);
        if (!status.isOK())
            return status;
        mt._uptime = mtUptime;
    }

    {
        bool mtWaiting;
        Status status = bsonExtractBooleanField(source, waiting.name(), &mtWaiting);
        if (!status.isOK())
            return status;
        mt._waiting = mtWaiting;
    }

    if (source.hasField(mongerVersion.name())) {
        std::string mtMongerVersion;
        Status status = bsonExtractStringField(source, mongerVersion.name(), &mtMongerVersion);
        if (!status.isOK())
            return status;
        mt._mongerVersion = mtMongerVersion;
    }

    if (source.hasField(configVersion.name())) {
        long long mtConfigVersion;
        Status status = bsonExtractIntegerField(source, configVersion.name(), &mtConfigVersion);
        if (!status.isOK())
            return status;
        mt._configVersion = mtConfigVersion;
    }

    if (source.hasField(advisoryHostFQDNs.name())) {
        mt._advisoryHostFQDNs = std::vector<std::string>();
        BSONElement array;
        Status status = bsonExtractTypedField(source, advisoryHostFQDNs.name(), Array, &array);
        if (!status.isOK())
            return status;

        BSONObjIterator it(array.Obj());
        while (it.more()) {
            BSONElement arrayElement = it.next();
            if (arrayElement.type() != String) {
                return Status(ErrorCodes::TypeMismatch,
                              str::stream() << "Elements in \"" << advisoryHostFQDNs.name()
                                            << "\" array must be strings but found "
                                            << typeName(arrayElement.type()));
            }
            mt._advisoryHostFQDNs->push_back(arrayElement.String());
        }
    }

    return mt;
}

Status MongersType::validate() const {
    if (!_name.is_initialized() || _name->empty()) {
        return {ErrorCodes::NoSuchKey, str::stream() << "missing " << name.name() << " field"};
    }

    if (!_ping.is_initialized()) {
        return {ErrorCodes::NoSuchKey, str::stream() << "missing " << ping.name() << " field"};
    }

    if (!_uptime.is_initialized()) {
        return {ErrorCodes::NoSuchKey, str::stream() << "missing " << uptime.name() << " field"};
    }

    if (!_waiting.is_initialized()) {
        return {ErrorCodes::NoSuchKey, str::stream() << "missing " << waiting.name() << " field"};
    }

    return Status::OK();
}

BSONObj MongersType::toBSON() const {
    BSONObjBuilder builder;

    if (_name)
        builder.append(name.name(), getName());
    if (_ping)
        builder.append(ping.name(), getPing());
    if (_uptime)
        builder.append(uptime.name(), getUptime());
    if (_waiting)
        builder.append(waiting.name(), getWaiting());
    if (_mongerVersion)
        builder.append(mongerVersion.name(), getMongerVersion());
    if (_configVersion)
        builder.append(configVersion.name(), getConfigVersion());
    if (_advisoryHostFQDNs)
        builder.append(advisoryHostFQDNs.name(), getAdvisoryHostFQDNs());

    return builder.obj();
}

void MongersType::setName(const std::string& name) {
    invariant(!name.empty());
    _name = name;
}

void MongersType::setPing(const Date_t& ping) {
    _ping = ping;
}

void MongersType::setUptime(long long uptime) {
    _uptime = uptime;
}

void MongersType::setWaiting(bool waiting) {
    _waiting = waiting;
}

void MongersType::setMongerVersion(const std::string& mongerVersion) {
    invariant(!mongerVersion.empty());
    _mongerVersion = mongerVersion;
}

void MongersType::setConfigVersion(const long long configVersion) {
    _configVersion = configVersion;
}

void MongersType::setAdvisoryHostFQDNs(const std::vector<std::string>& advisoryHostFQDNs) {
    _advisoryHostFQDNs = advisoryHostFQDNs;
}

std::string MongersType::toString() const {
    return toBSON().toString();
}

}  // namespace monger
