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

#include "monger/util/text.h"

#include <boost/integer_traits.hpp>
#include <errno.h>
#include <iostream>
#include <memory>
#include <sstream>

#ifdef _WIN32
#include <io.h>
#endif

#include "monger/platform/basic.h"
#include "monger/util/allocator.h"
#include "monger/util/str.h"

namespace monger {

// --- StringSplitter ----

/** get next split string fragment */
std::string StringSplitter::next() {
    const char* foo = strstr(_big, _splitter);
    if (foo) {
        std::string s(_big, foo - _big);
        _big = foo + strlen(_splitter);
        while (*_big && strstr(_big, _splitter) == _big)
            _big++;
        return s;
    }

    std::string s = _big;
    _big += strlen(_big);
    return s;
}


void StringSplitter::split(std::vector<std::string>& l) {
    while (more()) {
        l.push_back(next());
    }
}

std::vector<std::string> StringSplitter::split() {
    std::vector<std::string> l;
    split(l);
    return l;
}

std::string StringSplitter::join(const std::vector<std::string>& l, const std::string& split) {
    std::stringstream ss;
    for (unsigned i = 0; i < l.size(); i++) {
        if (i > 0)
            ss << split;
        ss << l[i];
    }
    return ss.str();
}

std::vector<std::string> StringSplitter::split(const std::string& big,
                                               const std::string& splitter) {
    StringSplitter ss(big.c_str(), splitter.c_str());
    return ss.split();
}


// --- utf8 utils ------

inline int leadingOnes(unsigned char c) {
    if (c < 0x80)
        return 0;
    static const char _leadingOnes[128] = {
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // 0x80 - 0x8F
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // 0x90 - 0x99
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // 0xA0 - 0xA9
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // 0xB0 - 0xB9
        2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,  // 0xC0 - 0xC9
        2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,  // 0xD0 - 0xD9
        3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,  // 0xE0 - 0xE9
        4, 4, 4, 4, 4, 4, 4, 4,                          // 0xF0 - 0xF7
        5, 5, 5, 5,                                      // 0xF8 - 0xFB
        6, 6,                                            // 0xFC - 0xFD
        7,                                               // 0xFE
        8,                                               // 0xFF
    };
    return _leadingOnes[c & 0x7f];
}

bool isValidUTF8(StringData s) {
    int left = 0;  // how many bytes are left in the current codepoint
    for (unsigned char c : s) {
        const int ones = leadingOnes(c);
        if (left) {
            if (ones != 1)
                return false;  // should be a continuation byte
            left--;
        } else {
            if (ones == 0)
                continue;  // ASCII byte
            if (ones == 1)
                return false;  // unexpected continuation byte
            if (c > 0xF4)
                return false;  // codepoint too large (< 0x10FFFF)
            if (c == 0xC0 || c == 0xC1)
                return false;  // codepoints <= 0x7F shouldn't be 2 bytes

            // still valid
            left = ones - 1;
        }
    }
    if (left != 0)
        return false;  // string ended mid-codepoint
    return true;
}

#if defined(_WIN32)

std::string toUtf8String(const std::wstring& wide) {
    if (wide.size() > boost::integer_traits<int>::const_max)
        throw std::length_error("Wide string cannot be more than INT_MAX characters long.");
    if (wide.size() == 0)
        return "";

    // Calculate necessary buffer size
    int len = ::WideCharToMultiByte(
        CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);

    // Perform actual conversion
    if (len > 0) {
        std::vector<char> buffer(len);
        len = ::WideCharToMultiByte(CP_UTF8,
                                    0,
                                    wide.c_str(),
                                    static_cast<int>(wide.size()),
                                    &buffer[0],
                                    static_cast<int>(buffer.size()),
                                    nullptr,
                                    nullptr);
        if (len > 0) {
            verify(len == static_cast<int>(buffer.size()));
            return std::string(&buffer[0], buffer.size());
        }
    }

    msgasserted(16091, str::stream() << "can't wstring to utf8: " << ::GetLastError());
    return "";
}

std::wstring toWideString(const char* utf8String) {
    int bufferSize = MultiByteToWideChar(CP_UTF8,     // Code page
                                         0,           // Flags
                                         utf8String,  // Input string
                                         -1,          // Count, -1 for NUL-terminated
                                         nullptr,     // No output buffer
                                         0            // Zero means "compute required size"
                                         );
    if (bufferSize == 0) {
        return std::wstring();
    }
    std::unique_ptr<wchar_t[]> tempBuffer(new wchar_t[bufferSize]);
    tempBuffer[0] = 0;
    MultiByteToWideChar(CP_UTF8,           // Code page
                        0,                 // Flags
                        utf8String,        // Input string
                        -1,                // Count, -1 for NUL-terminated
                        tempBuffer.get(),  // UTF-16 output buffer
                        bufferSize         // Buffer size in wide characters
                        );
    return std::wstring(tempBuffer.get());
}

/**
 * Write a UTF-8 string to the Windows console in Unicode (UTF-16)
 *
 * @param utf8String        UTF-8 input string
 * @param utf8StringSize    Number of bytes in UTF-8 string, no NUL terminator assumed
 * @return                  true if all characters were displayed (including zero characters)
 */
bool writeUtf8ToWindowsConsole(const char* utf8String, unsigned int utf8StringSize) {
    int bufferSize = MultiByteToWideChar(CP_UTF8,         // Code page
                                         0,               // Flags
                                         utf8String,      // Input string
                                         utf8StringSize,  // Input string length
                                         nullptr,         // No output buffer
                                         0                // Zero means "compute required size"
                                         );
    if (bufferSize == 0) {
        return true;
    }
    std::unique_ptr<wchar_t[]> utf16String(new wchar_t[bufferSize]);
    MultiByteToWideChar(CP_UTF8,            // Code page
                        0,                  // Flags
                        utf8String,         // Input string
                        utf8StringSize,     // Input string length
                        utf16String.get(),  // UTF-16 output buffer
                        bufferSize          // Buffer size in wide characters
                        );
    const wchar_t* utf16Pointer = utf16String.get();
    size_t numberOfCharactersToWrite = bufferSize;
    HANDLE consoleHandle = GetStdHandle(STD_OUTPUT_HANDLE);
    while (numberOfCharactersToWrite > 0) {
        static const DWORD MAXIMUM_CHARACTERS_PER_PASS = 8 * 1024;
        DWORD numberOfCharactersThisPass = static_cast<DWORD>(numberOfCharactersToWrite);
        if (numberOfCharactersThisPass > MAXIMUM_CHARACTERS_PER_PASS) {
            numberOfCharactersThisPass = MAXIMUM_CHARACTERS_PER_PASS;
        }
        DWORD numberOfCharactersWritten;
        BOOL success = WriteConsoleW(consoleHandle,
                                     utf16Pointer,
                                     numberOfCharactersThisPass,
                                     &numberOfCharactersWritten,
                                     nullptr);
        if (0 == success) {
            DWORD dosError = GetLastError();
            static bool errorMessageShown = false;
            if (ERROR_GEN_FAILURE == dosError) {
                if (!errorMessageShown) {
                    std::cout << "\n---\nUnicode text could not be correctly displayed.\n"
                                 "Please change your console font to a Unicode font "
                                 "(e.g. Lucida Console).\n---\n"
                              << std::endl;
                    errorMessageShown = true;
                }
                // we can't display the text properly using a raster font,
                // but we can display the bits that will display ...
                _write(1, utf8String, utf8StringSize);
            }
            return false;
        }
        numberOfCharactersToWrite -= numberOfCharactersWritten;
        utf16Pointer += numberOfCharactersWritten;
    }
    return true;
}

WindowsCommandLine::WindowsCommandLine(int argc, wchar_t* argvW[], wchar_t* envpW[])
    : _argv(nullptr), _envp(nullptr) {
    // Construct UTF-8 copy of arguments
    std::vector<std::string> utf8args;
    std::vector<size_t> utf8argLength;
    size_t blockSize = argc * sizeof(char*);
    size_t blockPtr = blockSize;
    for (int i = 0; i < argc; ++i) {
        utf8args.push_back(toUtf8String(argvW[i]));
        size_t argLength = utf8args[i].length() + 1;
        utf8argLength.push_back(argLength);
        blockSize += argLength;
    }
    _argv = static_cast<char**>(mongerMalloc(blockSize));
    for (int i = 0; i < argc; ++i) {
        _argv[i] = reinterpret_cast<char*>(_argv) + blockPtr;
        strcpy_s(_argv[i], utf8argLength[i], utf8args[i].c_str());
        blockPtr += utf8argLength[i];
    }

    // Construct UTF-8 copy of environment strings
    size_t envCount = 0;
    wchar_t** envpWptr = &envpW[0];
    while (*envpWptr++) {
        ++envCount;
    }
    std::vector<std::string> utf8envs;
    std::vector<size_t> utf8envLength;
    blockSize = (envCount + 1) * sizeof(char*);
    blockPtr = blockSize;
    for (size_t i = 0; i < envCount; ++i) {
        utf8envs.push_back(toUtf8String(envpW[i]));
        size_t envLength = utf8envs[i].length() + 1;
        utf8envLength.push_back(envLength);
        blockSize += envLength;
    }
    _envp = static_cast<char**>(mongerMalloc(blockSize));
    size_t i;
    for (i = 0; i < envCount; ++i) {
        _envp[i] = reinterpret_cast<char*>(_envp) + blockPtr;
        strcpy_s(_envp[i], utf8envLength[i], utf8envs[i].c_str());
        blockPtr += utf8envLength[i];
    }
    _envp[i] = nullptr;
}

WindowsCommandLine::~WindowsCommandLine() {
    free(_argv);
    free(_envp);
}

#endif  // #if defined(_WIN32)

// See "Parsing C++ Command-Line Arguments (C++)"
// http://msdn.microsoft.com/en-us/library/windows/desktop/17w5ykft(v=vs.85).aspx
static void quoteForWindowsCommandLine(const std::string& arg, std::ostream& os) {
    if (arg.empty()) {
        os << "\"\"";
    } else if (arg.find_first_of(" \t\"") == std::string::npos) {
        os << arg;
    } else {
        os << '"';
        std::string backslashes = "";
        for (std::string::const_iterator iter = arg.begin(), end = arg.end(); iter != end; ++iter) {
            switch (*iter) {
                case '\\':
                    backslashes.push_back(*iter);
                    if (iter + 1 == end)
                        os << backslashes << backslashes;
                    break;
                case '"':
                    os << backslashes << backslashes << "\\\"";
                    break;
                default:
                    os << backslashes << *iter;
                    backslashes.clear();
                    break;
            }
        }
        os << '"';
    }
}

std::string constructUtf8WindowsCommandLine(const std::vector<std::string>& argv) {
    if (argv.empty())
        return "";

    std::ostringstream commandLine;
    auto iter = argv.begin();
    const auto end = argv.end();
    quoteForWindowsCommandLine(*iter, commandLine);
    ++iter;
    for (; iter != end; ++iter) {
        commandLine << ' ';
        quoteForWindowsCommandLine(*iter, commandLine);
    }
    return commandLine.str();
}
}  // namespace monger
