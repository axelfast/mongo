#/bin/sh

set -e

rm -rf specifications

git clone git@github.com:mongerdb/specifications

go generate ../