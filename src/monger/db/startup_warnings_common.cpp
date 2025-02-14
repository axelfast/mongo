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

#define MONGO_LOG_DEFAULT_COMPONENT ::monger::logger::LogComponent::kControl

#include "monger/platform/basic.h"

#include "monger/db/startup_warnings_common.h"

#include <boost/filesystem/operations.hpp>
#include <fstream>

#include "monger/client/authenticate.h"
#include "monger/config.h"
#include "monger/db/server_options.h"
#include "monger/util/log.h"
#include "monger/util/net/ssl_options.h"
#include "monger/util/processinfo.h"
#include "monger/util/version.h"

namespace monger {

//
// system warnings
//
void logCommonStartupWarnings(const ServerGlobalParams& serverParams) {
    // each message adds a leading and a trailing newline

    bool warned = false;
    {
        auto&& vii = VersionInfoInterface::instance();
        if ((vii.minorVersion() % 2) != 0) {
            log() << startupWarningsLog;
            log() << "** NOTE: This is a development version (" << vii.version() << ") of MongerDB."
                  << startupWarningsLog;
            log() << "**       Not recommended for production." << startupWarningsLog;
            warned = true;
        }
    }

    if (serverParams.authState == ServerGlobalParams::AuthState::kUndefined) {
        log() << startupWarningsLog;
        log() << "** WARNING: Access control is not enabled for the database."
              << startupWarningsLog;
        log() << "**          Read and write access to data and configuration is "
                 "unrestricted."
              << startupWarningsLog;
        warned = true;
    }

    const bool is32bit = sizeof(int*) == 4;
    if (is32bit) {
        log() << startupWarningsLog;
        log() << "** WARNING: This 32-bit MongerDB binary is deprecated" << startupWarningsLog;
        warned = true;
    }

#ifdef MONGO_CONFIG_SSL
    if (sslGlobalParams.sslAllowInvalidCertificates) {
        log() << "** WARNING: While invalid X509 certificates may be used to" << startupWarningsLog;
        log() << "**          connect to this server, they will not be considered"
              << startupWarningsLog;
        log() << "**          permissible for authentication." << startupWarningsLog;
        log() << startupWarningsLog;
    }

    if (sslGlobalParams.sslAllowInvalidHostnames) {
        log() << "** WARNING: This server will not perform X.509 hostname validation"
              << startupWarningsLog;
        log() << "** This may allow your server to make or accept connections to"
              << startupWarningsLog;
        log() << "** untrusted parties" << startupWarningsLog;
    }
#endif

    /*
    * We did not add the message to startupWarningsLog as the user can not
    * specify a sslCAFile parameter from the shell
    */
    if (sslGlobalParams.sslMode.load() != SSLParams::SSLMode_disabled &&
#ifdef MONGO_CONFIG_SSL_CERTIFICATE_SELECTORS
        sslGlobalParams.sslCertificateSelector.empty() &&
#endif
        sslGlobalParams.sslCAFile.empty()) {
        log() << "";
        log() << "** WARNING: No client certificate validation can be performed since"
                 " no CA file has been provided";
#ifdef MONGO_CONFIG_SSL_CERTIFICATE_SELECTORS
        log() << "**          and no sslCertificateSelector has been specified.";
#endif
        log() << "**          Please specify an sslCAFile parameter.";
    }

#if defined(_WIN32) && !defined(_WIN64)
    // Warn user that they are running a 32-bit app on 64-bit Windows
    BOOL wow64Process;
    BOOL retWow64 = IsWow64Process(GetCurrentProcess(), &wow64Process);
    if (retWow64 && wow64Process) {
        log() << "** NOTE: This is a 32-bit MongerDB binary running on a 64-bit operating"
              << startupWarningsLog;
        log() << "**      system. Switch to a 64-bit build of MongerDB to" << startupWarningsLog;
        log() << "**      support larger databases." << startupWarningsLog;
        warned = true;
    }
#endif

#if !defined(_WIN32)
    if (getuid() == 0) {
        log() << "** WARNING: You are running this process as the root user, "
              << "which is not recommended." << startupWarningsLog;
        warned = true;
    }
#endif

    if (serverParams.bind_ips.empty()) {
        log() << startupWarningsLog;
        log() << "** WARNING: This server is bound to localhost." << startupWarningsLog;
        log() << "**          Remote systems will be unable to connect to this server. "
              << startupWarningsLog;
        log() << "**          Start the server with --bind_ip <address> to specify which IP "
              << startupWarningsLog;
        log() << "**          addresses it should serve responses from, or with --bind_ip_all to"
              << startupWarningsLog;
        log() << "**          bind to all interfaces. If this behavior is desired, start the"
              << startupWarningsLog;
        log() << "**          server with --bind_ip 127.0.0.1 to disable this warning."
              << startupWarningsLog;
        warned = true;
    }

    if (auth::hasMultipleInternalAuthKeys()) {
        log() << startupWarningsLog;
        log() << "** WARNING: Multiple keys specified in security key file. If cluster key file"
              << startupWarningsLog;
        log() << "            rollover is not in progress, only one key should be specified in"
              << startupWarningsLog;
        log() << "            the key file" << startupWarningsLog;
        warned = true;
    }

    if (warned) {
        log() << startupWarningsLog;
    }
}
}  // namespace monger
