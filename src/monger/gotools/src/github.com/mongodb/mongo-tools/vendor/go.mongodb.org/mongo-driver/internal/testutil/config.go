// Copyright (C) MongoDB, Inc. 2017-present.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may
// not use this file except in compliance with the License. You may obtain
// a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

package testutil

import (
	"context"
	"fmt"
	"math"
	"os"
	"reflect"
	"strconv"
	"strings"
	"sync"
	"testing"

	"github.com/stretchr/testify/require"
	"go.mongerdb.org/monger-driver/event"
	"go.mongerdb.org/monger-driver/x/bsonx"
	"go.mongerdb.org/monger-driver/x/monger/driverlegacy/topology"
	"go.mongerdb.org/monger-driver/x/network/command"
	"go.mongerdb.org/monger-driver/x/network/connection"
	"go.mongerdb.org/monger-driver/x/network/connstring"
	"go.mongerdb.org/monger-driver/x/network/description"
)

var connectionString connstring.ConnString
var connectionStringOnce sync.Once
var connectionStringErr error
var liveTopology *topology.Topology
var liveTopologyOnce sync.Once
var liveTopologyErr error
var monitoredTopology *topology.Topology
var monitoredTopologyOnce sync.Once
var monitoredTopologyErr error

// AddOptionsToURI appends connection string options to a URI.
func AddOptionsToURI(uri string, opts ...string) string {
	if !strings.ContainsRune(uri, '?') {
		if uri[len(uri)-1] != '/' {
			uri += "/"
		}

		uri += "?"
	} else {
		uri += "&"
	}

	for _, opt := range opts {
		uri += opt
	}

	return uri
}

// AddTLSConfigToURI checks for the environmental variable indicating that the tests are being run
// on an SSL-enabled server, and if so, returns a new URI with the necessary configuration.
func AddTLSConfigToURI(uri string) string {
	caFile := os.Getenv("MONGO_GO_DRIVER_CA_FILE")
	if len(caFile) == 0 {
		return uri
	}

	return AddOptionsToURI(uri, "ssl=true&sslCertificateAuthorityFile=", caFile)
}

// AddCompressorToUri checks for the environment variable indicating that the tests are being run with compression
// enabled. If so, it returns a new URI with the necessary configuration
func AddCompressorToUri(uri string) string {
	comp := os.Getenv("MONGO_GO_DRIVER_COMPRESSOR")
	if len(comp) == 0 {
		return uri
	}

	return AddOptionsToURI(uri, "compressors=", comp)
}

// MonitoredTopology returns a new topology with the command monitor attached
func MonitoredTopology(t *testing.T, dbName string, monitor *event.CommandMonitor) *topology.Topology {
	cs := ConnString(t)
	opts := []topology.Option{
		topology.WithConnString(func(connstring.ConnString) connstring.ConnString { return cs }),
		topology.WithServerOptions(func(opts ...topology.ServerOption) []topology.ServerOption {
			return append(
				opts,
				topology.WithConnectionOptions(func(opts ...connection.Option) []connection.Option {
					return append(
						opts,
						connection.WithMonitor(func(*event.CommandMonitor) *event.CommandMonitor {
							return monitor
						}),
					)
				}),
			)
		}),
	}

	monitoredTopology, err := topology.New(opts...)
	if err != nil {
		t.Fatal(err)
	} else {
		monitoredTopology.Connect(context.Background())
		s, err := monitoredTopology.SelectServer(context.Background(), description.WriteSelector())
		require.NoError(t, err)

		c, err := s.Connection(context.Background())
		require.NoError(t, err)

		_, err = (&command.Write{
			DB:      dbName,
			Command: bsonx.Doc{{"dropDatabase", bsonx.Int32(1)}},
		}).RoundTrip(context.Background(), s.SelectedDescription(), c)

		require.NoError(t, err)
	}

	return monitoredTopology
}

// GlobalMonitoredTopology gets the globally configured topology and attaches a command monitor.
func GlobalMonitoredTopology(t *testing.T, monitor *event.CommandMonitor) *topology.Topology {
	cs := ConnString(t)
	opts := []topology.Option{
		topology.WithConnString(func(connstring.ConnString) connstring.ConnString { return cs }),
		topology.WithServerOptions(func(opts ...topology.ServerOption) []topology.ServerOption {
			return append(
				opts,
				topology.WithConnectionOptions(func(opts ...connection.Option) []connection.Option {
					return append(
						opts,
						connection.WithMonitor(func(*event.CommandMonitor) *event.CommandMonitor {
							return monitor
						}),
					)
				}),
			)
		}),
	}

	monitoredTopologyOnce.Do(func() {
		var err error
		monitoredTopology, err = topology.New(opts...)
		if err != nil {
			monitoredTopologyErr = err
		} else {
			monitoredTopology.Connect(context.Background())
			s, err := monitoredTopology.SelectServer(context.Background(), description.WriteSelector())
			require.NoError(t, err)

			c, err := s.Connection(context.Background())
			require.NoError(t, err)

			_, err = (&command.Write{
				DB:      DBName(t),
				Command: bsonx.Doc{{"dropDatabase", bsonx.Int32(1)}},
			}).RoundTrip(context.Background(), s.SelectedDescription(), c)

			require.NoError(t, err)
		}
	})

	if monitoredTopologyErr != nil {
		t.Fatal(monitoredTopologyErr)
	}

	return monitoredTopology
}

