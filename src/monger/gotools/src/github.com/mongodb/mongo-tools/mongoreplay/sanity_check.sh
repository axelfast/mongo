#!/bin/bash

PORT=27017
STARTMONGO=false
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
PCAPFILE="$SCRIPT_DIR/mongerreplay_test.out"

while test $# -gt 0; do
	case "$1" in
		-f|--file)
			shift
			PCAPFILE="$1"
			shift
			;;
		-p|--port)
			shift
			PORT="$1"
			shift
			;;
		-m|--start-monger)
			shift
			STARTMONGO=true
			;;
		*)
			echo "Unknown arg: $1"
			exit 1
	esac
done

command -v mongerreplay >/dev/null
if [ $? != 0 ]; then
  echo "mongerreplay must be in PATH"
  exit 1
fi

set -e
set -o verbose

OUTFILE="$(echo $PCAPFILE | cut -f 1 -d '.').playback"
mongerreplay record -f $PCAPFILE -p $OUTFILE

if [ "$STARTMONGO" = true ]; then
	rm -rf /data/mongerreplay/
	mkdir /data/mongerreplay/
	echo "starting MONGOD"
	mongerd --port=$PORT --dbpath=/data/mongerreplay &
	MONGOPID=$!
fi

monger --port=$PORT mongerplay_test --eval "db.setProfilingLevel(2);" 
monger --port=$PORT mongerplay_test --eval "db.createCollection('sanity_check', {});" 

export MONGOREPLAY_HOST="mongerdb://localhost:$PORT"
mongerreplay play -p $OUTFILE
monger --port=$PORT mongerplay_test --eval "var profile_results = db.system.profile.find({'ns':'mongerplay_test.sanity_check'});
assert.gt(profile_results.size(), 0);" 

monger --port=$PORT mongerplay_test --eval "var query_results = db.sanity_check.find({'test_success':1});
assert.gt(query_results.size(), 0);" 

# test that files are correctly gziped ( TOOLS-1503 )
mongerreplay record -f $PCAPFILE -p ${OUTFILE} --gzip
gunzip -t ${OUTFILE}

echo "Success!"

if [ "$STARTMONGO" = true ]; then
	kill $MONGOPID
fi


