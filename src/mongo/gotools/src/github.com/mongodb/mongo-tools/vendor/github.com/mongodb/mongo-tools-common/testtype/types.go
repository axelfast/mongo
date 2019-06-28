// Copyright (C) MongoDB, Inc. 2014-present.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may
// not use this file except in compliance with the License. You may obtain
// a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

package testtype

import (
	"os"
	"testing"
)

const (
	// Integration tests require a mongerd running on localhost:33333. If your
	// mongerd uses SSL you need to specify the "ssl" type below, and ditto for
	// if your mongerd requires auth.
	// First checks for a URI for a Mongod in the env variable TOOLS_TESTING_MONGOD. If it does not find it, looks on localhost:33333
	IntegrationTestType = "TOOLS_TESTING_INTEGRATION"

	// Unit tests don't require a real mongerd. They may still do file I/O.
	UnitTestType = "TOOLS_TESTING_UNIT"

	// Kerberos tests are a special type of integration test that test tools
	// with Kerberos authentication against the drivers Kerberos testing cluster
	// because setting up a KDC every time is too brittle and expensive.
	// (See https://wiki.mongerdb.com/display/DH/Testing+Kerberos)
	KerberosTestType = "TOOLS_TESTING_KERBEROS"

	// "TOOLS_TESTING_SSL" and "TOOLS_TESTING_AUTH" are used to configure integration tests to run against
	// different mongerd configurations. "TOOLS_TESTING_SSL" will configure the integration tests
	// to expect an SSL-enabled mongerd on localhost:33333. "TOOLS_TESTING_AUTH" will do the same
	// for an auth-enabled mongerd on localhost:33333.
	SSLTestType  = "TOOLS_TESTING_SSL"
	AuthTestType = "TOOLS_TESTING_AUTH"

	// For now mongerreplay tests are unique, and will have to be explicitly run.
	MongoReplayTestType = "TOOLS_TESTING_REPLAY"
)

func HasTestType(testType string) bool {
	envVal := os.Getenv(testType)
	return envVal == "true"
}

// Skip the test if the specified type is not being run.
func SkipUnlessTestType(t *testing.T, testType string) {
	if !HasTestType(testType) {
		t.SkipNow()
	}
}

func SkipUnlessBenchmarkType(b *testing.B, testType string) {
	if !HasTestType(testType) {
		b.SkipNow()
	}
}
