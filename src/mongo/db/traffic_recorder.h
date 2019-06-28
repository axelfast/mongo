/**
 *    Copyright (C) 2018-present MongerDB, Inc.
 *
 *    This program is free software: you can redistribute it and/or modify
 *    it under the terms of the Server Side Public License, version 1,
 *    as published by MongerDB, Inc.
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

#pragma once

#include <boost/optional.hpp>

#include "monger/db/service_context.h"
#include "monger/db/traffic_recorder_gen.h"
#include "monger/platform/atomic_word.h"
#include "monger/rpc/message.h"
#include "monger/stdx/mutex.h"
#include "monger/transport/session.h"

namespace monger {

/**
 * A service context level global which captures packet capture through the transport layer if it is
 * enabled.  The service is intended to be turned on and off via startRecordingTrafficTraffic and
 * stopRecordingTrafficTraffic.
 *
 * The recording can have one recording running at a time and the intention is that observe() blocks
 * callers for the least amount of time possible.
 */
class TrafficRecorder {
public:
    static TrafficRecorder& get(ServiceContext* svc);

    TrafficRecorder();
    ~TrafficRecorder();

    // Start and stop block until the associate operation has succeeded or failed
    //
    // On failure these methods throw
    void start(const StartRecordingTraffic& options);
    void stop();

    void observe(const transport::SessionHandle& ts, Date_t now, const Message& message);

private:
    class TrafficRecorderSSS;
    class Recording;

    std::shared_ptr<Recording> _getCurrentRecording() const;

    AtomicWord<bool> _shouldRecord;

    // The mutex only protects the last recording shared_ptr
    mutable stdx::mutex _mutex;
    std::shared_ptr<Recording> _recording;
};

}  // namespace monger
