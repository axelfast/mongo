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

#include "monger/platform/strcasestr.h"

#if defined(__sun)
#include <dlfcn.h>

#include "monger/base/init.h"
#include "monger/base/status.h"
#endif

#if defined(_WIN32) || defined(__sun)

#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>

#if defined(__sun)
#define STRCASESTR_EMULATION_NAME strcasestr_emulation
#else
#define STRCASESTR_EMULATION_NAME strcasestr
#endif

namespace monger {
namespace pal {

/**
 * strcasestr -- case-insensitive search for a substring within another string.
 *
 * @param haystack      ptr to C-string to search
 * @param needle        ptr to C-string to try to find within 'haystack'
 * @return              ptr to start of 'needle' within 'haystack' if found, NULL otherwise
 */
const char* STRCASESTR_EMULATION_NAME(const char* haystack, const char* needle) {
    std::string haystackLower(haystack);
    std::transform(haystackLower.begin(), haystackLower.end(), haystackLower.begin(), ::tolower);

    std::string needleLower(needle);
    std::transform(needleLower.begin(), needleLower.end(), needleLower.begin(), ::tolower);

    // Use strstr() to find 'lowercased needle' in 'lowercased haystack'
    // If found, use the location to compute the matching location in the original string
    // If not found, return NULL
    const char* haystackLowerStart = haystackLower.c_str();
    const char* location = strstr(haystackLowerStart, needleLower.c_str());
    return location ? (haystack + (location - haystackLowerStart)) : nullptr;
}

#if defined(__sun)

typedef const char* (*StrCaseStrFunc)(const char* haystack, const char* needle);
static StrCaseStrFunc strcasestr_switcher = monger::pal::strcasestr_emulation;

const char* strcasestr(const char* haystack, const char* needle) {
    return strcasestr_switcher(haystack, needle);
}

#endif  // #if defined(__sun)

}  // namespace pal
}  // namespace monger

#endif  // #if defined(_WIN32) || defined(__sun)

#if defined(__sun)

namespace monger {

// 'strcasestr()' on Solaris will call the emulation if the symbol is not found
//
MONGO_INITIALIZER_GENERAL(SolarisStrCaseCmp, MONGO_NO_PREREQUISITES, ("default"))
(InitializerContext* context) {
    void* functionAddress = dlsym(RTLD_DEFAULT, "strcasestr");
    if (functionAddress != nullptr) {
        monger::pal::strcasestr_switcher =
            reinterpret_cast<monger::pal::StrCaseStrFunc>(functionAddress);
    }
    return Status::OK();
}

}  // namespace monger

#endif  // __sun
