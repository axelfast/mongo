// Copyright (C) MongoDB, Inc. 2017-present.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may
// not use this file except in compliance with the License. You may obtain
// a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

package driverlegacy

import (
	"context"
	"time"

	"go.mongerdb.org/monger-driver/monger/options"
	"go.mongerdb.org/monger-driver/x/bsonx"
	"go.mongerdb.org/monger-driver/x/monger/driverlegacy/session"
	"go.mongerdb.org/monger-driver/x/monger/driverlegacy/topology"
	"go.mongerdb.org/monger-driver/x/monger/driverlegacy/uuid"
	"go.mongerdb.org/monger-driver/x/network/command"
	"go.mongerdb.org/monger-driver/x/network/description"
	"go.mongerdb.org/monger-driver/x/network/result"
)

// Distinct handles the full cycle dispatch and execution of a distinct command against the provided
// topology.
func Distinct(
	ctx context.Context,
	cmd command.Distinct,
	topo *topology.Topology,
	selector description.ServerSelector,
	clientID uuid.UUID,
	pool *session.Pool,
	opts ...*options.DistinctOptions,
) (result.Distinct, error) {

	if cmd.Session != nil && cmd.Session.PinnedServer != nil {
		selector = cmd.Session.PinnedServer
	}
	ss, err := topo.SelectServer(ctx, selector)
	if err != nil {
		return result.Distinct{}, err
	}

	desc := ss.Description()
	conn, err := ss.Connection(ctx)
	if err != nil {
		return result.Distinct{}, err
	}
	defer conn.Close()

	rp, err := getReadPrefBasedOnTransaction(cmd.ReadPref, cmd.Session)
	if err != nil {
		return result.Distinct{}, err
	}
	cmd.ReadPref = rp

	// If no explicit session and deployment supports sessions, start implicit session.
	if cmd.Session == nil && topo.SupportsSessions() {
		cmd.Session, err = session.NewClientSession(pool, clientID, session.Implicit)
		if err != nil {
			return result.Distinct{}, err
		}
		defer cmd.Session.EndSession()
	}

	distinctOpts := options.MergeDistinctOptions(opts...)

	if distinctOpts.MaxTime != nil {
		cmd.Opts = append(cmd.Opts, bsonx.Elem{
			"maxTimeMS", bsonx.Int64(int64(*distinctOpts.MaxTime / time.Millisecond)),
		})
	}
	if distinctOpts.Collation != nil {
		if desc.WireVersion.Max < 5 {
			return result.Distinct{}, ErrCollation
		}
		collDoc, err := bsonx.ReadDoc(distinctOpts.Collation.ToDocument())
		if err != nil {
			return result.Distinct{}, err
		}
		cmd.Opts = append(cmd.Opts, bsonx.Elem{"collation", bsonx.Document(collDoc)})
	}

	return cmd.RoundTrip(ctx, desc, conn)
}
