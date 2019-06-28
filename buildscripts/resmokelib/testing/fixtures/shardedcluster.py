"""Sharded cluster fixture for executing JSTests against."""

import os.path
import time

import pymonger
import pymonger.errors

from . import interface
from . import standalone
from . import replicaset
from ... import config
from ... import core
from ... import errors
from ... import utils
from ...utils import registry


class ShardedClusterFixture(interface.Fixture):  # pylint: disable=too-many-instance-attributes
    """Fixture which provides JSTests with a sharded cluster to run against."""

    _CONFIGSVR_REPLSET_NAME = "config-rs"
    _SHARD_REPLSET_NAME_PREFIX = "shard-rs"

    def __init__(  # pylint: disable=too-many-arguments,too-many-locals
            self, logger, job_num, mongers_executable=None, mongers_options=None,
            mongerd_executable=None, mongerd_options=None, dbpath_prefix=None, preserve_dbpath=False,
            num_shards=1, num_rs_nodes_per_shard=None, num_mongers=1, enable_sharding=None,
            enable_balancer=True, enable_autosplit=True, auth_options=None, configsvr_options=None,
            shard_options=None):
        """Initialize ShardedClusterFixture with different options for the cluster processes."""

        interface.Fixture.__init__(self, logger, job_num, dbpath_prefix=dbpath_prefix)

        if "dbpath" in mongerd_options:
            raise ValueError("Cannot specify mongerd_options.dbpath")

        self.mongers_executable = mongers_executable
        self.mongers_options = utils.default_if_none(mongers_options, {})
        self.mongerd_executable = mongerd_executable
        self.mongerd_options = utils.default_if_none(mongerd_options, {})
        self.mongerd_options["set_parameters"] = mongerd_options.get("set_parameters", {}).copy()
        self.mongerd_options["set_parameters"]["migrationLockAcquisitionMaxWaitMS"] = \
                mongerd_options["set_parameters"].get("migrationLockAcquisitionMaxWaitMS", 30000)
        self.preserve_dbpath = preserve_dbpath
        self.num_shards = num_shards
        self.num_rs_nodes_per_shard = num_rs_nodes_per_shard
        self.num_mongers = num_mongers
        self.enable_sharding = utils.default_if_none(enable_sharding, [])
        self.enable_balancer = enable_balancer
        self.enable_autosplit = enable_autosplit
        self.auth_options = auth_options
        self.configsvr_options = utils.default_if_none(configsvr_options, {})
        self.shard_options = utils.default_if_none(shard_options, {})

        self._dbpath_prefix = os.path.join(self._dbpath_prefix, config.FIXTURE_SUBDIR)

        self.configsvr = None
        self.mongers = []
        self.shards = []

    def setup(self):
        """Set up the sharded cluster."""
        if self.configsvr is None:
            self.configsvr = self._new_configsvr()

        self.configsvr.setup()

        if not self.shards:
            for i in range(self.num_shards):
                if self.num_rs_nodes_per_shard is None:
                    shard = self._new_standalone_shard(i)
                elif isinstance(self.num_rs_nodes_per_shard, int):
                    if self.num_rs_nodes_per_shard <= 0:
                        raise ValueError("num_rs_nodes_per_shard must be a positive integer")
                    shard = self._new_rs_shard(i, self.num_rs_nodes_per_shard)
                else:
                    raise TypeError("num_rs_nodes_per_shard must be an integer or None")
                self.shards.append(shard)

        # Start up each of the shards
        for shard in self.shards:
            shard.setup()

    def await_ready(self):
        """Block until the fixture can be used for testing."""
        # Wait for the config server
        if self.configsvr is not None:
            self.configsvr.await_ready()

        # Wait for each of the shards
        for shard in self.shards:
            shard.await_ready()

        # We call self._new_mongers() and mongers.setup() in self.await_ready() function
        # instead of self.setup() because mongers routers have to connect to a running cluster.
        if not self.mongers:
            for i in range(self.num_mongers):
                mongers = self._new_mongers(i, self.num_mongers)
                self.mongers.append(mongers)

        for mongers in self.mongers:
            # Start up the mongers.
            mongers.setup()

            # Wait for the mongers.
            mongers.await_ready()

        client = self.monger_client()
        self._auth_to_db(client)

        # Turn off the balancer if it is not meant to be enabled.
        if not self.enable_balancer:
            self.stop_balancer()

        # Turn off autosplit if it is not meant to be enabled.
        if not self.enable_autosplit:
            wc = pymonger.WriteConcern(w="majority", wtimeout=30000)
            coll = client.config.get_collection("settings", write_concern=wc)
            coll.update_one({"_id": "autosplit"}, {"$set": {"enabled": False}}, upsert=True)

        # Inform mongers about each of the shards
        for shard in self.shards:
            self._add_shard(client, shard)

        # Ensure that all CSRS nodes are up to date. This is strictly needed for tests that use
        # multiple mongerses. In those cases, the first mongers initializes the contents of the config
        # database, but without waiting for those writes to replicate to all the config servers then
        # the secondary mongerses risk reading from a stale config server and seeing an empty config
        # database.
        self.configsvr.await_last_op_committed()

        # Enable sharding on each of the specified databases
        for db_name in self.enable_sharding:
            self.logger.info("Enabling sharding for '%s' database...", db_name)
            client.admin.command({"enablesharding": db_name})

        # Ensure that the sessions collection gets auto-sharded by the config server
        if self.configsvr is not None:
            primary = self.configsvr.get_primary().monger_client()
            primary.admin.command({"refreshLogicalSessionCacheNow": 1})

    def _auth_to_db(self, client):
        """Authenticate client for the 'authenticationDatabase'."""
        if self.auth_options is not None:
            auth_db = client[self.auth_options["authenticationDatabase"]]
            auth_db.authenticate(self.auth_options["username"],
                                 password=self.auth_options["password"],
                                 mechanism=self.auth_options["authenticationMechanism"])

    def stop_balancer(self, timeout_ms=60000):
        """Stop the balancer."""
        client = self.monger_client()
        self._auth_to_db(client)
        client.admin.command({"balancerStop": 1}, maxTimeMS=timeout_ms)
        self.logger.info("Stopped the balancer")

    def start_balancer(self, timeout_ms=60000):
        """Start the balancer."""
        client = self.monger_client()
        self._auth_to_db(client)
        client.admin.command({"balancerStart": 1}, maxTimeMS=timeout_ms)
        self.logger.info("Started the balancer")

    def _do_teardown(self):
        """Shut down the sharded cluster."""
        self.logger.info("Stopping all members of the sharded cluster...")

        running_at_start = self.is_running()
        if not running_at_start:
            self.logger.warning("All members of the sharded cluster were expected to be running, "
                                "but weren't.")

        if self.enable_balancer:
            self.stop_balancer()

        teardown_handler = interface.FixtureTeardownHandler(self.logger)

        if self.configsvr is not None:
            teardown_handler.teardown(self.configsvr, "config server")

        for mongers in self.mongers:
            teardown_handler.teardown(mongers, "mongers")

        for shard in self.shards:
            teardown_handler.teardown(shard, "shard")

        if teardown_handler.was_successful():
            self.logger.info("Successfully stopped all members of the sharded cluster.")
        else:
            self.logger.error("Stopping the sharded cluster fixture failed.")
            raise errors.ServerFailure(teardown_handler.get_error_message())

    def is_running(self):
        """Return true if all nodes in the cluster are all still operating."""
        return (self.configsvr is not None and self.configsvr.is_running()
                and all(shard.is_running() for shard in self.shards)
                and all(mongers.is_running() for mongers in self.mongers))

    def get_internal_connection_string(self):
        """Return the internal connection string."""
        if self.mongers is None:
            raise ValueError("Must call setup() before calling get_internal_connection_string()")

        return ",".join([mongers.get_internal_connection_string() for mongers in self.mongers])

    def get_driver_connection_url(self):
        """Return the driver connection URL."""
        return "mongerdb://" + self.get_internal_connection_string()

    def _new_configsvr(self):
        """Return a replicaset.ReplicaSetFixture configured as the config server."""

        mongerd_logger = self.logger.new_fixture_node_logger("configsvr")

        configsvr_options = self.configsvr_options.copy()

        auth_options = configsvr_options.pop("auth_options", self.auth_options)
        mongerd_executable = configsvr_options.pop("mongerd_executable", self.mongerd_executable)
        preserve_dbpath = configsvr_options.pop("preserve_dbpath", self.preserve_dbpath)
        num_nodes = configsvr_options.pop("num_nodes", 1)

        replset_config_options = configsvr_options.pop("replset_config_options", {})
        replset_config_options["configsvr"] = True

        mongerd_options = self.mongerd_options.copy()
        mongerd_options.update(configsvr_options.pop("mongerd_options", {}))
        mongerd_options["configsvr"] = ""
        mongerd_options["dbpath"] = os.path.join(self._dbpath_prefix, "config")
        mongerd_options["replSet"] = ShardedClusterFixture._CONFIGSVR_REPLSET_NAME
        mongerd_options["storageEngine"] = "wiredTiger"

        return replicaset.ReplicaSetFixture(
            mongerd_logger, self.job_num, mongerd_executable=mongerd_executable,
            mongerd_options=mongerd_options, preserve_dbpath=preserve_dbpath, num_nodes=num_nodes,
            auth_options=auth_options, replset_config_options=replset_config_options,
            **configsvr_options)

    def _new_rs_shard(self, index, num_rs_nodes_per_shard):
        """Return a replicaset.ReplicaSetFixture configured as a shard in a sharded cluster."""

        mongerd_logger = self.logger.new_fixture_node_logger("shard{}".format(index))

        shard_options = self.shard_options.copy()

        auth_options = shard_options.pop("auth_options", self.auth_options)
        mongerd_executable = shard_options.pop("mongerd_executable", self.mongerd_executable)
        preserve_dbpath = shard_options.pop("preserve_dbpath", self.preserve_dbpath)

        replset_config_options = shard_options.pop("replset_config_options", {})
        replset_config_options["configsvr"] = False

        mongerd_options = self.mongerd_options.copy()
        mongerd_options.update(shard_options.pop("mongerd_options", {}))
        mongerd_options["shardsvr"] = ""
        mongerd_options["dbpath"] = os.path.join(self._dbpath_prefix, "shard{}".format(index))
        mongerd_options["replSet"] = ShardedClusterFixture._SHARD_REPLSET_NAME_PREFIX + str(index)

        return replicaset.ReplicaSetFixture(
            mongerd_logger, self.job_num, mongerd_executable=mongerd_executable,
            mongerd_options=mongerd_options, preserve_dbpath=preserve_dbpath,
            num_nodes=num_rs_nodes_per_shard, auth_options=auth_options,
            replset_config_options=replset_config_options, **shard_options)

    def _new_standalone_shard(self, index):
        """Return a standalone.MongerDFixture configured as a shard in a sharded cluster."""

        mongerd_logger = self.logger.new_fixture_node_logger("shard{}".format(index))

        shard_options = self.shard_options.copy()

        mongerd_executable = shard_options.pop("mongerd_executable", self.mongerd_executable)
        preserve_dbpath = shard_options.pop("preserve_dbpath", self.preserve_dbpath)

        mongerd_options = self.mongerd_options.copy()
        mongerd_options.update(shard_options.pop("mongerd_options", {}))
        mongerd_options["shardsvr"] = ""
        mongerd_options["dbpath"] = os.path.join(self._dbpath_prefix, "shard{}".format(index))

        return standalone.MongerDFixture(
            mongerd_logger, self.job_num, mongerd_executable=mongerd_executable,
            mongerd_options=mongerd_options, preserve_dbpath=preserve_dbpath, **shard_options)

    def _new_mongers(self, index, total):
        """
        Return a _MongerSFixture configured to be used as the mongers for a sharded cluster.

        :param index: The index of the current mongers.
        :param total: The total number of mongers routers
        :return: _MongerSFixture
        """

        if total == 1:
            logger_name = "mongers"
        else:
            logger_name = "mongers{}".format(index)

        mongers_logger = self.logger.new_fixture_node_logger(logger_name)

        mongers_options = self.mongers_options.copy()
        mongers_options["configdb"] = self.configsvr.get_internal_connection_string()

        return _MongerSFixture(mongers_logger, self.job_num, mongers_executable=self.mongers_executable,
                              mongers_options=mongers_options)

    def _add_shard(self, client, shard):
        """
        Add the specified program as a shard by executing the addShard command.

        See https://docs.mongerdb.org/manual/reference/command/addShard for more details.
        """

        connection_string = shard.get_internal_connection_string()
        self.logger.info("Adding %s as a shard...", connection_string)
        client.admin.command({"addShard": connection_string})


