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

#include "monger/db/hasher.h"
#include "monger/db/pipeline/aggregation_context_fixture.h"
#include "monger/db/pipeline/document_source_exchange.h"
#include "monger/db/pipeline/document_source_mock.h"
#include "monger/db/storage/key_string.h"
#include "monger/executor/network_interface_factory.h"
#include "monger/executor/thread_pool_task_executor.h"
#include "monger/platform/random.h"
#include "monger/unittest/temp_dir.h"
#include "monger/unittest/unittest.h"
#include "monger/util/concurrency/thread_pool.h"
#include "monger/util/time_support.h"


namespace monger {

/**
 * An implementation of the MongerProcessInterface that is okay with changing the OperationContext,
 * but has no other parts of the interface implemented.
 */
class StubMongerProcessOkWithOpCtxChanges : public StubMongerProcessInterface {
public:
    void setOperationContext(OperationContext* opCtx) final {
        return;
    }
};

class DocumentSourceExchangeTest : public AggregationContextFixture {
protected:
    std::unique_ptr<executor::TaskExecutor> _executor;
    virtual void setUp() override {
        getExpCtx()->mongerProcessInterface = std::make_shared<StubMongerProcessOkWithOpCtxChanges>();

        auto net = executor::makeNetworkInterface("ExchangeTest");

        ThreadPool::Options options;
        auto pool = std::make_unique<ThreadPool>(options);

        _executor =
            std::make_unique<executor::ThreadPoolTaskExecutor>(std::move(pool), std::move(net));
        _executor->startup();
    }

    virtual void tearDown() override {
        _executor->shutdown();
        _executor.reset();
    }

    auto getMockSource(int cnt) {
        auto source = DocumentSourceMock::createForTest();

        for (int i = 0; i < cnt; ++i)
            source->emplace_back(Document{{"a", i}, {"b", "aaaaaaaaaaaaaaaaaaaaaaaaaaa"_sd}});

        return source;
    }

    static auto getNewSeed() {
        auto seed = Date_t::now().asInt64();
        unittest::log() << "Generated new seed is " << seed;

        return seed;
    }

    auto getRandomMockSource(size_t cnt, int64_t seed) {
        PseudoRandom prng(seed);

        auto source = DocumentSourceMock::createForTest();

        for (size_t i = 0; i < cnt; ++i)
            source->emplace_back(Document{{"a", static_cast<int>(prng.nextInt32() % cnt)},
                                          {"b", "aaaaaaaaaaaaaaaaaaaaaaaaaaa"_sd}});

        return source;
    }

