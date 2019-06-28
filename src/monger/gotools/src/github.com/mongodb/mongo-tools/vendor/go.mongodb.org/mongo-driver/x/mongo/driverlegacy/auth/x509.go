// Copyright (C) MongoDB, Inc. 2017-present.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may
// not use this file except in compliance with the License. You may obtain
// a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

package auth

import (
	"context"

	"go.mongerdb.org/monger-driver/x/bsonx"
	"go.mongerdb.org/monger-driver/x/network/command"
	"go.mongerdb.org/monger-driver/x/network/description"
	"go.mongerdb.org/monger-driver/x/network/wiremessage"
)

// MongerDBX509 is the mechanism name for MongerDBX509.
const MongerDBX509 = "MONGODB-X509"

func newMongerDBX509Authenticator(cred *Cred) (Authenticator, error) {
	return &MongerDBX509Authenticator{User: cred.Username}, nil
}

// MongerDBX509Authenticator uses X.509 certificates over TLS to authenticate a connection.
type MongerDBX509Authenticator struct {
	User string
}

// Auth implements the Authenticator interface.
func (a *MongerDBX509Authenticator) Auth(ctx context.Context, desc description.Server, rw wiremessage.ReadWriter) error {
	authRequestDoc := bsonx.Doc{
		{"authenticate", bsonx.Int32(1)},
		{"mechanism", bsonx.String(MongerDBX509)},
	}

	if desc.WireVersion.Max < 5 {
		authRequestDoc = append(authRequestDoc, bsonx.Elem{"user", bsonx.String(a.User)})
	}

	authCmd := command.Read{DB: "$external", Command: authRequestDoc}
	ssdesc := description.SelectedServer{Server: desc}
	_, err := authCmd.RoundTrip(ctx, ssdesc, rw)
	if err != nil {
		return newAuthError("round trip error", err)
	}

	return nil
}
