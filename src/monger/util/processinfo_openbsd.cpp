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

#include <cstdlib>
#include <string>

#include <kvm.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/param.h>
#include <sys/sysctl.h>
#include <sys/types.h>
#include <sys/user.h>
#include <sys/vmmeter.h>
#include <unistd.h>

#include "monger/util/log.h"
#include "monger/util/scopeguard.h"
#include "processinfo.h"

namespace monger {

ProcessInfo::ProcessInfo(ProcessId pid) : _pid(pid) {}

ProcessInfo::~ProcessInfo() {}

/**
 * Get a sysctl string value by name.  Use string specialization by default.
 */
template <typename T>
int getSysctlByIDWithDefault(const int* sysctlID,
                             const int idLen,
                             const T& defaultValue,
                             T* result);

template <>
int getSysctlByIDWithDefault<uintptr_t>(const int* sysctlID,
                                        const int idLen,
                                        const uintptr_t& defaultValue,
                                        uintptr_t* result) {
    uintptr_t value = 0;
    size_t len = sizeof(value);
    if (sysctl(sysctlID, idLen, &value, &len, NULL, 0) == -1) {
        *result = defaultValue;
        return errno;
    }
    if (len > sizeof(value)) {
        *result = defaultValue;
        return EINVAL;
    }

    *result = value;
    return 0;
}

template <>
int getSysctlByIDWithDefault<std::string>(const int* sysctlID,
                                          const int idLen,
                                          const std::string& defaultValue,
                                          string* result) {
    char value[256] = {0};
    size_t len = sizeof(value);
    if (sysctl(sysctlID, idLen, &value, &len, NULL, 0) == -1) {
        *result = defaultValue;
        return errno;
    }
    *result = value;
    return 0;
}

bool ProcessInfo::checkNumaEnabled() {
    return false;
}

int ProcessInfo::getVirtualMemorySize() {
    kvm_t* kd = NULL;
    int cnt = 0;
    char err[_POSIX2_LINE_MAX] = {0};
    if ((kd = kvm_openfiles(NULL, NULL, NULL, KVM_NO_FILES, err)) == NULL) {
        log() << "Unable to get virt mem size: " << err;
        return -1;
    }

    kinfo_proc* task = kvm_getprocs(kd, KERN_PROC_PID, _pid.toNative(), sizeof(kinfo_proc), &cnt);
    int vss = ((task->p_vm_dsize + task->p_vm_ssize + task->p_vm_tsize) * sysconf(_SC_PAGESIZE)) /
        1048576;
    kvm_close(kd);
    return vss;
}

int ProcessInfo::getResidentSize() {
    kvm_t* kd = NULL;
    int cnt = 0;
    char err[_POSIX2_LINE_MAX] = {0};
    if ((kd = kvm_openfiles(NULL, NULL, NULL, KVM_NO_FILES, err)) == NULL) {
        log() << "Unable to get res mem size: " << err;
        return -1;
    }
    kinfo_proc* task = kvm_getprocs(kd, KERN_PROC_PID, _pid.toNative(), sizeof(kinfo_proc), &cnt);
    int rss = (task->p_vm_rssize * sysconf(_SC_PAGESIZE)) / 1048576;  // convert from pages to MB
    kvm_close(kd);
    return rss;
}

double ProcessInfo::getSystemMemoryPressurePercentage() {
    return 0.0;
}

void ProcessInfo::SystemInfo::collectSystemInfo() {
    osType = "BSD";
    osName = "OpenBSD";
    int mib[2];

    mib[0] = CTL_KERN;
    mib[1] = KERN_VERSION;
    int status = getSysctlByIDWithDefault(mib, 2, std::string("unknown"), &osVersion);
    if (status != 0)
        log() << "Unable to collect OS Version. (errno: " << status << " msg: " << strerror(status)
              << ")";

    mib[0] = CTL_HW;
    mib[1] = HW_MACHINE;
    status = getSysctlByIDWithDefault(mib, 2, std::string("unknown"), &cpuArch);
    if (status != 0)
        log() << "Unable to collect Machine Architecture. (errno: " << status
              << " msg: " << strerror(status) << ")";
    addrSize = cpuArch.find("64") != std::string::npos ? 64 : 32;

    uintptr_t numBuffer;
    uintptr_t defaultNum = 1;
    mib[0] = CTL_HW;
    mib[1] = HW_PHYSMEM;
    status = getSysctlByIDWithDefault(mib, 2, defaultNum, &numBuffer);
    memSize = numBuffer;
    memLimit = memSize;
    if (status != 0)
        log() << "Unable to collect Physical Memory. (errno: " << status
              << " msg: " << strerror(status) << ")";

    mib[0] = CTL_HW;
    mib[1] = HW_NCPU;
    status = getSysctlByIDWithDefault(mib, 2, defaultNum, &numBuffer);
    numCores = numBuffer;
    if (status != 0)
        log() << "Unable to collect Number of CPUs. (errno: " << status
              << " msg: " << strerror(status) << ")";

    pageSize = static_cast<unsigned long long>(sysconf(_SC_PAGESIZE));

    hasNuma = checkNumaEnabled();
}

void ProcessInfo::getExtraInfo(BSONObjBuilder& info) {}

bool ProcessInfo::supported() {
    return true;
}

bool ProcessInfo::blockCheckSupported() {
    return true;
}

bool ProcessInfo::blockInMemory(const void* start) {
    char x = 0;
    if (mincore((void*)alignToStartOfPage(start), getPageSize(), &x)) {
        log() << "mincore failed: " << errnoWithDescription();
        return 1;
    }
    return x & 0x1;
}

bool ProcessInfo::pagesInMemory(const void* start, size_t numPages, std::vector<char>* out) {
    out->resize(numPages);
    // int mincore(const void *addr, size_t len, char *vec);
    if (mincore((void*)alignToStartOfPage(start), numPages * getPageSize(), &(out->front()))) {
        log() << "mincore failed: " << errnoWithDescription();
        return false;
    }
    for (size_t i = 0; i < numPages; ++i) {
        (*out)[i] = 0x1;
    }
    return true;
}

// get the number of CPUs available to the scheduler
boost::optional<unsigned long> ProcessInfo::getNumCoresForProcess() {
    long nprocs = sysconf(_SC_NPROCESSORS_ONLN);
    if (nprocs)
        return nprocs;
    return boost::none;
}
}
