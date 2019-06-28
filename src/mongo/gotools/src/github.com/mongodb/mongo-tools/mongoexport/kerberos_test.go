// Copyright (C) MongoDB, Inc. 2014-present.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may
// not use this file except in compliance with the License. You may obtain
// a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

package mongerexport

import (
	"bytes"
	"strings"
	"testing"

	"github.com/mongerdb/monger-tools-common/db"
	"github.com/mongerdb/monger-tools-common/json"
	"github.com/mongerdb/monger-tools-common/testtype"
	"github.com/mongerdb/monger-tools-common/testutil"
	. "github.com/smartystreets/goconvey/convey"
)

func TestKerberos(t *testing.T) {
	testtype.SkipUnlessTestType(t, testtype.KerberosTestType)

	Convey("Should be able to run mongerexport with Kerberos auth", t, func() {
		opts, err := testutil.GetKerberosOptions()

		So(err, ShouldBeNil)

		sessionProvider, err := db.NewSessionProvider(*opts)
		So(err, ShouldBeNil)

		export := MongerExport{
			ToolOptions:     opts,
			OutputOpts:      &OutputFormatOptions{},
			InputOpts:       &InputOptions{},
			SessionProvider: sessionProvider,
		}

		var out bytes.Buffer
		num, err := export.exportInternal(&out)

		So(err, ShouldBeNil)
		So(num, ShouldEqual, 1)
		outputLines := strings.Split(strings.TrimSpace(out.String()), "\n")
		So(len(outputLines), ShouldEqual, 1)
		outMap := map[string]interface{}{}
		So(json.Unmarshal([]byte(outputLines[0]), &outMap), ShouldBeNil)
		So(outMap["kerberos"], ShouldEqual, true)
		So(outMap["authenticated"], ShouldEqual, "yeah")
		So(outMap["_id"].(map[string]interface{})["$oid"], ShouldEqual, "528fb35afb3a8030e2f643c3")
	})
}
