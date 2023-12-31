#!/bin/sh

. "${TEST_SCRIPTS_DIR}/unit.sh"

setup_ctdb_base "$CTDB_TEST_TMP_DIR" "ctdb-etc"

define_test "3 nodes, all healthy with home_nodes"

home_nodes="$CTDB_BASE"/home_nodes

cat > "$home_nodes" <<EOF
192.168.21.254 2
192.168.20.251 1
EOF

required_result <<EOF
${TEST_DATE_STAMP}Deterministic IPs enabled. Resetting all ip allocations
192.168.21.254 2
192.168.21.253 1
192.168.21.252 2
192.168.20.254 0
192.168.20.253 1
192.168.20.252 2
192.168.20.251 1
192.168.20.250 1
192.168.20.249 2
EOF

simple_test 0,0,0 <<EOF
192.168.20.249 1
192.168.20.250 1
192.168.20.251 1
192.168.20.252 1
192.168.20.253 1
192.168.20.254 1
192.168.21.252 1
192.168.21.253 1
192.168.21.254 1
EOF

rm "$home_nodes"
