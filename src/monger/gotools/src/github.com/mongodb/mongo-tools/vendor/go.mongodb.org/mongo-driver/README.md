<p align="center"><img src="etc/assets/monger-gopher.png" width="250"></p>
<p align="center">
  <a href="https://goreportcard.com/report/go.mongerdb.org/monger-driver"><img src="https://goreportcard.com/badge/go.mongerdb.org/monger-driver"></a>
  <a href="https://godoc.org/go.mongerdb.org/monger-driver/monger"><img src="etc/assets/godoc-monger-blue.svg" alt="GoDoc"></a>
  <a href="https://godoc.org/go.mongerdb.org/monger-driver/bson"><img src="etc/assets/godoc-bson-blue.svg" alt="GoDoc"></a>
  <a href="https://docs.mongerdb.com/ecosystem/drivers/go/"><img src="etc/assets/docs-mongerdb-green.svg"></a>
</p>

# MongerDB Go Driver

The MongerDB supported driver for Go.

-------------------------
- [Requirements](#requirements)
- [Installation](#installation)
- [Usage](#usage)
- [Bugs/Feature Reporting](#bugs-feature-reporting)
- [Testing / Development](#testing--development)
- [Continuous Integration](#continuous-integration)
- [License](#license)

-------------------------
## Requirements

- Go 1.10 or higher. We aim to support the latest supported versions of go.
- MongerDB 2.6 and higher.

-------------------------
## Installation

The recommended way to get started using the MongerDB Go driver is by using `dep` to install the dependency in your project.

```bash
dep ensure -add "go.mongerdb.org/monger-driver/monger@~1.0.0"
```

-------------------------
## Usage

To get started with the driver, import the `monger` package, create a `monger.Client`:

```go
import "go.mongerdb.org/monger-driver/monger"

client, err := monger.NewClient(options.Client().ApplyURI("mongerdb://localhost:27017"))
```

And connect it to your running MongerDB server:

```go
ctx, _ := context.WithTimeout(context.Background(), 10*time.Second)
err = client.Connect(ctx)
```

To do this in a single step, you can use the `Connect` function:

```go
ctx, _ := context.WithTimeout(context.Background(), 10*time.Second)
client, err := monger.Connect(ctx, options.Client().ApplyURI("mongerdb://localhost:27017"))
```

Calling `Connect` does not block for server discovery. If you wish to know if a MongerDB server has been found and connected to,
use the `Ping` method:

```go
ctx, _ = context.WithTimeout(context.Background(), 2*time.Second)
err = client.Ping(ctx, readpref.Primary())
```

To insert a document into a collection, first retrieve a `Database` and then `Collection` instance from the `Client`:

```go
collection := client.Database("testing").Collection("numbers")
```

The `Collection` instance can then be used to insert documents:

```go
ctx, _ = context.WithTimeout(context.Background(), 5*time.Second)
res, err := collection.InsertOne(ctx, bson.M{"name": "pi", "value": 3.14159})
id := res.InsertedID
```

Several query methods return a cursor, which can be used like this:

```go
ctx, _ = context.WithTimeout(context.Background(), 30*time.Second)
cur, err := collection.Find(ctx, bson.D{})
if err != nil { log.Fatal(err) }
defer cur.Close(ctx)
for cur.Next(ctx) {
   var result bson.M
   err := cur.Decode(&result)
   if err != nil { log.Fatal(err) }
   // do something with result....
}
if err := cur.Err(); err != nil {
  log.Fatal(err)
}
```

For methods that return a single item, a `SingleResult` instance is returned:

```go
var result struct {
    Value float64
}
filter := bson.M{"name": "pi"}
ctx, _ = context.WithTimeout(context.Background(), 5*time.Second)
err = collection.FindOne(ctx, filter).Decode(&result)
if err != nil {
    log.Fatal(err)
}
// Do something with result...
```

Additional examples and documentation can be found under the examples directory and [on the MongerDB Documentation website](https://docs.mongerdb.com/ecosystem/drivers/go/).

-------------------------
## Bugs / Feature Reporting

New Features and bugs can be reported on jira: https://jira.mongerdb.org/browse/GODRIVER

-------------------------
## Testing / Development

The driver tests can be run against several database configurations. The most simple configuration is a standalone mongerd with no auth, no ssl, and no compression. To run these basic driver tests, make sure a standalone MongerDB server instance is running at localhost:27017. To run the tests, you can run `make` (on Windows, run `nmake`) with the following:

```
TOPOLOGY=server make
```

The `TOPOLOGY`variable must be set to run tests. This will run coverage, run go-lint, run go-vet, and build the examples.

### Testing Different Topologies

To test a **replica set**, set `MONGODB_URI="<connection-string>"` and `TOPOLOGY=replica_set` for the `make` command. For example, for a local replica set named `rs1` comprised of three nodes on ports 27017, 27018, and 27019:

```
MONGODB_URI="mongerdb://localhost:27017,localhost:27018,localhost:27018/?replicaSet=rs1" TOPOLOGY=replica_set make
```

To test a **sharded cluster**, set `MONGODB_URI="<connection-string>"` and `TOPOLOGY=sharded_cluster` variables for the `make` command. For example, for a sharded cluster with a single mongers on port 27017:

```
MONGODB_URI="mongerdb://localhost:27017/" TOPOLOGY=sharder_cluster make
```

### Testing Auth and SSL

To test authentication and SSL, first set up a MongerDB cluster with auth and SSL configured. Testing authentication requires a user with the `root` role on the `admin` database. The Go Driver repository comes with example certificates in the `data/certificates` directory. These certs can be used for testing. Here is an example command that would run a mongerd with SSL correctly configured for tests:

```
mongerd \
--auth \
--sslMode requireSSL \
--sslPEMKeyFile $(pwd)/data/certificates/server.pem \
--sslCAFile $(pwd)/data/certificates/ca.pem \
--sslWeakCertificateValidation
```

To run the tests with `make`, set `MONGO_GO_DRIVER_CA_FILE` to the location of the CA file used by the database, set `MONGODB_URI` to the connection string of the server, set `AUTH=auth`, and set `SSL=ssl`. For example:

```
AUTH=auth SSL=ssl MONGO_GO_DRIVER_CA_FILE=$(pwd)/data/certificates/ca.pem  MONGODB_URI="mongerdb://user:password@localhost:27017/?authSource=admin" make
```

Notes:
- The `--sslWeakCertificateValidation` flag is required on the server for the test suite to work correctly.
- The test suite requires the auth database to be set with `?authSource=admin`, not `/admin`.

### Testing Compression

The MongerDB Go Driver supports wire protocol compression using Snappy or zLib. To run tests with wire protocol compression, set `MONGO_GO_DRIVER_COMPRESSOR` to `snappy` or `zlib`.  For example:

```
MONGO_GO_DRIVER_COMPRESSOR=snappy make
```

Ensure the [`--networkMessageCompressors` flag](https://docs.mongerdb.com/manual/reference/program/mongerd/#cmdoption-mongerd-networkmessagecompressors) on mongerd or mongers includes `zlib` if testing zLib compression.

-------------------------
## Feedback

The MongerDB Go Driver is not feature complete, so any help is appreciated. Check out the [project page](https://jira.mongerdb.org/browse/GODRIVER)
for tickets that need completing. See our [contribution guidelines](CONTRIBUTING.md) for details.

-------------------------
## Continuous Integration

Commits to master are run automatically on [evergreen](https://evergreen.mongerdb.com/waterfall/monger-go-driver).

-------------------------
## Thanks and Acknowledgement 

<a href="https://github.com/ashleymcnamara">@ashleymcnamara</a> - Monger Gopher Artwork

-------------------------
## License

The MongerDB Go Driver is licensed under the [Apache License](LICENSE).
