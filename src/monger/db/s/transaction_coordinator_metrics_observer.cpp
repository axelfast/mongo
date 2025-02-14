/**
 *    Copyright (C) 2019-present MongoDB, Inc.
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

#include "monger/db/s/transaction_coordinator_metrics_observer.h"

namespace monger {

using CommitDecision = txn::CommitDecision;

void TransactionCoordinatorMetricsObserver::onCreate(
    ServerTransactionCoordinatorsMetrics* serverTransactionCoordinatorsMetrics,
    TickSource* tickSource,
    Date_t curWallClockTime) {

    //
    // Per transaction coordinator stats.
    //
    _singleTransactionCoordinatorStats.setCreateTime(tickSource->getTicks(), curWallClockTime);

    //
    // Server wide transaction coordinators metrics.
    //
    serverTransactionCoordinatorsMetrics->incrementTotalCreated();
}

void TransactionCoordinatorMetricsObserver::onStartWritingParticipantList(
    ServerTransactionCoordinatorsMetrics* serverTransactionCoordinatorsMetrics,
    TickSource* tickSource,
    Date_t curWallClockTime) {

    //
    // Per transaction coordinator stats.
    //
    _singleTransactionCoordinatorStats.setWritingParticipantListStartTime(tickSource->getTicks(),
                                                                          curWallClockTime);

    //
    // Server wide transaction coordinators metrics.
    //

    serverTransactionCoordinatorsMetrics->incrementTotalStartedTwoPhaseCommit();
    serverTransactionCoordinatorsMetrics->incrementCurrentWritingParticipantList();
}

void TransactionCoordinatorMetricsObserver::onStartWaitingForVotes(
    ServerTransactionCoordinatorsMetrics* serverTransactionCoordinatorsMetrics,
    TickSource* tickSource,
    Date_t curWallClockTime) {

    //
    // Per transaction coordinator stats.
    //
    _singleTransactionCoordinatorStats.setWaitingForVotesStartTime(tickSource->getTicks(),
                                                                   curWallClockTime);

    //
    // Server wide transaction coordinators metrics.
    //
    serverTransactionCoordinatorsMetrics->decrementCurrentWritingParticipantList();
    serverTransactionCoordinatorsMetrics->incrementCurrentWaitingForVotes();
}

void TransactionCoordinatorMetricsObserver::onStartWritingDecision(
    ServerTransactionCoordinatorsMetrics* serverTransactionCoordinatorsMetrics,
    TickSource* tickSource,
    Date_t curWallClockTime) {

    //
    // Per transaction coordinator stats.
    //
    _singleTransactionCoordinatorStats.setWritingDecisionStartTime(tickSource->getTicks(),
                                                                   curWallClockTime);

    //
    // Server wide transaction coordinators metrics.
    //
    serverTransactionCoordinatorsMetrics->decrementCurrentWaitingForVotes();
    serverTransactionCoordinatorsMetrics->incrementCurrentWritingDecision();
}

void TransactionCoordinatorMetricsObserver::onStartWaitingForDecisionAcks(
    ServerTransactionCoordinatorsMetrics* serverTransactionCoordinatorsMetrics,
    TickSource* tickSource,
    Date_t curWallClockTime) {

    //
    // Per transaction coordinator stats.
    //
    _singleTransactionCoordinatorStats.setWaitingForDecisionAcksStartTime(tickSource->getTicks(),
                                                                          curWallClockTime);

    //
    // Server wide transaction coordinators metrics.
    //
    serverTransactionCoordinatorsMetrics->decrementCurrentWritingDecision();
    serverTransactionCoordinatorsMetrics->incrementCurrentWaitingForDecisionAcks();
}

void TransactionCoordinatorMetricsObserver::onStartDeletingCoordinatorDoc(
    ServerTransactionCoordinatorsMetrics* serverTransactionCoordinatorsMetrics,
    TickSource* tickSource,
    Date_t curWallClockTime) {

    //
    // Per transaction coordinator stats.
    //
    _singleTransactionCoordinatorStats.setDeletingCoordinatorDocStartTime(tickSource->getTicks(),
                                                                          curWallClockTime);

    //
    // Server wide transaction coordinators metrics.
    //
    serverTransactionCoordinatorsMetrics->decrementCurrentWaitingForDecisionAcks();
    serverTransactionCoordinatorsMetrics->incrementCurrentDeletingCoordinatorDoc();
}

void TransactionCoordinatorMetricsObserver::onEnd(
    ServerTransactionCoordinatorsMetrics* serverTransactionCoordinatorsMetrics,
    TickSource* tickSource,
    Date_t curWallClockTime,
    TransactionCoordinator::Step step,
    const boost::optional<txn::CoordinatorCommitDecision>& decision) {

    //
    // Per transaction coordinator stats.
    //
    _singleTransactionCoordinatorStats.setEndTime(tickSource->getTicks(), curWallClockTime);

    //
    // Server wide transaction coordinators metrics.
    //
    if (decision) {
        switch (decision->getDecision()) {
            case CommitDecision::kCommit:
                serverTransactionCoordinatorsMetrics->incrementTotalSuccessfulTwoPhaseCommit();
                break;
            case CommitDecision::kAbort:
                serverTransactionCoordinatorsMetrics->incrementTotalAbortedTwoPhaseCommit();
                break;
        }
    }

    _decrementLastStep(serverTransactionCoordinatorsMetrics, step);
}

void TransactionCoordinatorMetricsObserver::_decrementLastStep(
    ServerTransactionCoordinatorsMetrics* serverTransactionCoordinatorsMetrics,
    TransactionCoordinator::Step step) {
    switch (step) {
        case TransactionCoordinator::Step::kInactive:
            break;
        case TransactionCoordinator::Step::kWritingParticipantList:
            serverTransactionCoordinatorsMetrics->decrementCurrentWritingParticipantList();
            break;
        case TransactionCoordinator::Step::kWaitingForVotes:
            serverTransactionCoordinatorsMetrics->decrementCurrentWaitingForVotes();
            break;
        case TransactionCoordinator::Step::kWritingDecision:
            serverTransactionCoordinatorsMetrics->decrementCurrentWritingDecision();
            break;
        case TransactionCoordinator::Step::kWaitingForDecisionAcks:
            serverTransactionCoordinatorsMetrics->decrementCurrentWaitingForDecisionAcks();
            break;
        case TransactionCoordinator::Step::kDeletingCoordinatorDoc:
            serverTransactionCoordinatorsMetrics->decrementCurrentDeletingCoordinatorDoc();
            break;
    }
}

}  // namespace monger
