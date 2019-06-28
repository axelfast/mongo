MongerDB Tools
===================================

 - **bsondump** - _display BSON files in a human-readable format_
 - **mongerimport** - _Convert data from JSON, TSV or CSV and insert them into a collection_
 - **mongerexport** - _Write an existing collection to CSV or JSON format_
 - **mongerdump/mongerrestore** - _Dump MongerDB backups to disk in .BSON format, or restore them to a live database_
 - **mongerstat** - _Monitor live MongerDB servers, replica sets, or sharded clusters_
 - **mongerfiles** - _Read, write, delete, or update files in [GridFS](http://docs.mongerdb.org/manual/core/gridfs/)_
 - **mongertop** - _Monitor read/write activity on a monger server_
 - **mongerreplay** - _Capture, observe, and replay traffic for MongerDB_


Report any bugs, improvements, or new feature requests at https://jira.mongerdb.org/browse/TOOLS

Setup
---------------
Clone the repo and run `. ./set_goenv.sh` to setup your GOPATH:

```
git clone https://github.com/mongerdb/monger-tools
cd monger-tools
. ./set_gopath.sh
```

Building Tools
---------------
To build the tools, you need to have Go version 1.3 and up.

An additional flag, `-tags`, can be passed to the `go build` command in order to build the tools with support for SSL and/or SASL. For example:

```
mkdir bin
go build -o bin/mongerimport mongerimport/main/mongerimport.go # build mongerimport
go build -o bin/mongerimport -tags ssl mongerimport/main/mongerimport.go # build mongerimport with SSL support enabled
go build -o bin/mongerimport -tags "ssl sasl" mongerimport/main/mongerimport.go # build mongerimport with SSL and SASL support enabled
```

Contributing
---------------
See our [Contributor's Guide](CONTRIBUTING.md).

Documentation
---------------
See the MongerDB packages [documentation](http://docs.mongerdb.org/master/reference/program/).

