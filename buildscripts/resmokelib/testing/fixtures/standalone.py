"""Standalone mongerd fixture for executing JSTests against."""

import os
import os.path
import time

import pymonger
import pymonger.errors

from . import interface
from ... import config
from ... import core
from ... import errors
from ... import utils


class MongerDFixture(interface.Fixture):
    """Fixture which provides JSTests with a standalone mongerd to run against."""

    AWAIT_READY_TIMEOUT_SECS = 300

    def __init__(  # pylint: disable=too-many-arguments
            self, logger, job_num, mongerd_executable=None, mongerd_options=None, dbpath_prefix=None,
            preserve_dbpath=False):
        """Initialize MongerDFixture with different options for the mongerd process."""

        interface.Fixture.__init__(self, logger, job_num, dbpath_prefix=dbpath_prefix)

        if "dbpath" in mongerd_options and dbpath_prefix is not None:
            raise ValueError("Cannot specify both mongerd_options.dbpath and dbpath_prefix")

        # Command line options override the YAML configuration.
        self.mongerd_executable = utils.default_if_none(config.MONGOD_EXECUTABLE, mongerd_executable)

        self.mongerd_options = utils.default_if_none(mongerd_options, {}).copy()
        self.preserve_dbpath = preserve_dbpath

        # The dbpath in mongerd_options takes precedence over other settings to make it easier for
        # users to specify a dbpath containing data to test against.
        if "dbpath" not in self.mongerd_options:
            self.mongerd_options["dbpath"] = os.path.join(self._dbpath_prefix, config.FIXTURE_SUBDIR)
        self._dbpath = self.mongerd_options["dbpath"]

        self.mongerd = None
        self.port = None

    def setup(self):
        """Set up the mongerd."""
        if not self.preserve_dbpath and os.path.lexists(self._dbpath):
            utils.rmtree(self._dbpath, ignore_errors=False)

        try:
            os.makedirs(self._dbpath)
        except os.error:
            # Directory already exists.
            pass

        if "port" not in self.mongerd_options:
            self.mongerd_options["port"] = core.network.PortAllocator.next_fixture_port(self.job_num)
        self.port = self.mongerd_options["port"]

        mongerd = core.programs.mongerd_program(self.logger, executable=self.mongerd_executable,
                                              **self.mongerd_options)
        try:
            self.logger.info("Starting mongerd on port %d...\n%s", self.port, mongerd.as_command())
            mongerd.start()
            self.logger.info("mongerd started on port %d with pid %d.", self.port, mongerd.pid)
        except Exception as err:
            msg = "Failed to start mongerd on port {:d}: {}".format(self.port, err)
            self.logger.exception(msg)
            raise errors.ServerFailure(msg)

        self.mongerd = mongerd

    def await_ready(self):
        """Block until the fixture can be used for testing."""
        deadline = time.time() + MongerDFixture.AWAIT_READY_TIMEOUT_SECS

        # Wait until the mongerd is accepting connections. The retry logic is necessary to support
        # versions of PyMonger <3.0 that immediately raise a ConnectionFailure if a connection cannot
        # be established.
        while True:
            # Check whether the mongerd exited for some reason.
            exit_code = self.mongerd.poll()
            if exit_code is not None:
                raise errors.ServerFailure("Could not connect to mongerd on port {}, process ended"
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
                        "Failed to connect to mongerd on port {} after {} seconds".format(
                            self.port, MongerDFixture.AWAIT_READY_TIMEOUT_SECS))

                self.logger.info("Waiting to connect to mongerd on port %d.", self.port)
                time.sleep(0.1)  # Wait a little bit before trying again.

        self.logger.info("Successfully contacted the mongerd on port %d.", self.port)

    def _do_teardown(self):
        if self.mongerd is None:
            self.logger.warning("The mongerd fixture has not been set up yet.")
            return  # Still a success even if nothing is running.

        self.logger.info("Stopping mongerd on port %d with pid %d...", self.port, self.mongerd.pid)
        if not self.is_running():
            exit_code = self.mongerd.poll()
            msg = ("mongerd on port {:d} was expected to be running, but wasn't. "
                   "Process exited with code {:d}.").format(self.port, exit_code)
            self.logger.warning(msg)
            raise errors.ServerFailure(msg)

        self.mongerd.stop()
        exit_code = self.mongerd.wait()

        if exit_code == 0:
            self.logger.info("Successfully stopped the mongerd on port {:d}.".format(self.port))
        else:
            self.logger.warning("Stopped the mongerd on port {:d}. "
                                "Process exited with code {:d}.".format(self.port, exit_code))
            raise errors.ServerFailure(
                "mongerd on port {:d} with pid {:d} exited with code {:d}".format(
                    self.port, self.mongerd.pid, exit_code))

    def is_running(self):
        """Return true if the mongerd is still operating."""
        return self.mongerd is not None and self.mongerd.poll() is None

    def get_dbpath_prefix(self):
        """Return the _dbpath, as this is the root of the data directory."""
        return self._dbpath

    def get_internal_connection_string(self):
        """Return the internal connection string."""
        if self.mongerd is None:
            raise ValueError("Must call setup() before calling get_internal_connection_string()")

        return "localhost:%d" % self.port

    def get_driver_connection_url(self):
        """Return the driver connection URL."""
        return "mongerdb://" + self.get_internal_connection_string()
