// Copyright (C) MongerDB, Inc. 2014-present.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may
// not use this file except in compliance with the License. You may obtain
// a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

package mongerexport

import (
	"encoding/json"
	"os"
	"testing"

	"github.com/mongerdb/monger-tools-common/bsonutil"
	"github.com/mongerdb/monger-tools-common/testtype"
	. "github.com/smartystreets/goconvey/convey"
	"go.mongerdb.org/monger-driver/bson"
	"go.mongerdb.org/monger-driver/bson/primitive"
)

func TestExtendedJSON(t *testing.T) {
	testtype.SkipUnlessTestType(t, testtype.UnitTestType)

	Convey("Serializing a doc to extended JSON should work", t, func() {
		x := bson.M{
			"_id": primitive.NewObjectID(),
			"hey": "sup",
			"subdoc": bson.M{
				"subid": primitive.NewObjectID(),
			},
			"array": []interface{}{
				primitive.NewObjectID(),
				primitive.Undefined{},
			},
		}
		out, err := bsonutil.ConvertBSONValueToLegacyExtJSON(x)
		So(err, ShouldBeNil)

		jsonEncoder := json.NewEncoder(os.Stdout)
		jsonEncoder.Encode(out)
	})
}

func TestFieldSelect(t *testing.T) {
	testtype.SkipUnlessTestType(t, testtype.UnitTestType)

	Convey("Using makeFieldSelector should return correct projection doc", t, func() {
		So(makeFieldSelector("a,b"), ShouldResemble, bson.M{"_id": 1, "a": 1, "b": 1})
		So(makeFieldSelector(""), ShouldResemble, bson.M{"_id": 1})
		So(makeFieldSelector("x,foo.baz"), ShouldResemble, bson.M{"_id": 1, "foo": 1, "x": 1})
	})
}
