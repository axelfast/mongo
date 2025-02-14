// Copyright (C) MongoDB, Inc. 2014-present.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may
// not use this file except in compliance with the License. You may obtain
// a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

// Main package for the mongertop tool.
package main

import (
	"os"
	"strconv"
	"time"

	"github.com/mongerdb/monger-tools-common/db"
	"github.com/mongerdb/monger-tools-common/log"
	"github.com/mongerdb/monger-tools-common/options"
	"github.com/mongerdb/monger-tools-common/signals"
	"github.com/mongerdb/monger-tools-common/util"
	"github.com/mongerdb/monger-tools/mongertop"
	"go.mongerdb.org/monger-driver/monger/readpref"
)

var (
	VersionStr = "built-without-version-string"
	GitCommit  = "build-without-git-commit"
)

func main() {
	// initialize command-line opts
	opts := options.New("mongertop", VersionStr, GitCommit, mongertop.Usage,
		options.EnabledOptions{Auth: true, Connection: true, Namespace: false, URI: true})
	opts.UseReadOnlyHostDescription()

	// add mongertop-specific options
	outputOpts := &mongertop.Output{}
	opts.AddOptions(outputOpts)

	args, err := opts.ParseArgs(os.Args[1:])
	if err != nil {
		log.Logvf(log.Always, "error parsing command line options: %v", err)
		log.Logvf(log.Always, util.ShortUsage("mongertop"))
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

	log.SetVerbosity(opts.Verbosity)
	signals.Handle()

	// verify uri options and log them
	opts.URI.LogUnsupportedOptions()

	if len(args) > 1 {
		log.Logvf(log.Always, "too many positional arguments")
		log.Logvf(log.Always, util.ShortUsage("mongertop"))
		os.Exit(util.ExitFailure)
	}

	sleeptime := 1 // default to 1 second sleep time
	if len(args) > 0 {
		sleeptime, err = strconv.Atoi(args[0])
		if err != nil || sleeptime <= 0 {
			log.Logvf(log.Always, "invalid sleep time: %v", args[0])
			os.Exit(util.ExitFailure)
		}
	}
	if outputOpts.RowCount < 0 {
		log.Logvf(log.Always, "invalid value for --rowcount: %v", outputOpts.RowCount)
		os.Exit(util.ExitFailure)
	}

	if opts.Auth.Username != "" && opts.Auth.Source == "" && !opts.Auth.RequiresExternalDB() {
		if opts.URI != nil && opts.URI.ConnectionString != "" {
			log.Logvf(log.Always, "authSource is required when authenticating against a non $external database")
			os.Exit(util.ExitFailure)
		}
		log.Logvf(log.Always, "--authenticationDatabase is required when authenticating against a non $external database")
		os.Exit(util.ExitFailure)
	}

	if opts.ReplicaSetName == "" {
		opts.ReadPreference = readpref.PrimaryPreferred()
	}

	// create a session provider to connect to the db
	sessionProvider, err := db.NewSessionProvider(*opts)
	if err != nil {
		log.Logvf(log.Always, "error connecting to host: %v", err)
		os.Exit(util.ExitFailure)
	}

	// fail fast if connecting to a mongers
	isMongers, err := sessionProvider.IsMongers()
	if err != nil {
		log.Logvf(log.Always, "Failed: %v", err)
		os.Exit(util.ExitFailure)
	}
	if isMongers {
		log.Logvf(log.Always, "cannot run mongertop against a mongers")
		os.Exit(util.ExitFailure)
	}

	// instantiate a mongertop instance
	top := &mongertop.MongerTop{
		Options:         opts,
		OutputOptions:   outputOpts,
		SessionProvider: sessionProvider,
		Sleeptime:       time.Duration(sleeptime) * time.Second,
	}

	// kick it off
	if err := top.Run(); err != nil {
		log.Logvf(log.Always, "Failed: %v", err)
		os.Exit(util.ExitFailure)
	}
}
