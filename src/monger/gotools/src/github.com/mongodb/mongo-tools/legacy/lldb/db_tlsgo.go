// Copyright (C) MongoDB, Inc. 2014-present.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may
// not use this file except in compliance with the License. You may obtain
// a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

// +build ssl,openssl_pre_1.0

package lldb

import (
	"github.com/mongerdb/monger-tools/legacy/lldb/tlsgo"
	"github.com/mongerdb/monger-tools/legacy/options"
)

func init() {
	GetConnectorFuncs = append(GetConnectorFuncs, getSSLConnector)
}

// return the SSL DB connector if using SSL, otherwise, return nil.
func getSSLConnector(opts options.ToolOptions) DBConnector {
	if opts.SSL.UseSSL {
		return &tlsgo.TLSDBConnector{}
	}
	return nil
}