class _MongerSFixture(interface.Fixture):
    """Fixture which provides JSTests with a mongers to connect to."""

    REGISTERED_NAME = registry.LEAVE_UNREGISTERED  # type: ignore

    def __init__(self, logger, job_num, mongers_executable=None, mongers_options=None):
        """Initialize _MongerSFixture."""

        interface.Fixture.__init__(self, logger, job_num)

        # Command line options override the YAML configuration.
        self.mongers_executable = utils.default_if_none(config.MONGOS_EXECUTABLE, mongers_executable)

        self.mongers_options = utils.default_if_none(mongers_options, {}).copy()

        self.mongers = None
        self.port = None

    def setup(self):
        """Set up the sharded cluster."""
        if "port" not in self.mongers_options:
            self.mongers_options["port"] = core.network.PortAllocator.next_fixture_port(self.job_num)
        self.port = self.mongers_options["port"]

        mongers = core.programs.mongers_program(self.logger, executable=self.mongers_executable,
                                              **self.mongers_options)
        try:
            self.logger.info("Starting mongers on port %d...\n%s", self.port, mongers.as_command())
            mongers.start()
            self.logger.info("mongers started on port %d with pid %d.", self.port, mongers.pid)
        except Exception as err:
            msg = "Failed to start mongers on port {:d}: {}".format(self.port, err)
            self.logger.exception(msg)
            raise errors.ServerFailure(msg)

        self.mongers = mongers

    def await_ready(self):
        """Block until the fixture can be used for testing."""
        deadline = time.time() + standalone.MongerDFixture.AWAIT_READY_TIMEOUT_SECS

        # Wait until the mongers is accepting connections. The retry logic is necessary to support
        # versions of PyMonger <3.0 that immediately raise a ConnectionFailure if a connection cannot
        # be established.
        while True:
            # Check whether the mongers exited for some reason.
            exit_code = self.mongers.poll()
            if exit_code is not None:
                raise errors.ServerFailure("Could not connect to mongers on port {}, process ended"
                                           " unexpectedly with code {}.".format(
                                               self.port, exit_code))

            try:
                # Use a shorter connection timeout to more closely satisfy the requested deadline.
                client = self.monger_client(timeout_millis=500)
                client.admin.command("ping")
                break
            except pymonger.errors.ConnectionFailure:
                remaining = deadline - time.time()
                if remaining <= 0.0:
                    raise errors.ServerFailure(
                        "Failed to connect to mongers on port {} after {} seconds".format(
                            self.port, standalone.MongerDFixture.AWAIT_READY_TIMEOUT_SECS))

                self.logger.info("Waiting to connect to mongers on port %d.", self.port)
                time.sleep(0.1)  # Wait a little bit before trying again.

        self.logger.info("Successfully contacted the mongers on port %d.", self.port)

    def _do_teardown(self):
        if self.mongers is None:
            self.logger.warning("The mongers fixture has not been set up yet.")
            return  # Teardown is still a success even if nothing is running.

        self.logger.info("Stopping mongers on port %d with pid %d...", self.port, self.mongers.pid)
        if not self.is_running():
            exit_code = self.mongers.poll()
            msg = ("mongers on port {:d} was expected to be running, but wasn't. "
                   "Process exited with code {:d}").format(self.port, exit_code)
            self.logger.warning(msg)
            raise errors.ServerFailure(msg)

        self.mongers.stop()
        exit_code = self.mongers.wait()

        if exit_code == 0:
            self.logger.info("Successfully stopped the mongers on port {:d}".format(self.port))
        else:
            self.logger.warning("Stopped the mongers on port {:d}. "
                                "Process exited with code {:d}.".format(self.port, exit_code))
            raise errors.ServerFailure(
                "mongers on port {:d} with pid {:d} exited with code {:d}".format(
                    self.port, self.mongers.pid, exit_code))

    def is_running(self):
        """Return true if the cluster is still operating."""
        return self.mongers is not None and self.mongers.poll() is None

    def get_internal_connection_string(self):
        """Return the internal connection string."""
        if self.mongers is None:
            raise ValueError("Must call setup() before calling get_internal_connection_string()")

        return "localhost:%d" % self.port

    def get_driver_connection_url(self):
        """Return the driver connection URL."""
        return "mongerdb://" + self.get_internal_connection_string()
