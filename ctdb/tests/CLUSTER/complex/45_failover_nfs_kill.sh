#!/bin/bash

# Verify that a mounted NFS share is still operational after failover.

# We mount an NFS share from a node, write a file via NFS and then
# confirm that we can correctly read the file after a failover.

# Prerequisites:

# * An active CTDB cluster with at least 2 nodes with public addresses.

# * Test must be run on a real or virtual cluster rather than against
#   local daemons.

# * Test must not be run from a cluster node.

# Steps:

# 1. Verify that the cluster is healthy.
# 2. Select a public address and its corresponding node.
# 3. Select the 1st NFS share exported on the node.
# 4. Mount the selected NFS share.
# 5. Create a file in the NFS mount and calculate its checksum.
# 6. Kill CTDB on the selected node.
# 7. Read the file and calculate its checksum.
# 8. Compare the checksums.

# Expected results:

# * When a node is disabled the public address fails over and it is
#   possible to correctly read a file over NFS.  The checksums should be
#   the same before and after.

. "${TEST_SCRIPTS_DIR}/cluster.bash"

set -e

ctdb_test_init

nfs_test_setup

echo "Create file containing random data..."
dd if=/dev/urandom of=$nfs_local_file bs=1k count=1
original_sum=$(sum $nfs_local_file)
[ $? -eq 0 ]

gratarp_sniff_start

echo "Killing node $test_node"
try_command_on_node $test_node $CTDB getpid
pid=${out#*:}
# We need to be nasty to make that the node being failed out doesn't
# get a chance to send any tickles or doing anything else clever.  IPs
# also need to be dropped because we're simulating a dead node rather
# than a CTDB failure.  To properly handle a CTDB failure we would
# need a watchdog to drop the IPs when CTDB disappears.
try_command_on_node -v $test_node "kill -9 $pid ; $CTDB_TEST_WRAPPER drop_ips ${test_node_ips}"
wait_until_node_has_status $test_node disconnected

gratarp_sniff_wait_show

new_sum=$(sum $nfs_local_file)
[ $? -eq 0 ]

if [ "$original_md5" = "$new_md5" ] ; then
    echo "GOOD: file contents unchanged after failover"
else
    die "BAD: file contents are different after failover"
fi
