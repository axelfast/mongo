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

#include "monger/platform/shared_library.h"

#include <boost/filesystem.hpp>

#include "monger/util/assert_util.h"
#include "monger/util/log.h"
#include "monger/util/str.h"
#include "monger/util/text.h"

namespace monger {

SharedLibrary::~SharedLibrary() {
    if (_handle) {
        if (FreeLibrary(static_cast<HMODULE>(_handle)) == 0) {
            DWORD lasterror = GetLastError();
            LOG(2) << "Load library close failed: " << errnoWithDescription(lasterror);
        }
    }
}

StatusWith<std::unique_ptr<SharedLibrary>> SharedLibrary::create(
    const boost::filesystem::path& full_path) {
    LOG(1) << "Loading library: " << toUtf8String(full_path.c_str());

    HMODULE handle = LoadLibraryW(full_path.c_str());
    if (handle == nullptr) {
        return StatusWith<std::unique_ptr<SharedLibrary>>(ErrorCodes::InternalError,
                                                          str::stream() << "Load library failed: "
                                                                        << errnoWithDescription());
    }

    return StatusWith<std::unique_ptr<SharedLibrary>>(
        std::unique_ptr<SharedLibrary>(new SharedLibrary(handle)));
}

StatusWith<void*> SharedLibrary::getSymbol(StringData name) {
    // StringData is not assued to be null-terminated
    std::string symbolName = name.toString();

    void* function = GetProcAddress(static_cast<HMODULE>(_handle), symbolName.c_str());

    if (function == nullptr) {
        DWORD gle = GetLastError();
        if (gle != ERROR_PROC_NOT_FOUND) {
            return StatusWith<void*>(ErrorCodes::InternalError,
                                     str::stream() << "GetProcAddress failed for symbol: "
                                                   << errnoWithDescription());
        }
    }

    return StatusWith<void*>(function);
}

}  // namespace monger
