#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
temp=$(mktemp -d)
trap 'rm -rf "$temp"' EXIT HUP INT TERM
${CC:-cc} -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
    -I"$root/include" "$root/tests/protocol_test.c" "$root/module/hv_protocol.c" \
    -o "$temp/protocol-test"
"$temp/protocol-test"
for script in "$root"/tools/*.sh "$root"/tests/*.sh; do sh -n "$script"; done
