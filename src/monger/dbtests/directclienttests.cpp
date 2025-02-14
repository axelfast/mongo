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

#include <iostream>

#include "monger/db/client.h"
#include "monger/db/dbdirectclient.h"
#include "monger/db/json.h"
#include "monger/db/lasterror.h"
#include "monger/dbtests/dbtests.h"
#include "monger/rpc/get_status_from_command_result.h"
#include "monger/util/timer.h"

namespace DirectClientTests {

using std::unique_ptr;
using std::vector;

class ClientBase {
public:
    ClientBase() {
        monger::LastError::get(cc()).reset();
    }
    virtual ~ClientBase() {
        monger::LastError::get(cc()).reset();
    }
};

const char* ns = "a.b";

class Capped : public ClientBase {
public:
    virtual void run() {
        // Skip the test if the storage engine doesn't support capped collections.
        if (!getGlobalServiceContext()->getStorageEngine()->supportsCappedCollections()) {
            return;
        }

        const ServiceContext::UniqueOperationContext opCtxPtr = cc().makeOperationContext();
        OperationContext& opCtx = *opCtxPtr;
        DBDirectClient client(&opCtx);
        for (int pass = 0; pass < 3; pass++) {
            client.createCollection(ns, 1024 * 1024, true, 999);
            for (int j = 0; j < pass * 3; j++)
                client.insert(ns, BSON("x" << j));

            // test truncation of a capped collection
            if (pass) {
                BSONObj info;
                BSONObj cmd = BSON("captrunc"
                                   << "b"
                                   << "n"
                                   << 1
                                   << "inc"
                                   << true);
                // cout << cmd.toString() << endl;
                bool ok = client.runCommand("a", cmd, info);
                // cout << info.toString() << endl;
                verify(ok);
            }

            verify(client.dropCollection(ns));
        }
    }
};

class InsertMany : ClientBase {
public:
    virtual void run() {
        const ServiceContext::UniqueOperationContext opCtxPtr = cc().makeOperationContext();
        OperationContext& opCtx = *opCtxPtr;
        DBDirectClient client(&opCtx);

        vector<BSONObj> objs;
        objs.push_back(BSON("_id" << 1));
        objs.push_back(BSON("_id" << 1));
        objs.push_back(BSON("_id" << 2));


        client.dropCollection(ns);
        client.insert(ns, objs);
        ASSERT_EQUALS(client.getLastErrorDetailed()["code"].numberInt(), 11000);
        ASSERT_EQUALS((int)client.count(ns), 1);

        client.dropCollection(ns);
        client.insert(ns, objs, InsertOption_ContinueOnError);
        ASSERT_EQUALS(client.getLastErrorDetailed()["code"].numberInt(), 11000);
        ASSERT_EQUALS((int)client.count(ns), 2);
    }
};

class BadNSCmd : ClientBase {
public:
    virtual void run() {
        const ServiceContext::UniqueOperationContext opCtxPtr = cc().makeOperationContext();
        OperationContext& opCtx = *opCtxPtr;
        DBDirectClient client(&opCtx);

        BSONObj result;
        BSONObj cmdObj = BSON("count"
                              << "");
        ASSERT(!client.runCommand("", cmdObj, result)) << result;
        ASSERT_EQ(getStatusFromCommandResult(result), ErrorCodes::InvalidNamespace);
    }
};

class BadNSQuery : ClientBase {
public:
    virtual void run() {
        const ServiceContext::UniqueOperationContext opCtxPtr = cc().makeOperationContext();
        OperationContext& opCtx = *opCtxPtr;
        DBDirectClient client(&opCtx);

        ASSERT_THROWS_CODE(client.query(NamespaceString(), Query(), 1)->nextSafe(),
                           AssertionException,
                           ErrorCodes::InvalidNamespace);
    }
};

class BadNSGetMore : ClientBase {
public:
    virtual void run() {
        const ServiceContext::UniqueOperationContext opCtxPtr = cc().makeOperationContext();
        OperationContext& opCtx = *opCtxPtr;
        DBDirectClient client(&opCtx);

        ASSERT_THROWS_CODE(
            client.getMore("", 1, 1)->nextSafe(), AssertionException, ErrorCodes::InvalidNamespace);
    }
};

class BadNSInsert : ClientBase {
public:
    virtual void run() {
        const ServiceContext::UniqueOperationContext opCtxPtr = cc().makeOperationContext();
        OperationContext& opCtx = *opCtxPtr;
        DBDirectClient client(&opCtx);

        client.insert("", BSONObj(), 0);
        ASSERT(!client.getLastError().empty());
    }
};

class BadNSUpdate : ClientBase {
public:
    virtual void run() {
        const ServiceContext::UniqueOperationContext opCtxPtr = cc().makeOperationContext();
        OperationContext& opCtx = *opCtxPtr;
        DBDirectClient client(&opCtx);

        client.update("", Query(), BSON("$set" << BSON("x" << 1)));
        ASSERT(!client.getLastError().empty());
    }
};

class BadNSRemove : ClientBase {
public:
    virtual void run() {
        const ServiceContext::UniqueOperationContext opCtxPtr = cc().makeOperationContext();
        OperationContext& opCtx = *opCtxPtr;
        DBDirectClient client(&opCtx);

        client.remove("", Query());
        ASSERT(!client.getLastError().empty());
    }
};

class All : public Suite {
public:
    All() : Suite("directclient") {}
    void setupTests() {
        add<Capped>();
        add<InsertMany>();
        add<BadNSCmd>();
        add<BadNSQuery>();
        add<BadNSGetMore>();
        add<BadNSInsert>();
        add<BadNSUpdate>();
        add<BadNSRemove>();
    }
};

SuiteInstance<All> myall;
}  // namespace DirectClientTests
