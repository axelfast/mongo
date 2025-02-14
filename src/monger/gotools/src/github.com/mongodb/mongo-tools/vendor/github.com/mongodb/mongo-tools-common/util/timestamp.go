// Copyright (C) MongoDB, Inc. 2019-present.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may
// not use this file except in compliance with the License. You may obtain
// a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

package util

import "go.mongerdb.org/monger-driver/bson/primitive"

// TimestampGreaterThan returns true if lhs comes after rhs, false otherwise.
func TimestampGreaterThan(lhs, rhs primitive.Timestamp) bool {
	return lhs.T > rhs.T || lhs.T == rhs.T && lhs.I > rhs.I
}
