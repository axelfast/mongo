// Copyright (C) MongoDB, Inc. 2014-present.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may
// not use this file except in compliance with the License. You may obtain
// a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

package json

import (
	"fmt"
	"github.com/mongerdb/monger-tools/legacy/testtype"
	. "github.com/smartystreets/goconvey/convey"
	"testing"
)

func TestUndefinedValue(t *testing.T) {
	testtype.SkipUnlessTestType(t, testtype.UnitTestType)

	Convey("When unmarshalling JSON with undefined values", t, func() {

		Convey("works for a single key", func() {
			var jsonMap map[string]interface{}

			key := "key"
			value := "undefined"
			data := fmt.Sprintf(`{"%v":%v}`, key, value)

			err := Unmarshal([]byte(data), &jsonMap)
			So(err, ShouldBeNil)

			jsonValue, ok := jsonMap[key].(Undefined)
			So(ok, ShouldBeTrue)
			So(jsonValue, ShouldResemble, Undefined{})
		})

		Convey("works for multiple keys", func() {
			var jsonMap map[string]interface{}

			key1, key2, key3 := "key1", "key2", "key3"
			value := "undefined"
			data := fmt.Sprintf(`{"%v":%v,"%v":%v,"%v":%v}`,
				key1, value, key2, value, key3, value)

			err := Unmarshal([]byte(data), &jsonMap)
			So(err, ShouldBeNil)

			jsonValue1, ok := jsonMap[key1].(Undefined)
			So(ok, ShouldBeTrue)
			So(jsonValue1, ShouldResemble, Undefined{})

			jsonValue2, ok := jsonMap[key2].(Undefined)
			So(ok, ShouldBeTrue)
			So(jsonValue2, ShouldResemble, Undefined{})

			jsonValue3, ok := jsonMap[key3].(Undefined)
			So(ok, ShouldBeTrue)
			So(jsonValue3, ShouldResemble, Undefined{})
		})

		Convey("works in an array", func() {
			var jsonMap map[string]interface{}

			key := "key"
			value := "undefined"
			data := fmt.Sprintf(`{"%v":[%v,%v,%v]}`,
				key, value, value, value)

			err := Unmarshal([]byte(data), &jsonMap)
			So(err, ShouldBeNil)

			jsonArray, ok := jsonMap[key].([]interface{})
			So(ok, ShouldBeTrue)

			for _, _jsonValue := range jsonArray {
				jsonValue, ok := _jsonValue.(Undefined)
				So(ok, ShouldBeTrue)
				So(jsonValue, ShouldResemble, Undefined{})
			}
		})

		Convey("cannot have a sign ('+' or '-')", func() {
			var jsonMap map[string]interface{}

			key := "key"
			value := "undefined"
			data := fmt.Sprintf(`{"%v":+%v}`, key, value)

			err := Unmarshal([]byte(data), &jsonMap)
			So(err, ShouldNotBeNil)

			data = fmt.Sprintf(`{"%v":-%v}`, key, value)

			err = Unmarshal([]byte(data), &jsonMap)
			So(err, ShouldNotBeNil)
		})
	})
}