    auto parseSpec(const BSONObj& spec) {
        IDLParserErrorContext ctx("internalExchange");
        return ExchangeSpec::parse(ctx, spec);
    }
};

TEST_F(DocumentSourceExchangeTest, SimpleExchange1Consumer) {
    const size_t nDocs = 500;

    auto source = getMockSource(nDocs);

    ExchangeSpec spec;
    spec.setPolicy(ExchangePolicyEnum::kRoundRobin);
    spec.setConsumers(1);
    spec.setBufferSize(1024);

    boost::intrusive_ptr<Exchange> ex =
        new Exchange(spec, unittest::assertGet(Pipeline::create({source}, getExpCtx())));

    auto input = ex->getNext(getExpCtx()->opCtx, 0, nullptr);

    size_t docs = 0;
    for (; input.isAdvanced(); input = ex->getNext(getExpCtx()->opCtx, 0, nullptr)) {
        ++docs;
    }

    ASSERT_EQ(docs, nDocs);
}

TEST_F(DocumentSourceExchangeTest, SimpleExchangeNConsumer) {
    const size_t nDocs = 500;
    auto source = getMockSource(500);

    const size_t nConsumers = 5;

    ASSERT_EQ(nDocs % nConsumers, 0u);

    ExchangeSpec spec;
    spec.setPolicy(ExchangePolicyEnum::kRoundRobin);
    spec.setConsumers(nConsumers);
    spec.setBufferSize(1024);

    boost::intrusive_ptr<Exchange> ex =
        new Exchange(spec, unittest::assertGet(Pipeline::create({source}, getExpCtx())));

    std::vector<boost::intrusive_ptr<DocumentSourceExchange>> prods;

    for (size_t idx = 0; idx < nConsumers; ++idx) {
        prods.push_back(new DocumentSourceExchange(getExpCtx(), ex, idx, nullptr));
    }

    std::vector<executor::TaskExecutor::CallbackHandle> handles;

    for (size_t id = 0; id < nConsumers; ++id) {
        auto handle = _executor->scheduleWork(
            [prods, id, nDocs, nConsumers](const executor::TaskExecutor::CallbackArgs& cb) {
                PseudoRandom prng(getNewSeed());

                auto input = prods[id]->getNext();

                size_t docs = 0;

                for (; input.isAdvanced(); input = prods[id]->getNext()) {
                    sleepmillis(prng.nextInt32() % 20 + 1);
                    ++docs;
                }
                ASSERT_EQ(docs, nDocs / nConsumers);
            });

        handles.emplace_back(std::move(handle.getValue()));
    }

    for (auto& h : handles)
        _executor->wait(h);
}

TEST_F(DocumentSourceExchangeTest, ExchangeNConsumerEarlyout) {
    const size_t nDocs = 500;
    auto source = getMockSource(500);

    const size_t nConsumers = 2;

    ASSERT_EQ(nDocs % nConsumers, 0u);

    ExchangeSpec spec;
    spec.setPolicy(ExchangePolicyEnum::kRoundRobin);
    spec.setConsumers(nConsumers);
    spec.setBufferSize(1024);

    boost::intrusive_ptr<Exchange> ex =
        new Exchange(spec, unittest::assertGet(Pipeline::create({source}, getExpCtx())));

    std::vector<boost::intrusive_ptr<DocumentSourceExchange>> prods;

    for (size_t idx = 0; idx < nConsumers; ++idx) {
        prods.push_back(new DocumentSourceExchange(getExpCtx(), ex, idx, nullptr));
    }

    std::vector<executor::TaskExecutor::CallbackHandle> handles;

    for (size_t id = 0; id < nConsumers; ++id) {
        auto handle = _executor->scheduleWork(
            [prods, id, nDocs, nConsumers](const executor::TaskExecutor::CallbackArgs& cb) {
                PseudoRandom prng(getNewSeed());

                auto input = prods[id]->getNext();

                size_t docs = 0;

                for (; input.isAdvanced(); input = prods[id]->getNext()) {
                    sleepmillis(prng.nextInt32() % 20 + 1);
                    ++docs;

                    // The consumer 1 bails out early wihout consuming all its documents.
                    if (id == 1 && docs == 100) {
                        // Pretend we have seen all docs.
                        docs = nDocs / nConsumers;

                        prods[id]->dispose();
                        break;
                    }
                }
                ASSERT_EQ(docs, nDocs / nConsumers);
            });

        handles.emplace_back(std::move(handle.getValue()));
    }

    for (auto& h : handles)
        _executor->wait(h);
}

TEST_F(DocumentSourceExchangeTest, BroadcastExchangeNConsumer) {
    const size_t nDocs = 500;
    auto source = getMockSource(nDocs);

    const size_t nConsumers = 5;

    ExchangeSpec spec;
    spec.setPolicy(ExchangePolicyEnum::kBroadcast);
    spec.setConsumers(nConsumers);
    spec.setBufferSize(1024);

    boost::intrusive_ptr<Exchange> ex =
        new Exchange(spec, unittest::assertGet(Pipeline::create({source}, getExpCtx())));

    std::vector<boost::intrusive_ptr<DocumentSourceExchange>> prods;

    for (size_t idx = 0; idx < nConsumers; ++idx) {
        prods.push_back(new DocumentSourceExchange(getExpCtx(), ex, idx, nullptr));
    }

    std::vector<executor::TaskExecutor::CallbackHandle> handles;

    for (size_t id = 0; id < nConsumers; ++id) {
        auto handle = _executor->scheduleWork(
            [prods, id, nDocs](const executor::TaskExecutor::CallbackArgs& cb) {
                size_t docs = 0;
                for (auto input = prods[id]->getNext(); input.isAdvanced();
                     input = prods[id]->getNext()) {
                    ++docs;
                }
                ASSERT_EQ(docs, nDocs);
            });

        handles.emplace_back(std::move(handle.getValue()));
    }

    for (auto& h : handles)
        _executor->wait(h);
}

TEST_F(DocumentSourceExchangeTest, RangeExchangeNConsumer) {
    const size_t nDocs = 500;
    auto source = getMockSource(nDocs);

    const std::vector<BSONObj> boundaries = {BSON("a" << MINKEY),
                                             BSON("a" << 100),
                                             BSON("a" << 200),
                                             BSON("a" << 300),
                                             BSON("a" << 400),
                                             BSON("a" << MAXKEY)};

    const size_t nConsumers = boundaries.size() - 1;

    ASSERT(nDocs % nConsumers == 0);

    ExchangeSpec spec;
    spec.setPolicy(ExchangePolicyEnum::kKeyRange);
    spec.setKey(BSON("a" << 1));
    spec.setBoundaries(boundaries);
    spec.setConsumers(nConsumers);
    spec.setBufferSize(1024);

    boost::intrusive_ptr<Exchange> ex =
        new Exchange(std::move(spec), unittest::assertGet(Pipeline::create({source}, getExpCtx())));

    std::vector<boost::intrusive_ptr<DocumentSourceExchange>> prods;

    for (size_t idx = 0; idx < nConsumers; ++idx) {
        prods.push_back(new DocumentSourceExchange(getExpCtx(), ex, idx, nullptr));
    }

    std::vector<executor::TaskExecutor::CallbackHandle> handles;

    for (size_t id = 0; id < nConsumers; ++id) {
        auto handle = _executor->scheduleWork(
            [prods, id, nDocs, nConsumers](const executor::TaskExecutor::CallbackArgs& cb) {
                size_t docs = 0;
                for (auto input = prods[id]->getNext(); input.isAdvanced();
                     input = prods[id]->getNext()) {
                    size_t value = input.getDocument()["a"].getInt();

                    ASSERT(value >= id * 100);
                    ASSERT(value < (id + 1) * 100);

                    ++docs;
                }

                ASSERT_EQ(docs, nDocs / nConsumers);
            });

        handles.emplace_back(std::move(handle.getValue()));
    }

    for (auto& h : handles)
        _executor->wait(h);
}

TEST_F(DocumentSourceExchangeTest, RangeShardingExchangeNConsumer) {
    const size_t nDocs = 500;
    auto source = getMockSource(nDocs);

    const std::vector<BSONObj> boundaries = {
        BSON("a" << MINKEY),
        BSON("a" << 50),
        BSON("a" << 100),
        BSON("a" << 150),
        BSON("a" << 200),
        BSON("a" << 250),
        BSON("a" << 300),
        BSON("a" << 350),
        BSON("a" << 400),
        BSON("a" << 450),
        BSON("a" << MAXKEY),
    };
    std::vector<int> consumerIds({0, 0, 1, 1, 2, 2, 3, 3, 4, 4});

    const size_t nConsumers = consumerIds.size() / 2;

    ASSERT(nDocs % nConsumers == 0);

    ExchangeSpec spec;
    spec.setPolicy(ExchangePolicyEnum::kKeyRange);
    spec.setKey(BSON("a" << 1));
    spec.setBoundaries(boundaries);
    spec.setConsumerIds(consumerIds);
    spec.setConsumers(nConsumers);
    spec.setBufferSize(1024);

    boost::intrusive_ptr<Exchange> ex =
        new Exchange(std::move(spec), unittest::assertGet(Pipeline::create({source}, getExpCtx())));

    std::vector<boost::intrusive_ptr<DocumentSourceExchange>> prods;

    for (size_t idx = 0; idx < nConsumers; ++idx) {
        prods.push_back(new DocumentSourceExchange(getExpCtx(), ex, idx, nullptr));
    }

    std::vector<executor::TaskExecutor::CallbackHandle> handles;

    for (size_t id = 0; id < nConsumers; ++id) {
        auto handle = _executor->scheduleWork(
            [prods, id, nDocs, nConsumers](const executor::TaskExecutor::CallbackArgs& cb) {
                size_t docs = 0;
                for (auto input = prods[id]->getNext(); input.isAdvanced();
                     input = prods[id]->getNext()) {
                    size_t value = input.getDocument()["a"].getInt();

                    ASSERT(value >= id * 100);
                    ASSERT(value < (id + 1) * 100);

                    ++docs;
                }

                ASSERT_EQ(docs, nDocs / nConsumers);
            });

        handles.emplace_back(std::move(handle.getValue()));
    }

    for (auto& h : handles)
        _executor->wait(h);
}

TEST_F(DocumentSourceExchangeTest, RangeRandomExchangeNConsumer) {
    const size_t nDocs = 500;
    auto source = getRandomMockSource(nDocs, getNewSeed());

    const std::vector<BSONObj> boundaries = {BSON("a" << MINKEY),
                                             BSON("a" << 100),
                                             BSON("a" << 200),
                                             BSON("a" << 300),
                                             BSON("a" << 400),
                                             BSON("a" << MAXKEY)};

    const size_t nConsumers = boundaries.size() - 1;

    ASSERT(nDocs % nConsumers == 0);

    ExchangeSpec spec;
    spec.setPolicy(ExchangePolicyEnum::kKeyRange);
    spec.setKey(BSON("a" << 1));
    spec.setBoundaries(boundaries);
    spec.setConsumers(nConsumers);
    spec.setBufferSize(1024);

    boost::intrusive_ptr<Exchange> ex =
        new Exchange(std::move(spec), unittest::assertGet(Pipeline::create({source}, getExpCtx())));

    std::vector<boost::intrusive_ptr<DocumentSourceExchange>> prods;

    for (size_t idx = 0; idx < nConsumers; ++idx) {
        prods.push_back(new DocumentSourceExchange(getExpCtx(), ex, idx, nullptr));
    }

    std::vector<executor::TaskExecutor::CallbackHandle> handles;

    AtomicWord<size_t> processedDocs{0};

    for (size_t id = 0; id < nConsumers; ++id) {
        auto handle = _executor->scheduleWork(
            [prods, id, &processedDocs](const executor::TaskExecutor::CallbackArgs& cb) {
                PseudoRandom prng(getNewSeed());

                auto input = prods[id]->getNext();

                size_t docs = 0;
                for (; input.isAdvanced(); input = prods[id]->getNext()) {
                    size_t value = input.getDocument()["a"].getInt();

                    ASSERT(value >= id * 100);
                    ASSERT(value < (id + 1) * 100);

                    ++docs;

                    // This helps randomizing thread scheduling forcing different threads to load
                    // buffers. The sleep API is inherently imprecise so we cannot guarantee 100%
                    // reproducibility.
                    sleepmillis(prng.nextInt32() % 50 + 1);
                }
                processedDocs.fetchAndAdd(docs);
            });

        handles.emplace_back(std::move(handle.getValue()));
    }

    for (auto& h : handles)
        _executor->wait(h);

    ASSERT_EQ(nDocs, processedDocs.load());
}

TEST_F(DocumentSourceExchangeTest, RandomExchangeNConsumerResourceYielding) {
    const size_t nDocs = 500;
    auto source = getRandomMockSource(nDocs, getNewSeed());

    const std::vector<BSONObj> boundaries = {
        BSON("a" << MINKEY), BSON("a" << 500), BSON("a" << MAXKEY)};

    const size_t nConsumers = boundaries.size() - 1;

    ASSERT(nDocs % nConsumers == 0);

    ExchangeSpec spec;
    spec.setPolicy(ExchangePolicyEnum::kKeyRange);
    spec.setKey(BSON("a" << 1));
    spec.setBoundaries(boundaries);
    spec.setConsumers(nConsumers);

    // Tiny buffer so if there are deadlocks possible they reproduce more often.
    spec.setBufferSize(64);

    // An "artifical" mutex that's not actually necessary for thread safety. We enforce that each
    // thread holds this while it calls getNext(). This is to simulate the case where a thread may
    // hold some "real" resources which need to be yielded while waiting, such as the Session, or
    // the locks held in a transaction.
    stdx::mutex artificalGlobalMutex;

    boost::intrusive_ptr<Exchange> ex =
        new Exchange(std::move(spec), unittest::assertGet(Pipeline::create({source}, getExpCtx())));

    /**
     * This class is used for an Exchange consumer to temporarily relinquish control of a mutex
     * while it's blocked.
     */
    class MutexYielder : public ResourceYielder {
    public:
        MutexYielder(stdx::mutex* mutex) : _lock(*mutex, stdx::defer_lock) {}

        void yield(OperationContext* opCtx) override {
            _lock.unlock();
        }

        void unyield(OperationContext* opCtx) override {
            _lock.lock();
        }

        stdx::unique_lock<stdx::mutex>& getLock() {
            return _lock;
        }

    private:
        stdx::unique_lock<stdx::mutex> _lock;
    };

    /**
     * Used to keep track of each client and operation context.
     */
    struct ThreadInfo {
        ServiceContext::UniqueClient client;
        ServiceContext::UniqueOperationContext opCtx;
        boost::intrusive_ptr<DocumentSourceExchange> documentSourceExchange;
        MutexYielder* yielder;
    };
    std::vector<ThreadInfo> threads;

    for (size_t idx = 0; idx < nConsumers; ++idx) {
        ServiceContext::UniqueClient client = getServiceContext()->makeClient("exchange client");
        ServiceContext::UniqueOperationContext opCtxOwned =
            getServiceContext()->makeOperationContext(client.get());
        OperationContext* opCtx = opCtxOwned.get();
        auto yielder = std::make_unique<MutexYielder>(&artificalGlobalMutex);
        auto yielderRaw = yielder.get();

        threads.push_back(
            ThreadInfo{std::move(client),
                       std::move(opCtxOwned),
                       new DocumentSourceExchange(
                           new ExpressionContext(opCtx, nullptr), ex, idx, std::move(yielder)),
                       yielderRaw});
    }

    std::vector<executor::TaskExecutor::CallbackHandle> handles;

    AtomicWord<size_t> processedDocs{0};

    for (size_t id = 0; id < nConsumers; ++id) {
        ThreadInfo* threadInfo = &threads[id];
        auto handle = _executor->scheduleWork(
            [threadInfo, &processedDocs](const executor::TaskExecutor::CallbackArgs& cb) {

                DocumentSourceExchange* exchange = threadInfo->documentSourceExchange.get();
                const auto getNext = [exchange, threadInfo]() {
                    // Will acquire 'artificalGlobalMutex'. Within getNext() it will be released and
                    // reacquired by the MutexYielder if the Exchange has to block.
                    threadInfo->yielder->getLock().lock();
                    auto res = exchange->getNext();
                    threadInfo->yielder->getLock().unlock();
                    return res;
                };

                for (auto input = getNext(); input.isAdvanced(); input = getNext()) {
                    // This helps randomizing thread scheduling forcing different threads to load
                    // buffers. The sleep API is inherently imprecise so we cannot guarantee 100%
                    // reproducibility.
                    PseudoRandom prng(getNewSeed());
                    sleepmillis(prng.nextInt32() % 50 + 1);
                    processedDocs.fetchAndAdd(1);
                }
            });

        handles.emplace_back(std::move(handle.getValue()));
    }

    for (auto& h : handles)
        _executor->wait(h);

    ASSERT_EQ(nDocs, processedDocs.load());
}

TEST_F(DocumentSourceExchangeTest, RangeRandomHashExchangeNConsumer) {
    const size_t nDocs = 500;
    auto source = getRandomMockSource(nDocs, getNewSeed());

    const std::vector<BSONObj> boundaries = {
        BSON("a" << MINKEY),
        BSON("a" << BSONElementHasher::hash64(BSON("" << 0).firstElement(),
                                              BSONElementHasher::DEFAULT_HASH_SEED)),
        BSON("a" << MAXKEY)};

    const size_t nConsumers = boundaries.size() - 1;

    ASSERT(nDocs % nConsumers == 0);

    ExchangeSpec spec;
    spec.setPolicy(ExchangePolicyEnum::kKeyRange);
    spec.setKey(BSON("a"
                     << "hashed"));
    spec.setBoundaries(boundaries);
    spec.setConsumers(nConsumers);
    spec.setBufferSize(1024);

    boost::intrusive_ptr<Exchange> ex =
        new Exchange(std::move(spec), unittest::assertGet(Pipeline::create({source}, getExpCtx())));

    std::vector<boost::intrusive_ptr<DocumentSourceExchange>> prods;

    for (size_t idx = 0; idx < nConsumers; ++idx) {
        prods.push_back(new DocumentSourceExchange(getExpCtx(), ex, idx, nullptr));
    }

    std::vector<executor::TaskExecutor::CallbackHandle> handles;

    AtomicWord<size_t> processedDocs{0};

    for (size_t id = 0; id < nConsumers; ++id) {
        auto handle = _executor->scheduleWork(
            [prods, id, &processedDocs](const executor::TaskExecutor::CallbackArgs& cb) {
                PseudoRandom prng(getNewSeed());

                auto input = prods[id]->getNext();

                size_t docs = 0;
                for (; input.isAdvanced(); input = prods[id]->getNext()) {
                    ++docs;

                    // This helps randomizing thread scheduling forcing different threads to load
                    // buffers. The sleep API is inherently imprecise so we cannot guarantee 100%
                    // reproducibility.
                    sleepmillis(prng.nextInt32() % 50 + 1);
                }
                processedDocs.fetchAndAdd(docs);
            });

        handles.emplace_back(std::move(handle.getValue()));
    }

    for (auto& h : handles)
        _executor->wait(h);

    ASSERT_EQ(nDocs, processedDocs.load());
}

TEST_F(DocumentSourceExchangeTest, RejectNoConsumers) {
    BSONObj spec = BSON("policy"
                        << "broadcast"
                        << "consumers"
                        << 0);
    ASSERT_THROWS_CODE(
        Exchange(parseSpec(spec), unittest::assertGet(Pipeline::create({}, getExpCtx()))),
        AssertionException,
        50901);
}

TEST_F(DocumentSourceExchangeTest, RejectInvalidKey) {
    BSONObj spec = BSON("policy"
                        << "broadcast"
                        << "consumers"
                        << 1
                        << "key"
                        << BSON("a" << 2));
    ASSERT_THROWS_CODE(
        Exchange(parseSpec(spec), unittest::assertGet(Pipeline::create({}, getExpCtx()))),
        AssertionException,
        50896);
}

TEST_F(DocumentSourceExchangeTest, RejectInvalidKeyHashExpected) {
    BSONObj spec = BSON("policy"
                        << "broadcast"
                        << "consumers"
                        << 1
                        << "key"
                        << BSON("a"
                                << "nothash"));
    ASSERT_THROWS_CODE(
        Exchange(parseSpec(spec), unittest::assertGet(Pipeline::create({}, getExpCtx()))),
        AssertionException,
        50895);
}

TEST_F(DocumentSourceExchangeTest, RejectInvalidKeyWrongType) {
    BSONObj spec = BSON("policy"
                        << "broadcast"
                        << "consumers"
                        << 1
                        << "key"
                        << BSON("a" << true));
    ASSERT_THROWS_CODE(
        Exchange(parseSpec(spec), unittest::assertGet(Pipeline::create({}, getExpCtx()))),
        AssertionException,
        50897);
}

TEST_F(DocumentSourceExchangeTest, RejectInvalidKeyEmpty) {
    BSONObj spec = BSON("policy"
                        << "broadcast"
                        << "consumers"
                        << 1
                        << "key"
                        << BSON("" << 1));
    ASSERT_THROWS_CODE(
        Exchange(parseSpec(spec), unittest::assertGet(Pipeline::create({}, getExpCtx()))),
        AssertionException,
        40352);
}

TEST_F(DocumentSourceExchangeTest, RejectInvalidBoundaries) {
    BSONObj spec = BSON("policy"
                        << "keyRange"
                        << "consumers"
                        << 1
                        << "key"
                        << BSON("a" << 1)
                        << "boundaries"
                        << BSON_ARRAY(BSON("a" << MAXKEY) << BSON("a" << MINKEY))
                        << "consumerIds"
                        << BSON_ARRAY(0));
    ASSERT_THROWS_CODE(
        Exchange(parseSpec(spec), unittest::assertGet(Pipeline::create({}, getExpCtx()))),
        AssertionException,
        50893);
}

TEST_F(DocumentSourceExchangeTest, RejectInvalidBoundariesMissingMin) {
    BSONObj spec = BSON("policy"
                        << "keyRange"
                        << "consumers"
                        << 1
                        << "key"
                        << BSON("a" << 1)
                        << "boundaries"
                        << BSON_ARRAY(BSON("a" << 0) << BSON("a" << MAXKEY))
                        << "consumerIds"
                        << BSON_ARRAY(0));
    ASSERT_THROWS_CODE(
        Exchange(parseSpec(spec), unittest::assertGet(Pipeline::create({}, getExpCtx()))),
        AssertionException,
        50958);
}

TEST_F(DocumentSourceExchangeTest, RejectInvalidBoundariesMissingMax) {
    BSONObj spec = BSON("policy"
                        << "keyRange"
                        << "consumers"
                        << 1
                        << "key"
                        << BSON("a" << 1)
                        << "boundaries"
                        << BSON_ARRAY(BSON("a" << MINKEY) << BSON("a" << 0))
                        << "consumerIds"
                        << BSON_ARRAY(0));
    ASSERT_THROWS_CODE(
        Exchange(parseSpec(spec), unittest::assertGet(Pipeline::create({}, getExpCtx()))),
        AssertionException,
        50959);
}

TEST_F(DocumentSourceExchangeTest, RejectInvalidBoundariesAndConsumerIds) {
    BSONObj spec = BSON("policy"
                        << "keyRange"
                        << "consumers"
                        << 2
                        << "key"
                        << BSON("a" << 1)
                        << "boundaries"
                        << BSON_ARRAY(BSON("a" << MINKEY) << BSON("a" << MAXKEY))
                        << "consumerIds"
                        << BSON_ARRAY(0 << 1));
    ASSERT_THROWS_CODE(
        Exchange(parseSpec(spec), unittest::assertGet(Pipeline::create({}, getExpCtx()))),
        AssertionException,
        50900);
}

TEST_F(DocumentSourceExchangeTest, RejectInvalidPolicyBoundaries) {
    BSONObj spec = BSON("policy"
                        << "roundrobin"
                        << "consumers"
                        << 1
                        << "key"
                        << BSON("a" << 1)
                        << "boundaries"
                        << BSON_ARRAY(BSON("a" << MINKEY) << BSON("a" << MAXKEY))
                        << "consumerIds"
                        << BSON_ARRAY(0));
    ASSERT_THROWS_CODE(
        Exchange(parseSpec(spec), unittest::assertGet(Pipeline::create({}, getExpCtx()))),
        AssertionException,
        50899);
}

TEST_F(DocumentSourceExchangeTest, RejectInvalidConsumerIds) {
    BSONObj spec = BSON("policy"
                        << "keyRange"
                        << "consumers"
                        << 1
                        << "key"
                        << BSON("a" << 1)
                        << "boundaries"
                        << BSON_ARRAY(BSON("a" << MINKEY) << BSON("a" << MAXKEY))
                        << "consumerIds"
                        << BSON_ARRAY(1));
    ASSERT_THROWS_CODE(
        Exchange(parseSpec(spec), unittest::assertGet(Pipeline::create({}, getExpCtx()))),
        AssertionException,
        50894);
}

TEST_F(DocumentSourceExchangeTest, RejectInvalidMissingKeys) {
    BSONObj spec = BSON("policy"
                        << "keyRange"
                        << "consumers"
                        << 1
                        << "boundaries"
                        << BSON_ARRAY(BSON("a" << MINKEY) << BSON("a" << MAXKEY))
                        << "consumerIds"
                        << BSON_ARRAY(0));
    ASSERT_THROWS_CODE(
        Exchange(parseSpec(spec), unittest::assertGet(Pipeline::create({}, getExpCtx()))),
        AssertionException,
        50967);
}

}  // namespace monger
