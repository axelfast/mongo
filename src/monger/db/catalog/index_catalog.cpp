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

#define MONGO_LOG_DEFAULT_COMPONENT ::monger::logger::LogComponent::kIndex

#include "monger/platform/basic.h"

#include "monger/db/catalog/index_catalog.h"
#include "monger/db/index/index_descriptor.h"


namespace monger {
using IndexIterator = IndexCatalog::IndexIterator;
using ReadyIndexesIterator = IndexCatalog::ReadyIndexesIterator;
using AllIndexesIterator = IndexCatalog::AllIndexesIterator;

bool IndexIterator::more() {
    if (_start) {
        _next = _advance();
        _start = false;
    }
    return _next != nullptr;
}

const IndexCatalogEntry* IndexIterator::next() {
    if (!more())
        return nullptr;
    _prev = _next;
    _next = _advance();
    return _prev;
}

ReadyIndexesIterator::ReadyIndexesIterator(OperationContext* const opCtx,
                                           IndexCatalogEntryContainer::const_iterator beginIterator,
                                           IndexCatalogEntryContainer::const_iterator endIterator)
    : _opCtx(opCtx), _iterator(beginIterator), _endIterator(endIterator) {}

const IndexCatalogEntry* ReadyIndexesIterator::_advance() {
    while (_iterator != _endIterator) {
        IndexCatalogEntry* entry = _iterator->get();
        ++_iterator;

        if (auto minSnapshot = entry->getMinimumVisibleSnapshot()) {
            if (auto mySnapshot = _opCtx->recoveryUnit()->getPointInTimeReadTimestamp()) {
                if (mySnapshot < minSnapshot) {
                    // This index isn't finished in my snapshot.
                    continue;
                }
            }
        }

        return entry;
    }

    return nullptr;
}

AllIndexesIterator::AllIndexesIterator(
    OperationContext* const opCtx, std::unique_ptr<std::vector<IndexCatalogEntry*>> ownedContainer)
    : _opCtx(opCtx), _ownedContainer(std::move(ownedContainer)) {
    // Explicitly order calls onto the ownedContainer with respect to its move.
    _iterator = _ownedContainer->begin();
    _endIterator = _ownedContainer->end();
}

const IndexCatalogEntry* AllIndexesIterator::_advance() {
    if (_iterator == _endIterator) {
        return nullptr;
    }

    IndexCatalogEntry* entry = *_iterator;
    ++_iterator;
    return entry;
}
}  // namespace monger
