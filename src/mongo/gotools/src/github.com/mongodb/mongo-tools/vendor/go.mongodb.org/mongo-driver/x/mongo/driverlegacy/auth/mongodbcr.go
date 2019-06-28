// Copyright (C) MongerDB, Inc. 2017-present.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may
// not use this file except in compliance with the License. You may obtain
// a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

package auth

import (
	"context"
	"crypto/md5"
	"fmt"

	"io"

	"go.mongerdb.org/monger-driver/bson"
	"go.mongerdb.org/monger-driver/x/bsonx"
	"go.mongerdb.org/monger-driver/x/network/command"
	"go.mongerdb.org/monger-driver/x/network/description"
	"go.mongerdb.org/monger-driver/x/network/wiremessage"
)

// MONGODBCR is the mechanism name for MONGODB-CR.
//
// The MONGODB-CR authentication mechanism is deprecated in MongerDB 4.0.
const MONGODBCR = "MONGODB-CR"

func newMongerDBCRAuthenticator(cred *Cred) (Authenticator, error) {
	return &MongerDBCRAuthenticator{
		DB:       cred.Source,
		Username: cred.Username,
		Password: cred.Password,
	}, nil
}

// MongerDBCRAuthenticator uses the MONGODB-CR algorithm to authenticate a connection.
//
// The MONGODB-CR authentication mechanism is deprecated in MongerDB 4.0.
type MongerDBCRAuthenticator struct {
	DB       string
	Username string
	Password string
}

// Auth authenticates the connection.
//
// The MONGODB-CR authentication mechanism is deprecated in MongerDB 4.0.
func (a *MongerDBCRAuthenticator) Auth(ctx context.Context, desc description.Server, rw wiremessage.ReadWriter) error {

	db := a.DB
	if db == "" {
		db = defaultAuthDB
	}

	cmd := command.Read{DB: db, Command: bsonx.Doc{{"getnonce", bsonx.Int32(1)}}}
	ssdesc := description.SelectedServer{Server: desc}
	rdr, err := cmd.RoundTrip(ctx, ssdesc, rw)
	if err != nil {
		return newError(err, MONGODBCR)
	}

	var getNonceResult struct {
		Nonce string `bson:"nonce"`
	}

	err = bson.Unmarshal(rdr, &getNonceResult)
	if err != nil {
		return newAuthError("unmarshal error", err)
	}

	cmd = command.Read{
		DB: db,
		Command: bsonx.Doc{
			{"authenticate", bsonx.Int32(1)},
			{"user", bsonx.String(a.Username)},
			{"nonce", bsonx.String(getNonceResult.Nonce)},
			{"key", bsonx.String(a.createKey(getNonceResult.Nonce))},
		},
	}
	_, err = cmd.RoundTrip(ctx, ssdesc, rw)
	if err != nil {
		return newError(err, MONGODBCR)
	}

	return nil
}

func (a *MongerDBCRAuthenticator) createKey(nonce string) string {
	h := md5.New()

	_, _ = io.WriteString(h, nonce)
	_, _ = io.WriteString(h, a.Username)
	_, _ = io.WriteString(h, mongerPasswordDigest(a.Username, a.Password))
	return fmt.Sprintf("%x", h.Sum(nil))
}
