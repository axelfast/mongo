// Copyright (C) MongoDB, Inc. 2014-present.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may
// not use this file except in compliance with the License. You may obtain
// a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

// Main package for the mongerexport tool.
package main

import (
	"os"

	"github.com/mongerdb/monger-tools-common/log"
	"github.com/mongerdb/monger-tools-common/signals"
	"github.com/mongerdb/monger-tools-common/util"
	"github.com/mongerdb/monger-tools/mongerexport"
)

var (
	VersionStr = "built-without-version-string"
	GitCommit  = "build-without-git-commit"
)

func main() {
	opts, err := mongerexport.ParseOptions(os.Args[1:], VersionStr, GitCommit)
	if err != nil {
		log.Logvf(log.Always, "error parsing command line options: %v", err)
		log.Logvf(log.Always, util.ShortUsage("mongerexport"))
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

	exporter, err := mongerexport.New(opts)
	if err != nil {
		log.Logvf(log.Always, "%v", err)

		if se, ok := err.(util.SetupError); ok && se.Message != "" {
			log.Logv(log.Always, se.Message)
		}

		os.Exit(util.ExitFailure)
	}
	defer exporter.Close()

	writer, err := exporter.GetOutputWriter()
	if err != nil {
		log.Logvf(log.Always, "error opening output stream: %v", err)
		os.Exit(util.ExitFailure)
	}
	if writer == nil {
		writer = os.Stdout
	} else {
		defer writer.Close()
	}

	numDocs, err := exporter.Export(writer)
	if err != nil {
		log.Logvf(log.Always, "Failed: %v", err)
		os.Exit(util.ExitFailure)
	}

	if numDocs == 1 {
		log.Logvf(log.Always, "exported %v record", numDocs)
	} else {
		log.Logvf(log.Always, "exported %v records", numDocs)
	}

}
