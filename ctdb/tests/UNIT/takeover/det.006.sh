#!/bin/sh

. "${TEST_SCRIPTS_DIR}/unit.sh"

setup_ctdb_base "$CTDB_TEST_TMP_DIR" "ctdb-etc"

define_test "3 nodes, 1 healthy with home_nodes"

home_nodes="$CTDB_BASE"/home_nodes

cat > "$home_nodes" <<EOF
192.168.21.254 2
192.168.20.251 1
EOF

required_result <<EOF
${TEST_DATE_STAMP}Deterministic IPs enabled. Resetting all ip allocations
${TEST_DATE_STAMP}Unassign IP: 192.168.21.253 from 1
${TEST_DATE_STAMP}Unassign IP: 192.168.20.254 from 0
${TEST_DATE_STAMP}Unassign IP: 192.168.20.253 from 1
${TEST_DATE_STAMP}Unassign IP: 192.168.20.251 from 1
${TEST_DATE_STAMP}Unassign IP: 192.168.20.250 from 1
192.168.21.254 2
192.168.21.253 2
192.168.21.252 2
192.168.20.254 2
192.168.20.253 2
192.168.20.252 2
192.168.20.251 2
192.168.20.250 2
192.168.20.249 2
EOF

simple_test 2,2,0 <<EOF
192.168.20.249 0
192.168.20.250 1
192.168.20.251 2
192.168.20.252 0
192.168.20.253 1
192.168.20.254 2
192.168.21.252 0
192.168.21.253 1
192.168.21.254 2
EOF

rm "$home_nodes"
