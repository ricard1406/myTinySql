#!/usr/bin/env bash
# Runs every tests/NAME.sql against a fresh copy of tests/data/ and compares the output
# with tests/NAME.expected. Use --update to rewrite the .expected files.
#
# tests/data/ is a frozen copy of the tables, so the results don't change when you
# add, edit or delete rows in your working data/ folder.
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
bin="$root/myTinySql"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

update=false
[[ "${1:-}" == "--update" ]] && update=true

failed=0
for sql in "$root"/tests/*.sql; do
    name="$(basename "$sql" .sql)"
    rm -rf "$work/data"
    cp -r "$root/tests/data" "$work/data"
    "$bin" --data="$work/data" --sqlfile="$sql" > "$work/$name.out" || true

    if $update; then
        cp "$work/$name.out" "$root/tests/$name.expected"
        echo "UPDATED $name"
    elif diff -u "$root/tests/$name.expected" "$work/$name.out"; then
        echo "PASS $name"
    else
        echo "FAIL $name"
        failed=1
    fi
done
exit $failed
