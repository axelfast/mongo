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

#include "monger/db/ftdc/ftdc_test.h"

#include <boost/filesystem.hpp>
#include <memory>

#include "monger/base/data_type_validated.h"
#include "monger/base/init.h"
#include "monger/bson/bson_validate.h"
#include "monger/db/client.h"
#include "monger/db/ftdc/file_reader.h"
#include "monger/db/jsobj.h"
#include "monger/db/service_context.h"
#include "monger/unittest/unittest.h"
#include "monger/util/clock_source.h"
#include "monger/util/clock_source_mock.h"
#include "monger/util/tick_source_mock.h"

namespace monger {

namespace {

BSONObj filteredFTDCCopy(const BSONObj& obj) {
    BSONObjBuilder builder;
    for (const auto& f : obj) {
        if (FTDCBSONUtil::isFTDCType(f.type())) {
            builder.append(f);
        }
    }
    return builder.obj();
}
}  // namespace


void ValidateDocumentList(const boost::filesystem::path& p,
                          const std::vector<BSONObj>& docs,
                          FTDCValidationMode mode) {
    FTDCFileReader reader;

    ASSERT_OK(reader.open(p));

    std::vector<BSONObj> list;
    auto sw = reader.hasNext();
    while (sw.isOK() && sw.getValue()) {
        list.emplace_back(std::get<1>(reader.next()).getOwned());
        sw = reader.hasNext();
    }

    ASSERT_OK(sw);

    ValidateDocumentList(list, docs, mode);
}

void ValidateDocumentList(const std::vector<BSONObj>& docs1,
                          const std::vector<BSONObj>& docs2,
                          FTDCValidationMode mode) {
    ASSERT_EQUALS(docs1.size(), docs2.size());

    auto ai = docs1.begin();
    auto bi = docs2.begin();

    while (ai != docs1.end() && bi != docs2.end()) {
        if (mode == FTDCValidationMode::kStrict) {
            if (SimpleBSONObjComparator::kInstance.evaluate(*ai != *bi)) {
                std::cout << *ai << " vs " << *bi << std::endl;
                ASSERT_BSONOBJ_EQ(*ai, *bi);
            }
        } else {
            BSONObj left = filteredFTDCCopy(*ai);
            BSONObj right = filteredFTDCCopy(*bi);
            if (SimpleBSONObjComparator::kInstance.evaluate(left != right)) {
                std::cout << left << " vs " << right << std::endl;
                ASSERT_BSONOBJ_EQ(left, right);
            }
        }

        ++ai;
        ++bi;
    }

    ASSERT_TRUE(ai == docs1.end() && bi == docs2.end());
}


void deleteFileIfNeeded(const boost::filesystem::path& p) {
    if (boost::filesystem::exists(p)) {
        boost::filesystem::remove(p);
    }
}

std::vector<boost::filesystem::path> scanDirectory(const boost::filesystem::path& path) {
    boost::filesystem::directory_iterator di(path);
    std::vector<boost::filesystem::path> files;

    for (; di != boost::filesystem::directory_iterator(); di++) {
        boost::filesystem::directory_entry& de = *di;
        auto f = de.path().filename();
        files.emplace_back(path / f);
    }

    std::sort(files.begin(), files.end());

    return files;
}

void createDirectoryClean(const boost::filesystem::path& dir) {
    boost::filesystem::remove_all(dir);

    boost::filesystem::create_directory(dir);
}

FTDCTest::FTDCTest() {
    auto service = getServiceContext();
    service->setFastClockSource(std::make_unique<ClockSourceMock>());
    service->setPreciseClockSource(std::make_unique<ClockSourceMock>());
    service->setTickSource(std::make_unique<TickSourceMock<>>());
}

}  // namespace monger
