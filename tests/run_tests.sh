#!/bin/sh
# Regression tests for amiitool.
#
# Covers the supported tag shapes: NTAG215 and NTAG I2C Plus 2K ("v3", as used
# by the Kirby Air Riders amiibo). The NTAG I2C Plus 1K sample in samples/ is
# kept as a fixture but is not yet supported and is not asserted here.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
AMIITOOL="$ROOT/amiitool"
KEY="$ROOT/key_retail.bin"
CONV="$ROOT/tests/nfc2bin.py"
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

fail=0
pass() { printf 'ok   - %s\n' "$1"; }
die()  { printf 'FAIL - %s\n' "$1"; fail=1; }

[ -x "$AMIITOOL" ] || { echo "amiitool not built; run 'make' first" >&2; exit 1; }
[ -f "$KEY" ] || { echo "missing key_retail.bin" >&2; exit 1; }

# Exercise a sample end to end: convert, strict decrypt, round-trip and
# deterministic decrypt. $1=label $2=sample.nfc $3=expected byte size.
check_sample() {
	label=$1
	sample=$2
	want_size=$3
	bin="$WORK/$label.bin"
	dec="$WORK/$label.dec"
	reenc="$WORK/$label.reenc"
	dec2="$WORK/$label.dec2"

	python3 "$CONV" "$sample" "$bin"

	if [ "$(wc -c < "$bin")" -eq "$want_size" ]; then
		pass "$label converts to a $want_size-byte dump"
	else
		die "$label dump is not $want_size bytes (got $(wc -c < "$bin"))"
		return
	fi

	if "$AMIITOOL" -d -k "$KEY" -i "$bin" -o "$dec" 2>/dev/null; then
		pass "$label decrypts with valid signature (strict mode)"
	else
		die "$label failed strict decrypt"
		return
	fi

	"$AMIITOOL" -e -k "$KEY" -i "$dec" -o "$reenc" 2>/dev/null
	if cmp -s "$bin" "$reenc"; then
		pass "$label round-trips (encrypt(decrypt(x)) == x, all $want_size bytes)"
	else
		die "$label round-trip mismatch"
	fi

	"$AMIITOOL" -d -k "$KEY" -i "$reenc" -o "$dec2" 2>/dev/null
	if cmp -s "$dec" "$dec2"; then
		pass "$label decrypt is deterministic"
	else
		die "$label decrypt is not deterministic"
	fi
}

# NTAG215: 135 pages = 540 bytes.
check_sample "Yarn" "$ROOT/samples/Yarn.nfc" 540

# NTAG I2C Plus 2K ("v3"): Flipper dump is 492 pages = 1968 bytes.
check_sample "Kirby" "$ROOT/samples/Kirby.nfc" 1968

# -------------------------------------------------------------------------
if [ "$fail" -ne 0 ]; then
	echo "TESTS FAILED" >&2
	exit 1
fi
echo "All tests passed"
