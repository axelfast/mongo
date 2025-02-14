Building MongerDB
================

To build MongerDB, you will need:

* A modern C++ compiler capable of compiling C++17. One of the following is required:
    * GCC 8.0 or newer
    * Clang 7.0 (or Apple XCode 10.0 Clang) or newer
    * Visual Studio 2017 version 15.9 or newer (See Windows section below for details)
* On Linux and macOS, the libcurl library and header is required. MacOS includes libcurl.
    * Fedora/RHEL - `dnf install libcurl-devel`
    * Ubuntu/Debian - `libcurl-dev` is provided by three packages. Install one of them:
      * `libcurl4-openssl-dev`
      * `libcurl4-nss-dev`
      * `libcurl4-gnutls-dev`
* Python 3.7.x and Pip modules:
  * See the section "Python Prerequisites" below.
* About 13 GB of free disk space for the core binaries (`mongerd`, `mongers`, and `monger`) and about 600 GB for the all target.

MongerDB supports the following architectures: arm64, ppc64le, s390x, and x86-64.
More detailed platform instructions can be found below.


MongerDB Tools
--------------

The MongerDB command line tools (mongerdump, mongerrestore, mongerimport, mongerexport, etc)
have been rewritten in [Go](http://golang.org/) and are no longer included in this repository.

The source for the tools is now available at [mongerdb/monger-tools](https://github.com/mongerdb/monger-tools).

Python Prerequisites
---------------

In order to build MongerDB, Python 3.7.x is required, and several Python modules. To install
the required Python modules, run:

    $ pip3 install -r etc/pip/compile-requirements.txt

Note: If the `pip3` command is not available, `pip` without a suffix may be the pip command
associated with Python 3.7.x.

Note: In order to compile C-based Python modules, you'll also need the Python and OpenSSL C headers. Run:

* Fedora/RHEL - `dnf install python3-devel openssl-devel`
* Ubuntu/Debian - `apt-get install python3.7-dev libssl-dev`

SCons
---------------

For detail information about building, please see [the build manual](https://github.com/mongerdb/monger/wiki/Build-Mongerdb-From-Source)

If you want to build everything (mongerd, monger, tests, etc):

    $ python3 buildscripts/scons.py all

If you only want to build the database:

    $ python3 buildscripts/scons.py mongerd

***Note***: For C++ compilers that are newer than the supported version, the compiler may issue new warnings that cause MongerDB to fail to build since the build system treats compiler warnings as errors. To ignore the warnings, pass the switch `--disable-warnings-as-errors` to scons.

    $ python3 buildscripts/scons.py mongerd --disable-warnings-as-errors

To install

    $ python3 buildscripts/scons.py --prefix=/opt/monger install

Please note that prebuilt binaries are available on [mongerdb.org](http://www.mongerdb.org/downloads) and may be the easiest way to get started.

SCons Targets
--------------

The following targets can be named on the scons command line to build only certain components:

* mongerd
* mongers
* monger
* core (includes mongerd, mongers, monger)
* all

Windows
--------------

See [the windows build manual](https://github.com/mongerdb/monger/wiki/Build-Mongerdb-From-Source#windows-specific-instructions)

Build requirements:
* Visual Studio 2017 version 15.9 or newer
* Python 3.7

Or download a prebuilt binary for Windows at www.mongerdb.org.

Debian/Ubuntu
--------------

To install dependencies on Debian or Ubuntu systems:

    # apt-get install build-essential
    # apt-get install libboost-filesystem-dev libboost-program-options-dev libboost-system-dev libboost-thread-dev

To run tests as well, you will need PyMonger:

    # apt-get install python3-pymonger

OS X
--------------

Using [Homebrew](http://brew.sh):

    $ brew install mongerdb

Using [MacPorts](http://www.macports.org):

    $ sudo port install mongerdb

FreeBSD
--------------

Install the following ports:

  * devel/libexecinfo
  * lang/llvm70
  * lang/python

Optional Components if you want to use system libraries instead of the libraries included with MongerDB

  * archivers/snappy
  * devel/boost
  * devel/pcre

Add `CC=clang70 CXX=clang++70` to the `scons` options, when building.

OpenBSD
--------------
Install the following ports:

  * devel/libexecinfo
  * lang/gcc
  * lang/python
