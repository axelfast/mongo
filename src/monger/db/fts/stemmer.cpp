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

#include <cstdlib>

#include "monger/db/fts/stemmer.h"
#include "monger/util/str.h"

namespace monger {

namespace fts {

Stemmer::Stemmer(const FTSLanguage* language) {
    _stemmer = nullptr;
    if (language->str() != "none")
        _stemmer = sb_stemmer_new(language->str().c_str(), "UTF_8");
}

Stemmer::~Stemmer() {
    if (_stemmer) {
        sb_stemmer_delete(_stemmer);
        _stemmer = nullptr;
    }
}

StringData Stemmer::stem(StringData word) const {
    if (!_stemmer)
        return word;

    const sb_symbol* sb_sym =
        sb_stemmer_stem(_stemmer, (const sb_symbol*)word.rawData(), word.size());

    if (sb_sym == nullptr) {
        // out of memory
        MONGO_UNREACHABLE;
    }

    return StringData((const char*)(sb_sym), sb_stemmer_length(_stemmer));
}
}
}
