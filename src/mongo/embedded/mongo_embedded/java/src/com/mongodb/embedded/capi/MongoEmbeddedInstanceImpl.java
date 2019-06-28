
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

package com.mongerdb.embedded.capi;

import com.mongerdb.embedded.capi.internal.CAPI;

class MongerEmbeddedInstanceImpl implements MongerEmbeddedInstance {
    private final CAPI.monger_embedded_v1_status status;
    private final CAPI.monger_embedded_v1_instance instance;

    MongerEmbeddedInstanceImpl(final CAPI.monger_embedded_v1_lib libraryPointer, final String yamlConfig) {
        status = CAPIHelper.createStatusPointer();

        try {
            instance = CAPI.monger_embedded_v1_instance_create(libraryPointer,
                    new CAPI.cstring(yamlConfig != null ? yamlConfig : ""), status);
        } catch (Throwable t) {
            throw CAPIHelper.createError("instance_create", t);
        }

        if (instance == null) {
            CAPIHelper.createErrorFromStatus(status);
        }
    }

    @Override
    public MongerEmbeddedClient createClient() {
        return new MongerEmbeddedClientImpl(instance);
    }

    @Override
    public void close() {
        try {
            CAPIHelper.validateErrorCode(status,
                    CAPI.monger_embedded_v1_instance_destroy(instance, status));
        } catch (Throwable t) {
            throw CAPIHelper.createError("instance_destroy", t);
        }
    }
}
