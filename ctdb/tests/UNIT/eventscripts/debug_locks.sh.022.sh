#!/bin/sh

. "${TEST_SCRIPTS_DIR}/unit.sh"

define_test "DB D. DB MUTEX"

setup

do_test "DB" "D." "DB" "MUTEX"
