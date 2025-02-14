Contributing to the MongerDB Tools Project
===================================

Pull requests are always welcome, and the MongerDB engineering team appreciates any help the community can give to make the MongerDB tools better.

For any particular improvement you want to make, you can begin a discussion on the
[MongerDB Developers Forum](https://groups.google.com/forum/?fromgroups#!forum/mongerdb-dev).  This is the best place to discuss your proposed improvement (and its
implementation) with the core development team.

If you're interested in contributing, we have a list of some suggested tickets that are easy enough to get started on [here](https://jira.mongerdb.org/issues/?jql=project%20%3D%20TOOLS%20AND%20labels%20%3D%20community%20and%20status%20%3D%20open)

Getting Started
---------------

1. Create a [MongerDB JIRA account](https://jira.mongerdb.org/secure/Signup!default.jspa).
2. Create a [Github account](https://github.com/signup/free).
3. [Fork](https://help.github.com/articles/fork-a-repo/) the repository on Github at https://github.com/mongerdb/monger-tools.
4. For more details see http://www.mongerdb.org/about/contributors/.
5. Submit a [pull request](https://help.github.com/articles/creating-a-pull-request/) against the project for review. Note: if you are a MongerDB engineer, please use the internal code review tool instead of github.

JIRA Tickets
------------

1. File a JIRA ticket in the [TOOLS project](https://jira.mongerdb.org/browse/TOOLS).
2. All commit messages to the MongerDB Tools repository must be prefaced with the relevant JIRA ticket number e.g. "TOOLS-XXX add support for xyz".

In filing JIRA tickets for bugs, please clearly describe the issue you are resolving, including the platforms on which the issue is present and clear steps to reproduce.

For improvements or feature requests, be sure to explain the goal or use case, and the approach
your solution will take.

Style Guide
-----------

All commits to the MongerDB Tools repository must pass golint:

```go run vendor/src/github.com/3rf/monger-lint/golint/golint.go monger* bson* common/*```

_We use a modified version of [golint](https://github.com/golang/lint)_

Testing
-------

To run unit and integration tests:

```
go test -v ./... 
```
If TOOLS_TESTING_UNIT is set to "true" in the environment, unit tests will run.
If TOOLS_TESTING_INTEGRATION is set to "true" in the environment, integration tests will run.

Integration tests require a `mongerd` (running on port 33333) while unit tests do not.

To run the quality assurance tests, you need to have the latest stable version of the rebuilt tools, `mongerd`, `mongers`, and `monger` in your current working directory. 

```
cd test/qa-tests
python buildscripts/smoke.py bson export files import oplog restore stat top
```
_Some tests require older binaries that are named accordingly (e.g. `mongerd-2.4`, `mongerd-2.6`, etc). You can use [setup_multiversion_mongerdb.py](test/qa-tests/buildscripts/setup_multiversion_mongerdb.py) to download those binaries_
