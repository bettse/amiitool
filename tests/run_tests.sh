#!/bin/sh
# Regression tests for amiitool.
#
# Only NTAG215 dumps are exercised here; the larger NTAG I2C Plus samples in
# samples/ are kept as fixtures for future support work and are intentionally
# not asserted against yet.
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

# --- NTAG215 fixture ------------------------------------------------------
SAMPLE="$ROOT/samples/Yarn.nfc"
BIN="$WORK/Yarn.bin"
DEC="$WORK/Yarn.dec"
REENC="$WORK/Yarn.reenc"
DEC2="$WORK/Yarn.dec2"

python3 "$CONV" "$SAMPLE" "$BIN"

# An NTAG215 dump is exactly 135 pages = 540 bytes.
if [ "$(wc -c < "$BIN")" -eq 540 ]; then
	pass "Yarn converts to a 540-byte NTAG215 dump"
else
	die "Yarn dump is not 540 bytes (got $(wc -c < "$BIN"))"
fi

# Decrypt must succeed in strict mode (signatures valid).
if "$AMIITOOL" -d -k "$KEY" -i "$BIN" -o "$DEC" 2>/dev/null; then
	pass "Yarn decrypts with valid signature (strict mode)"
else
	die "Yarn failed strict decrypt"
fi

# Round-trip: re-encrypting the plaintext reproduces the original tag.
"$AMIITOOL" -e -k "$KEY" -i "$DEC" -o "$REENC" 2>/dev/null
if cmp -s "$BIN" "$REENC"; then
	pass "Yarn round-trips (encrypt(decrypt(x)) == x)"
else
	die "Yarn round-trip mismatch"
fi

# Decrypt is deterministic / idempotent.
"$AMIITOOL" -d -k "$KEY" -i "$REENC" -o "$DEC2" 2>/dev/null
if cmp -s "$DEC" "$DEC2"; then
	pass "Yarn decrypt is deterministic"
else
	die "Yarn decrypt is not deterministic"
fi

# -------------------------------------------------------------------------
if [ "$fail" -ne 0 ]; then
	echo "TESTS FAILED" >&2
	exit 1
fi
echo "All tests passed"
