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

#if defined(__sun)

#include "monger/platform/posix_fadvise.h"

#include <dlfcn.h>

#include "monger/base/init.h"
#include "monger/base/status.h"

namespace monger {
namespace pal {

int posix_fadvise_emulation(int fd, off_t offset, off_t len, int advice) {
    return 0;
}

typedef int (*PosixFadviseFunc)(int fd, off_t offset, off_t len, int advice);
static PosixFadviseFunc posix_fadvise_switcher = monger::pal::posix_fadvise_emulation;

int posix_fadvise(int fd, off_t offset, off_t len, int advice) {
    return posix_fadvise_switcher(fd, offset, len, advice);
}

}  // namespace pal

// 'posix_fadvise()' on Solaris will call the emulation if the symbol is not found
//
MONGO_INITIALIZER_GENERAL(SolarisPosixFadvise, MONGO_NO_PREREQUISITES, ("default"))
(InitializerContext* context) {
    void* functionAddress = dlsym(RTLD_DEFAULT, "posix_fadvise");
    if (functionAddress != nullptr) {
        monger::pal::posix_fadvise_switcher =
            reinterpret_cast<monger::pal::PosixFadviseFunc>(functionAddress);
    }
    return Status::OK();
}

}  // namespace monger

#endif  // #if defined(__sun)
