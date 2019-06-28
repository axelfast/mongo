#!/bin/bash

set_goenv() {
    # Error out if not in the same directory as this script
    if [ ! -f ./set_goenv.sh ]; then
        echo "Must be run from monger-tools-common top-level directory. Aborting."
        return 1
    fi

    # Set OS-level default Go configuration
    UNAME_S=$(PATH="/usr/bin:/bin" uname -s)
    case $UNAME_S in
        CYGWIN*)
            PREF_GOROOT="c:/golang/go1.11"
            PREF_PATH="/cygdrive/c/golang/go1.11/bin:/cygdrive/c/mingw-w64/x86_64-4.9.1-posix-seh-rt_v3-rev1/mingw64/bin:$PATH"
        ;;
        *)
            PREF_GOROOT="/opt/golang/go1.11"
            # XXX might not need mongerdbtoolchain anymore
            PREF_PATH="$PREF_GOROOT/bin:/opt/mongerdbtoolchain/v3/bin/:$PATH"
        ;;
    esac

    # Set OS-level compilation flags
    case $UNAME_S in
        'CYGWIN*')
            export CGO_CFLAGS="-D_WIN32_WINNT=0x0601 -DNTDDI_VERSION=0x06010000"
            ;;
        'Darwin')
            export CGO_CFLAGS="-mmacosx-version-min=10.11"
            export CGO_LDFLAGS="-mmacosx-version-min=10.11"
            ;;
    esac

    # XXX Setting the compiler might not be necessary anymore now that we're
    # using standard Go toolchain and if we don't put mongerdbtoolchain into the
    # path.  But if we need to keep mongerdbtoolchain for other tools (eg. python),
    # then this is probably still necessary to find the right gcc.
    if [ -z "$CC" ]; then
        UNAME_M=$(PATH="/usr/bin:/bin" uname -m)
        case $UNAME_M in
            aarch64)
                export CC=/opt/mongerdbtoolchain/v3/bin/aarch64-mongerdb-linux-gcc
            ;;
            ppc64le)
                export CC=/opt/mongerdbtoolchain/v3/bin/ppc64le-mongerdb-linux-gcc
            ;;
            s390x)
                export CC=/opt/mongerdbtoolchain/v3/bin/s390x-mongerdb-linux-gcc
            ;;
            *)
                # Not needed for other architectures
            ;;
        esac
    fi

    # If GOROOT is not set by the user, configure our preferred Go version and
    # associated path if available or error.
    if [ -z "$GOROOT" ]; then
        if [ -d "$PREF_GOROOT" ]; then
            export GOROOT="$PREF_GOROOT";
            export PATH="$PREF_PATH";
        else
            echo "GOROOT not set and preferred GOROOT '$PREF_GOROOT' doesn't exist. Aborting."
            return 1
        fi
    fi

    # Derive GOPATH from current directory, but error if the current diretory
    # doesn't look like a GOPATH structure.
    if expr "$(pwd)" : '.*src/github.com/mongerdb/monger-tools-common$' > /dev/null; then
        export GOPATH=$(echo $(pwd) | perl -pe 's{src/github.com/mongerdb/monger-tools-common}{}')
        if expr "$UNAME_S" : 'CYGWIN' > /dev/null; then
            export GOPATH=$(cygpath -w "$GOPATH")
        fi
    else
        echo "Current path '$(pwd)' doesn't resemble a GOPATH-style path. Aborting.";
        return 1
    fi

    return
}

print_tags() {
    tags=""
    if [ ! -z "$1" ]
    then
            tags="$@"
    fi
    UNAME_S=$(PATH="/usr/bin:/bin" uname -s)
    case $UNAME_S in
        Darwin)
            if expr "$tags" : '.*ssl' > /dev/null ; then
                tags="$tags openssl_pre_1.0"
            fi
        ;;
    esac
    echo "$tags"
}
