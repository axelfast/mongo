"""The unittest.TestCase for MQL MongerD Model tests."""

import os
import os.path

from buildscripts.resmokelib import config
from buildscripts.resmokelib import core
from buildscripts.resmokelib import utils
from buildscripts.resmokelib.testing.testcases import jsrunnerfile


class MqlModelMongerdTestCase(jsrunnerfile.JSRunnerFileTestCase):
    """A MQL MongerD Model test to execute."""

    REGISTERED_NAME = "mql_model_mongerd_test"

    def __init__(self, logger, json_filename, shell_executable=None, shell_options=None):
        """Initialize the MqlModelMongerdTestCase with the JSON test file."""

        jsrunnerfile.JSRunnerFileTestCase.__init__(
            self, logger, "MQL MongerD Model test", json_filename,
            test_runner_file="jstests/libs/mql_model_mongerd_test_runner.js",
            shell_executable=shell_executable, shell_options=shell_options)

    @property
    def json_filename(self):
        """Get the JSON filename."""
        return self.test_name

    def _populate_test_data(self, test_data):
        test_data["mqlTestFile"] = self.json_filename

        top_level_dirname = os.path.normpath(self.json_filename).split(os.sep)[0]

        # We join() with an empty string to include a trailing slash.
        test_data["mqlRootPath"] = os.path.join(top_level_dirname, "")
