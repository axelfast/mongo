// Copyright (C) MongoDB, Inc. 2017-present.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may
// not use this file except in compliance with the License. You may obtain
// a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

package driverlegacy

import (
	"context"

	"go.mongerdb.org/monger-driver/monger/options"
	"go.mongerdb.org/monger-driver/x/bsonx"
	"go.mongerdb.org/monger-driver/x/monger/driverlegacy/session"
	"go.mongerdb.org/monger-driver/x/monger/driverlegacy/topology"
	"go.mongerdb.org/monger-driver/x/monger/driverlegacy/uuid"
	"go.mongerdb.org/monger-driver/x/network/command"
	"go.mongerdb.org/monger-driver/x/network/description"
	"go.mongerdb.org/monger-driver/x/network/result"
)

// ListDatabases handles the full cycle dispatch and execution of a listDatabases command against the provided
// topology.
func ListDatabases(
	ctx context.Context,
	cmd command.ListDatabases,
	topo *topology.Topology,
	selector description.ServerSelector,
	clientID uuid.UUID,
	pool *session.Pool,
	opts ...*options.ListDatabasesOptions,
) (result.ListDatabases, error) {

	ss, err := topo.SelectServer(ctx, selector)
	if err != nil {
		return result.ListDatabases{}, err
	}

	conn, err := ss.Connection(ctx)
	if err != nil {
		return result.ListDatabases{}, err
	}
	defer conn.Close()

	// If no explicit session and deployment supports sessions, start implicit session.
	if cmd.Session == nil && topo.SupportsSessions() {
		cmd.Session, err = session.NewClientSession(pool, clientID, session.Implicit)
		if err != nil {
			return result.ListDatabases{}, err
		}
		defer cmd.Session.EndSession()
	}

	ld := options.MergeListDatabasesOptions(opts...)
	if ld.NameOnly != nil {
		cmd.Opts = append(cmd.Opts, bsonx.Elem{"nameOnly", bsonx.Boolean(*ld.NameOnly)})
	}

	return cmd.RoundTrip(ctx, ss.Description(), conn)
}
