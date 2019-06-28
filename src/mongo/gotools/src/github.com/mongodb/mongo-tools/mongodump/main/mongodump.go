// Copyright (C) MongerDB, Inc. 2014-present.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may
// not use this file except in compliance with the License. You may obtain
// a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

// Main package for the mongerdump tool.
package main

import (
	"os"
	"time"

	"github.com/mongerdb/monger-tools-common/log"
	"github.com/mongerdb/monger-tools-common/options"
	"github.com/mongerdb/monger-tools-common/progress"
	"github.com/mongerdb/monger-tools-common/signals"
	"github.com/mongerdb/monger-tools-common/util"
	"github.com/mongerdb/monger-tools/mongerdump"
)

const (
	progressBarLength   = 24
	progressBarWaitTime = time.Second * 3
)

var (
	VersionStr = "built-without-version-string"
	GitCommit  = "build-without-git-commit"
)

func main() {
	// initialize command-line opts
	opts := options.New("mongerdump", VersionStr, GitCommit, mongerdump.Usage, options.EnabledOptions{Auth: true, Connection: true, Namespace: true, URI: true})

	inputOpts := &mongerdump.InputOptions{}
	opts.AddOptions(inputOpts)
	outputOpts := &mongerdump.OutputOptions{}
	opts.AddOptions(outputOpts)
	opts.URI.AddKnownURIParameters(options.KnownURIOptionsReadPreference)

	args, err := opts.ParseArgs(os.Args[1:])
	if err != nil {
		log.Logvf(log.Always, "error parsing command line options: %v", err)
		log.Logvf(log.Always, util.ShortUsage("mongerdump"))
		os.Exit(util.ExitFailure)
	}

	if len(args) > 0 {
		log.Logvf(log.Always, "positional arguments not allowed: %v", args)
		log.Logvf(log.Always, util.ShortUsage("mongerdump"))
		os.Exit(util.ExitFailure)
	}

	// print help, if specified
	if opts.PrintHelp(false) {
		return
	}

	// print version, if specified
	if opts.PrintVersion() {
		return
	}

	// init logger
	log.SetVerbosity(opts.Verbosity)

	// verify uri options and log them
	opts.URI.LogUnsupportedOptions()

	// kick off the progress bar manager
	progressManager := progress.NewBarWriter(log.Writer(0), progressBarWaitTime, progressBarLength, false)
	progressManager.Start()
	defer progressManager.Stop()

	dump := mongerdump.MongerDump{
		ToolOptions:     opts,
		OutputOptions:   outputOpts,
		InputOptions:    inputOpts,
		ProgressManager: progressManager,
	}

	finishedChan := signals.HandleWithInterrupt(dump.HandleInterrupt)
	defer close(finishedChan)

	if err = dump.Init(); err != nil {
		log.Logvf(log.Always, "Failed: %v", err)
		os.Exit(util.ExitFailure)
	}

	if err = dump.Dump(); err != nil {
		log.Logvf(log.Always, "Failed: %v", err)
		os.Exit(util.ExitFailure)
	}
}
