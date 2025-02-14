// Copyright (C) MongoDB, Inc. 2017-present.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may
// not use this file except in compliance with the License. You may obtain
// a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

package session

import (
	"time"

	"crypto/rand"

	"go.mongerdb.org/monger-driver/x/bsonx"
	"go.mongerdb.org/monger-driver/x/monger/driverlegacy/uuid"
)

var rander = rand.Reader

// Server is an open session with the server.
type Server struct {
	SessionID bsonx.Doc
	TxnNumber int64
	LastUsed  time.Time
}

// returns whether or not a session has expired given a timeout in minutes
// a session is considered expired if it has less than 1 minute left before becoming stale
func (ss *Server) expired(timeoutMinutes uint32) bool {
	if timeoutMinutes <= 0 {
		return true
	}
	timeUnused := time.Since(ss.LastUsed).Minutes()
	return timeUnused > float64(timeoutMinutes-1)
}

// update the last used time for this session.
// must be called whenever this server session is used to send a command to the server.
func (ss *Server) updateUseTime() {
	ss.LastUsed = time.Now()
}

func newServerSession() (*Server, error) {
	id, err := uuid.New()
	if err != nil {
		return nil, err
	}

	idDoc := bsonx.Doc{{"id", bsonx.Binary(UUIDSubtype, id[:])}}

	return &Server{
		SessionID: idDoc,
		LastUsed:  time.Now(),
	}, nil
}

// IncrementTxnNumber increments the transaction number.
func (ss *Server) IncrementTxnNumber() {
	ss.TxnNumber++
}

// UUIDSubtype is the BSON binary subtype that a UUID should be encoded as
const UUIDSubtype byte = 4