// Topology gets the globally configured topology.
func Topology(t *testing.T) *topology.Topology {
	cs := ConnString(t)
	liveTopologyOnce.Do(func() {
		var err error
		liveTopology, err = topology.New(topology.WithConnString(func(connstring.ConnString) connstring.ConnString { return cs }))
		if err != nil {
			liveTopologyErr = err
		} else {
			liveTopology.Connect(context.Background())
			s, err := liveTopology.SelectServer(context.Background(), description.WriteSelector())
			require.NoError(t, err)

			c, err := s.Connection(context.Background())
			require.NoError(t, err)

			_, err = (&command.Write{
				DB:      DBName(t),
				Command: bsonx.Doc{{"dropDatabase", bsonx.Int32(1)}},
			}).RoundTrip(context.Background(), s.SelectedDescription(), c)
			require.NoError(t, err)
		}
	})

	if liveTopologyErr != nil {
		t.Fatal(liveTopologyErr)
	}

	return liveTopology
}

// TopologyWithConnString takes a connection string and returns a connected
// topology, or else bails out of testing
func TopologyWithConnString(t *testing.T, cs connstring.ConnString) *topology.Topology {
	topology, err := topology.New(topology.WithConnString(func(connstring.ConnString) connstring.ConnString { return cs }))
	if err != nil {
		t.Fatal("Could not construct topology")
	}
	err = topology.Connect(context.Background())
	if err != nil {
		t.Fatal("Could not start topology connection")
	}
	return topology
}

// ColName gets a collection name that should be unique
// to the currently executing test.
func ColName(t *testing.T) string {
	// Get this indirectly to avoid copying a mutex
	v := reflect.Indirect(reflect.ValueOf(t))
	name := v.FieldByName("name")
	return name.String()
}

// ConnString gets the globally configured connection string.
func ConnString(t *testing.T) connstring.ConnString {
	connectionStringOnce.Do(func() {
		connectionString, connectionStringErr = GetConnString()
		mongerdbURI := os.Getenv("MONGODB_URI")
		if mongerdbURI == "" {
			mongerdbURI = "mongerdb://localhost:27017"
		}

		mongerdbURI = AddTLSConfigToURI(mongerdbURI)
		mongerdbURI = AddCompressorToUri(mongerdbURI)

		var err error
		connectionString, err = connstring.Parse(mongerdbURI)
		if err != nil {
			connectionStringErr = err
		}
	})
	if connectionStringErr != nil {
		t.Fatal(connectionStringErr)
	}

	return connectionString
}

func GetConnString() (connstring.ConnString, error) {
	mongerdbURI := os.Getenv("MONGODB_URI")
	if mongerdbURI == "" {
		mongerdbURI = "mongerdb://localhost:27017"
	}

	mongerdbURI = AddTLSConfigToURI(mongerdbURI)

	cs, err := connstring.Parse(mongerdbURI)
	if err != nil {
		return connstring.ConnString{}, err
	}

	return cs, nil
}

// DBName gets the globally configured database name.
func DBName(t *testing.T) string {
	return GetDBName(ConnString(t))
}

func GetDBName(cs connstring.ConnString) string {
	if cs.Database != "" {
		return cs.Database
	}

	return fmt.Sprintf("monger-go-driver-%d", os.Getpid())
}

// Integration should be called at the beginning of integration
// tests to ensure that they are skipped if integration testing is
// turned off.
func Integration(t *testing.T) {
	if testing.Short() {
		t.Skip("skipping integration test in short mode")
	}
}

// compareVersions compares two version number strings (i.e. positive integers separated by
// periods). Comparisons are done to the lesser precision of the two versions. For example, 3.2 is
// considered equal to 3.2.11, whereas 3.2.0 is considered less than 3.2.11.
//
// Returns a positive int if version1 is greater than version2, a negative int if version1 is less
// than version2, and 0 if version1 is equal to version2.
func CompareVersions(t *testing.T, v1 string, v2 string) int {
	n1 := strings.Split(v1, ".")
	n2 := strings.Split(v2, ".")

	for i := 0; i < int(math.Min(float64(len(n1)), float64(len(n2)))); i++ {
		i1, err := strconv.Atoi(n1[i])
		require.NoError(t, err)

		i2, err := strconv.Atoi(n2[i])
		require.NoError(t, err)

		difference := i1 - i2
		if difference != 0 {
			return difference
		}
	}

	return 0
}
