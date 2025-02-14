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

#include <functional>

#include "monger/stdx/thread.h"
#include "monger/unittest/unittest.h"
#include "monger/util/concurrency/spin_lock.h"
#include "monger/util/timer.h"

namespace {

using monger::SpinLock;
using monger::Timer;

namespace stdx = monger::stdx;

class LockTester {
public:
    LockTester(SpinLock* spin, int* counter) : _spin(spin), _counter(counter), _requests(0) {}

    ~LockTester() {
        delete _t;
    }

    void start(int increments) {
        _t = new stdx::thread([this, increments] { test(increments); });
    }

    void join() {
        if (_t)
            _t->join();
    }

    int requests() const {
        return _requests;
    }

private:
    SpinLock* _spin;  // not owned here
    int* _counter;    // not owned here
    int _requests;
    stdx::thread* _t;

    void test(int increments) {
        while (increments-- > 0) {
            _spin->lock();
            ++(*_counter);
            ++_requests;
            _spin->unlock();
        }
    }

    LockTester(LockTester&);
    LockTester& operator=(LockTester&);
};


TEST(Concurrency, ConcurrentIncs) {
    SpinLock spin;
    int counter = 0;

    const int threads = 64;
    const int incs = 50000;
    LockTester* testers[threads];

    Timer timer;

    for (int i = 0; i < threads; i++) {
        testers[i] = new LockTester(&spin, &counter);
    }
    for (int i = 0; i < threads; i++) {
        testers[i]->start(incs);
    }
    for (int i = 0; i < threads; i++) {
        testers[i]->join();
        ASSERT_EQUALS(testers[i]->requests(), incs);
        delete testers[i];
    }

    int ms = timer.millis();
    monger::unittest::log() << "spinlock ConcurrentIncs time: " << ms << std::endl;

    ASSERT_EQUALS(counter, threads * incs);
}

}  // namespace
