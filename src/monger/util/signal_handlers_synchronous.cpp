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

#include "monger/util/signal_handlers_synchronous.h"

#include <boost/exception/diagnostic_information.hpp>
#include <boost/exception/exception.hpp>
#include <csignal>
#include <exception>
#include <iostream>
#include <memory>
#include <streambuf>
#include <typeinfo>

#include "monger/base/string_data.h"
#include "monger/logger/log_domain.h"
#include "monger/logger/logger.h"
#include "monger/platform/compiler.h"
#include "monger/stdx/thread.h"
#include "monger/util/concurrency/thread_name.h"
#include "monger/util/debug_util.h"
#include "monger/util/debugger.h"
#include "monger/util/exception_filter_win32.h"
#include "monger/util/exit_code.h"
#include "monger/util/log.h"
#include "monger/util/quick_exit.h"
#include "monger/util/stacktrace.h"
#include "monger/util/text.h"

namespace monger {

namespace {

#if defined(_WIN32)
const char* strsignal(int signalNum) {
    // should only see SIGABRT on windows
    switch (signalNum) {
        case SIGABRT:
            return "SIGABRT";
        default:
            return "UNKNOWN";
    }
}

void endProcessWithSignal(int signalNum) {
    RaiseException(EXIT_ABRUPT, EXCEPTION_NONCONTINUABLE, 0, nullptr);
}

#else

void endProcessWithSignal(int signalNum) {
    // This works by restoring the system-default handler for the given signal and re-raising it, in
    // order to get the system default termination behavior (i.e., dumping core, or just exiting).
    struct sigaction defaultedSignals;
    memset(&defaultedSignals, 0, sizeof(defaultedSignals));
    defaultedSignals.sa_handler = SIG_DFL;
    sigemptyset(&defaultedSignals.sa_mask);
    invariant(sigaction(signalNum, &defaultedSignals, nullptr) == 0);
    raise(signalNum);
}

#endif

// This should only be used with MallocFreeOSteam
class MallocFreeStreambuf : public std::streambuf {
    MallocFreeStreambuf(const MallocFreeStreambuf&) = delete;
    MallocFreeStreambuf& operator=(const MallocFreeStreambuf&) = delete;

public:
    MallocFreeStreambuf() {
        setp(_buffer, _buffer + maxLogLineSize);
    }

    StringData str() const {
        return StringData(pbase(), pptr() - pbase());
    }
    void rewind() {
        setp(pbase(), epptr());
    }

private:
    static const size_t maxLogLineSize = 100 * 1000;
    char _buffer[maxLogLineSize];
};

class MallocFreeOStream : public std::ostream {
    MallocFreeOStream(const MallocFreeOStream&) = delete;
    MallocFreeOStream& operator=(const MallocFreeOStream&) = delete;

public:
    MallocFreeOStream() : std::ostream(&_buf) {}

