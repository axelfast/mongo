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

#include "monger/db/s/transaction_coordinator_factory.h"
#include "monger/db/s/transaction_coordinator_service.h"
#include "monger/db/transaction_participant.h"
#include "monger/db/transaction_participant_gen.h"

namespace monger {

MONGO_REGISTER_SHIM(createTransactionCoordinator)
(OperationContext* opCtx, TxnNumber clientTxnNumber)->void {
    auto clientLsid = opCtx->getLogicalSessionId().get();
    auto clockSource = opCtx->getServiceContext()->getFastClockSource();

    // If this shard has been selected as the coordinator, set up the coordinator state
    // to be ready to receive votes.
    TransactionCoordinatorService::get(opCtx)->createCoordinator(
        opCtx,
        clientLsid,
        clientTxnNumber,
        clockSource->now() + Seconds(gTransactionLifetimeLimitSeconds.load()));
}

}  // namespace monger
