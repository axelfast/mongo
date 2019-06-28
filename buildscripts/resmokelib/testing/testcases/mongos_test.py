"""The unittest.TestCase for mongers --test."""

from . import interface
from ... import config
from ... import core
from ... import utils


class MongersTestCase(interface.ProcessTestCase):
    """A TestCase which runs a mongers binary with the given parameters."""

    REGISTERED_NAME = "mongers_test"

    def __init__(self, logger, mongers_options):
        """Initialize the mongers test and saves the options."""

        self.mongers_executable = utils.default_if_none(config.MONGOS_EXECUTABLE,
                                                       config.DEFAULT_MONGOS_EXECUTABLE)
        # Use the executable as the test name.
        interface.ProcessTestCase.__init__(self, logger, "mongers test", self.mongers_executable)
        self.options = mongers_options.copy()

    def configure(self, fixture, *args, **kwargs):
        """Ensure the --test option is present in the mongers options."""

        interface.ProcessTestCase.configure(self, fixture, *args, **kwargs)
        # Always specify test option to ensure the mongers will terminate.
        if "test" not in self.options:
            self.options["test"] = ""

    def _make_process(self):
        return core.programs.mongers_program(self.logger, executable=self.mongers_executable,
                                            **self.options)
