
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


import static java.lang.String.format;

/**
 * Top level Exception for all Monger Embedded CAPI exceptions
 */
public class MongerEmbeddedCAPIException extends RuntimeException {
    private static final long serialVersionUID = -5524416583514807953L;
    private final int code;

    /**
     * @param msg   the message
     * @param cause the cause
     */
    public MongerEmbeddedCAPIException(final String msg, final Throwable cause) {
        super(msg, cause);
        this.code = -1;
    }

    /**
     * @param code the error code
     * @param msg  the message
     * @param cause the cause
     */
    public MongerEmbeddedCAPIException(final int code, final String msg, final Throwable cause) {
        super(msg, cause);
        this.code = code;
    }
    
    /**
     * Constructs a new instance
     *
     * @param errorCode the error code
     * @param subErrorCode the sub category error code
     * @param reason the reason for the exception
     * @param cause the cause
     */
    public MongerEmbeddedCAPIException(final int errorCode, final int subErrorCode, final String reason, final Throwable cause) {
        this(errorCode, format("%s (%s:%s)", reason, errorCode, subErrorCode), cause);
    }

    /**
     * @return the error code for the exception.
     */
    public int getCode() {
        return code;
    }
}
