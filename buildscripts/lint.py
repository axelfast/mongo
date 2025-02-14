"""Lint module."""

import codecs
import os
import sys

# Get relative imports to work when the package is not installed on the PYTHONPATH.
if __name__ == "__main__" and __package__ is None:
    sys.path.append(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

import buildscripts.cpplint as cpplint  # pylint: disable=wrong-import-position
import buildscripts.utils as utils  # pylint: disable=wrong-import-position


class CheckForConfigH(object):
    """CheckForConfigH class."""

    def __init__(self):
        """Initialize CheckForConfigH."""
        self.found_configh = False

    def __call__(self, filename, clean_lines, line_num, error):
        """Check for a config file."""
        if self.found_configh:
            return

        cur_line = clean_lines.elided[line_num]
        self.found_configh = cur_line.startswith('#include "monger/config.h"')

        if not self.found_configh and "MONGO_CONFIG_" in cur_line:
            error(filename, line_num, 'build/config_h_include', 5,
                  'MONGO_CONFIG define used without prior inclusion of config.h.')


def run_lint(paths, nudge_on=False):  # pylint: disable=too-many-statements
    """Run lint."""
    # errors are as of 10/14
    # idea is not to let it any new type of error
    # as we knock one out, we should remove line
    # note: not all of these are things we want, so please check first

    nudge = []  # things we'd like to turn on sson, so don't make worse
    later = []  # things that are unlikely anytime soon, so meh
    never = []  # things we totally disagree with

    nudge.append('-build/c++11')  # errors found: 6
    never.append('-build/header_guard')  # errors found: 345
    nudge.append('-build/include')  # errors found: 924
    nudge.append('-build/include_order')  # errors found: 511
    nudge.append('-build/include_what_you_use')  # errors found: 986
    nudge.append('-build/namespaces')  # errors found: 131
    never.append('-readability/braces')  # errors found: 880
    later.append('-readability/casting')  # errors found: 748
    nudge.append('-readability/check')  # errors found: 7
    nudge.append('-readability/fn_size')  # errors found: 1
    nudge.append('-readability/function')  # errors found: 49
    nudge.append('-readability/inheritance')  # errors found: 7
    nudge.append('-readability/multiline_comment')  # errors found: 1
    later.append('-readability/namespace')  # errors found: 876
    later.append('-readability/streams')  # errors found: 72
    later.append('-readability/todo')  # errors found: 309
    nudge.append('-runtime/arrays')  # errors found: 5
    later.append('-runtime/explicit')  # errors found: 322
    never.append('-runtime/indentation_namespace')  # errors found: 4601
    later.append('-runtime/int')  # errors found: 1420
    later.append('-runtime/printf')  # errors found: 29
    nudge.append('-runtime/references')  # errors found: 1338
    nudge.append('-runtime/string')  # errors found: 6
    nudge.append('-runtime/threadsafe_fn')  # errors found: 46
    never.append('-whitespace/blank_line')  # errors found: 2080
    never.append('-whitespace/braces')  # errors found: 962
    later.append('-whitespace/comma')  # errors found: 621
    later.append('-whitespace/comments')  # errors found: 2189
    nudge.append('-whitespace/empty_loop_body')  # errors found: 19
    later.append('-whitespace/end_of_line')  # errors found: 4340
    later.append('-whitespace/line_length')  # errors found: 14500
    never.append('-whitespace/indent')  # errors found: 4108
    later.append('-whitespace/newline')  # errors found: 1520
    nudge.append('-whitespace/operators')  # errors found: 2297
    never.append('-whitespace/parens')  # errors found: 49058
    nudge.append('-whitespace/semicolon')  # errors found: 121
    nudge.append('-whitespace/tab')  # errors found: 233

    filters = later + never
    if not nudge_on:
        filters = filters + nudge

    source_files = []
    for path in paths:
        utils.get_all_source_files(source_files, path)

    args = \
        ["--linelength=100", "--filter=" + ",".join(filters), "--counting=detailed"] + source_files
    filenames = cpplint.ParseArguments(args)

    def _our_is_test_filename(file_name):
        if file_name.find("dbtests") >= 0:
            return True
        if file_name.endswith("_test.cpp"):
            return True
        return False

    # pylint: disable=protected-access
    cpplint._IsTestFilename = _our_is_test_filename

    cpplint._cpplint_state.ResetErrorCounts()
    for filename in filenames:
        config_h_check_obj = CheckForConfigH()
        cpplint.ProcessFile(filename, cpplint._cpplint_state.verbose_level,
                            extra_check_functions=[config_h_check_obj])
    cpplint._cpplint_state.PrintErrorCounts()

    return cpplint._cpplint_state.error_count == 0
    # pylint: enable=protected-access


def main():
    """Execute Main program."""

    paths = []
    nudge = False

    for arg in sys.argv[1:]:
        if arg.startswith("--"):
            arg = arg[2:]
            if arg == "nudge":
                nudge = True
                continue
            else:
                print("unknown arg [%s]" % arg)
                sys.exit(-1)
        paths.append(arg)

    if not paths:
        paths.append("src/monger/")

    if not run_lint(paths, nudge):
        sys.exit(-1)


if __name__ == "__main__":
    main()
