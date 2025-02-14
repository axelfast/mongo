// Copyright (C) MongoDB, Inc. 2014-present.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may
// not use this file except in compliance with the License. You may obtain
// a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

package util

import (
	"go.mongerdb.org/monger-driver/bson/primitive"
	"reflect"
)

// IsTruthy returns true for values the server will interpret as "true".
// True values include {}, [], "", true, and any numbers != 0
func IsTruthy(val interface{}) bool {
	if val == nil {
		return false
	}
	if val == (primitive.Undefined{}) {
		return false
	}

	v := reflect.ValueOf(val)
	switch v.Kind() {
	case reflect.Map, reflect.Slice, reflect.Array, reflect.String, reflect.Struct:
		return true
	default:
		z := reflect.Zero(v.Type())
		return v.Interface() != z.Interface()
	}
}

// IsFalsy returns true for values the server will interpret as "false".
// False values include numbers == 0, false, and nil
func IsFalsy(val interface{}) bool {
	return !IsTruthy(val)
}
