// These commands were removed from mongers 4.2, but will still appear in the listCommands output
// of a 4.0 mongers. A last-stable mongers will be unable to run a command on a latest version shard
// that no longer supports that command. To increase test coverage and allow us to run on same- and
// mixed-version suites, we allow these commands to have a test defined without always existing on
// the servers being used.
const commandsRemovedFromMongosIn42 = [
    'copydb',
    'copydbsaslstart',
    'eval',
    'geoNear',
    'getPrevError',
    'group',
    'reIndex',
];
// These commands were added in mongers 4.2/4.4, so will not appear in the listCommands output of a
// 4.0 mongers. We will allow these commands to have a test defined without always existing on the
// mongers being used.
const commandsAddedToMongosIn42 = [
    'abortTransaction',
    'commitTransaction',
    'dropConnections',
    'setIndexCommitQuorum',
    'startRecordingTraffic',
    'stopRecordingTraffic',
];
const commandsAddedToMongosIn44 = ['refineCollectionShardKey'];