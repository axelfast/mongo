// Copyright (C) MongerDB, Inc. 2014-present.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may
// not use this file except in compliance with the License. You may obtain
// a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

// Main package for the mongerimport tool.
package main

import (
	"os"

	"github.com/mongerdb/monger-tools-common/log"
	"github.com/mongerdb/monger-tools-common/signals"
	"github.com/mongerdb/monger-tools-common/util"
	"github.com/mongerdb/monger-tools/mongerimport"
)

var (
	VersionStr = "built-without-version-string"
	GitCommit  = "build-without-git-commit"
)

func main() {
	opts, err := mongerimport.ParseOptions(os.Args[1:], VersionStr, GitCommit)
	if err != nil {
		log.Logvf(log.Always, "error parsing command line options: %v", err)
		log.Logvf(log.Always, util.ShortUsage("mongerimport"))
		os.Exit(util.ExitFailure)
	}

	signals.Handle()

	// print help, if specified
	if opts.PrintHelp(false) {
		return
	}

	// print version, if specified
	if opts.PrintVersion() {
		return
	}

	m, err := mongerimport.New(opts)
	if err != nil {
		log.Logvf(log.Always, err.Error())
		os.Exit(util.ExitFailure)
	}
	defer m.Close()

	numDocs, numFailure, err := m.ImportDocuments()
	if !opts.Quiet {
		if err != nil {
			log.Logvf(log.Always, "Failed: %v", err)
		}
		if m.ToolOptions.WriteConcern.Acknowledged() {
			log.Logvf(log.Always, "%v document(s) imported successfully. %v document(s) failed to import.", numDocs, numFailure)
		} else {
			log.Logvf(log.Always, "done")
		}
	}
	if err != nil {
		os.Exit(util.ExitFailure)
	}
}