    StringData str() const {
        return _buf.str();
    }
    void rewind() {
        _buf.rewind();
    }

private:
    MallocFreeStreambuf _buf;
};

MallocFreeOStream mallocFreeOStream;

/**
 * Instances of this type guard the mallocFreeOStream. While locking a mutex isn't guaranteed to
 * be signal-safe, this file does it anyway. The assumption is that the main safety risk to
 * locking a mutex is that you could deadlock with yourself. That risk is protected against by
 * only locking the mutex in fatal functions that log then exit. There is a remaining risk that
 * one of these functions recurses (possible if logging segfaults while handing a
 * segfault). This is currently acceptable because if things are that broken, there is little we
 * can do about it.
 *
 * If in the future, we decide to be more strict about posix signal safety, we could switch to
 * an atomic test-and-set loop, possibly with a mechanism for detecting signals raised while
 * handling other signals.
 */
class MallocFreeOStreamGuard {
public:
    explicit MallocFreeOStreamGuard() : _lk(_streamMutex, stdx::defer_lock) {
        if (terminateDepth++) {
            quickExit(EXIT_ABRUPT);
        }
        _lk.lock();
    }

private:
    static stdx::mutex _streamMutex;
    static thread_local int terminateDepth;
    stdx::unique_lock<stdx::mutex> _lk;
};

stdx::mutex MallocFreeOStreamGuard::_streamMutex;
thread_local int MallocFreeOStreamGuard::terminateDepth = 0;

// must hold MallocFreeOStreamGuard to call
void writeMallocFreeStreamToLog() {
    logger::globalLogDomain()
        ->append(logger::MessageEventEphemeral(Date_t::now(),
                                               logger::LogSeverity::Severe(),
                                               getThreadName(),
                                               mallocFreeOStream.str())
                     .setIsTruncatable(false))
        .transitional_ignore();
    mallocFreeOStream.rewind();
}

// must hold MallocFreeOStreamGuard to call
void printSignalAndBacktrace(int signalNum) {
    mallocFreeOStream << "Got signal: " << signalNum << " (" << strsignal(signalNum) << ").\n";
    printStackTrace(mallocFreeOStream);
    writeMallocFreeStreamToLog();
}

// this will be called in certain c++ error cases, for example if there are two active
// exceptions
void myTerminate() {
    MallocFreeOStreamGuard lk{};

    // In c++11 we can recover the current exception to print it.
    if (std::current_exception()) {
        mallocFreeOStream << "terminate() called. An exception is active;"
                          << " attempting to gather more information";
        writeMallocFreeStreamToLog();

        const std::type_info* typeInfo = nullptr;
        try {
            try {
                throw;
            } catch (const DBException& ex) {
                typeInfo = &typeid(ex);
                mallocFreeOStream << "DBException::toString(): " << redact(ex) << '\n';
            } catch (const std::exception& ex) {
                typeInfo = &typeid(ex);
                mallocFreeOStream << "std::exception::what(): " << redact(ex.what()) << '\n';
            } catch (const boost::exception& ex) {
                typeInfo = &typeid(ex);
                mallocFreeOStream << "boost::diagnostic_information(): "
                                  << boost::diagnostic_information(ex) << '\n';
            } catch (...) {
                mallocFreeOStream << "A non-standard exception type was thrown\n";
            }

            if (typeInfo) {
                const std::string name = demangleName(*typeInfo);
                mallocFreeOStream << "Actual exception type: " << name << '\n';
            }
        } catch (...) {
            mallocFreeOStream << "Exception while trying to print current exception.\n";
            if (typeInfo) {
                // It is possible that we failed during demangling. At least try to print the
                // mangled name.
                mallocFreeOStream << "Actual exception type: " << typeInfo->name() << '\n';
            }
        }
    } else {
        mallocFreeOStream << "terminate() called. No exception is active";
    }

    printStackTrace(mallocFreeOStream);
    writeMallocFreeStreamToLog();
    breakpoint();
    endProcessWithSignal(SIGABRT);
}

void abruptQuit(int signalNum) {
    MallocFreeOStreamGuard lk{};
    printSignalAndBacktrace(signalNum);
    breakpoint();
    endProcessWithSignal(signalNum);
}

#if defined(_WIN32)

void myInvalidParameterHandler(const wchar_t* expression,
                               const wchar_t* function,
                               const wchar_t* file,
                               unsigned int line,
                               uintptr_t pReserved) {
    severe() << "Invalid parameter detected in function " << toUtf8String(function)
             << " File: " << toUtf8String(file) << " Line: " << line;
    severe() << "Expression: " << toUtf8String(expression);
    severe() << "immediate exit due to invalid parameter";

    abruptQuit(SIGABRT);
}

void myPureCallHandler() {
    severe() << "Pure call handler invoked";
    severe() << "immediate exit due to invalid pure call";
    abruptQuit(SIGABRT);
}

#else

void abruptQuitWithAddrSignal(int signalNum, siginfo_t* siginfo, void* ucontext_erased) {
    // For convenient debugger access.
    MONGO_COMPILER_VARIABLE_UNUSED auto ucontext = static_cast<const ucontext_t*>(ucontext_erased);

    MallocFreeOStreamGuard lk{};

    const char* action = (signalNum == SIGSEGV || signalNum == SIGBUS) ? "access" : "operation";
    mallocFreeOStream << "Invalid " << action << " at address: " << siginfo->si_addr;

    // Writing out message to log separate from the stack trace so at least that much gets
    // logged. This is important because we may get here by jumping to an invalid address which
    // could cause unwinding the stack to break.
    writeMallocFreeStreamToLog();

    printSignalAndBacktrace(signalNum);
    breakpoint();
    endProcessWithSignal(signalNum);
}

#endif

}  // namespace

void setupSynchronousSignalHandlers() {
    std::set_terminate(myTerminate);
    std::set_new_handler(reportOutOfMemoryErrorAndExit);

#if defined(_WIN32)
    invariant(signal(SIGABRT, abruptQuit) != SIG_ERR);
    _set_purecall_handler(myPureCallHandler);
    _set_invalid_parameter_handler(myInvalidParameterHandler);
    setWindowsUnhandledExceptionFilter();
#else
    {
        struct sigaction ignoredSignals;
        memset(&ignoredSignals, 0, sizeof(ignoredSignals));
        ignoredSignals.sa_handler = SIG_IGN;
        sigemptyset(&ignoredSignals.sa_mask);

        invariant(sigaction(SIGHUP, &ignoredSignals, nullptr) == 0);
        invariant(sigaction(SIGUSR2, &ignoredSignals, nullptr) == 0);
        invariant(sigaction(SIGPIPE, &ignoredSignals, nullptr) == 0);
    }
    {
        struct sigaction plainSignals;
        memset(&plainSignals, 0, sizeof(plainSignals));
        plainSignals.sa_handler = abruptQuit;
        sigemptyset(&plainSignals.sa_mask);

        // ^\ is the stronger ^C. Log and quit hard without waiting for cleanup.
        invariant(sigaction(SIGQUIT, &plainSignals, nullptr) == 0);

        invariant(sigaction(SIGABRT, &plainSignals, nullptr) == 0);
    }
    {
        struct sigaction addrSignals;
        memset(&addrSignals, 0, sizeof(addrSignals));
        addrSignals.sa_sigaction = abruptQuitWithAddrSignal;
        sigemptyset(&addrSignals.sa_mask);
        addrSignals.sa_flags = SA_SIGINFO;

        invariant(sigaction(SIGSEGV, &addrSignals, nullptr) == 0);
        invariant(sigaction(SIGBUS, &addrSignals, nullptr) == 0);
        invariant(sigaction(SIGILL, &addrSignals, nullptr) == 0);
        invariant(sigaction(SIGFPE, &addrSignals, nullptr) == 0);
    }
    setupSIGTRAPforGDB();
#endif
}

void reportOutOfMemoryErrorAndExit() {
    MallocFreeOStreamGuard lk{};
    printStackTrace(mallocFreeOStream << "out of memory.\n");
    writeMallocFreeStreamToLog();
    quickExit(EXIT_ABRUPT);
}

void clearSignalMask() {
#ifndef _WIN32
    // We need to make sure that all signals are unmasked so signals are handled correctly
    sigset_t unblockSignalMask;
    invariant(sigemptyset(&unblockSignalMask) == 0);
    invariant(sigprocmask(SIG_SETMASK, &unblockSignalMask, nullptr) == 0);
#endif
}
}  // namespace monger
