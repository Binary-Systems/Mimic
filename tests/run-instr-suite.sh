#!/bin/sh
# Run the 6809/6309 instruction validation suite.
#
# Recommended usage when your shell cwd is the emulator subdir:
#   cd /Volumes/Drobo0/BSI/Mimic/mimic09
#   ../tests/run-instr-suite.sh
#   ../tests/run-instr-suite.sh --test 6809_ABX_inh
#
# Or from the Mimic root:
#   ./tests/run-instr-suite.sh
#
# The script will find the correct python test runner and the mimic09 binary.

set -e

# Resolve the directory this wrapper lives in (tests/)
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

# Mimic root is parent of tests/
MIMIC_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# The emulator binary is in the mimic09/ subdir
BINARY="$MIMIC_ROOT/mimic09/bin/mimic09"

if [ ! -x "$BINARY" ]; then
    echo "Error: binary not found or not executable at $BINARY"
    echo "Please run 'make' from inside $MIMIC_ROOT/mimic09 first."
    exit 1
fi

# Pass through any args (--test, --list, etc.)
exec python3 "$SCRIPT_DIR/instr/validate.py" --binary "$BINARY" "$@"
