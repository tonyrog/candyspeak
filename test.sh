#!/bin/bash
# test.sh - CandySpeak regression test runner

CSP=./csp
EXAMPLES=examples
UNITS=tests/unit
EXPECTED=tests/expected
ACTUAL=tests/actual

PASS=0
FAIL=0
SKIP=0

# Default cycles
CYCLES=10

# Colors (if terminal supports it)
if [ -t 1 ]; then
    RED='\033[0;31m'
    GREEN='\033[0;32m'
    YELLOW='\033[0;33m'
    NC='\033[0m'
else
    RED=''
    GREEN=''
    YELLOW=''
    NC=''
fi

run_test() {
    local file="$1"
    local name=$(basename "$file" .csp)
    local dir=$(dirname "$file")
    local prefix="${dir##*/}"  # examples or unit
    local testname="${prefix}_${name}"

    # Check for .skip file
    if [ -f "${file}.skip" ]; then
        echo -e "${YELLOW}SKIP${NC}: $testname"
        ((SKIP++))
        return
    fi

    # Check for custom cycles in .cycles file
    local cycles=$CYCLES
    if [ -f "${file}.cycles" ]; then
        cycles=$(cat "${file}.cycles")
    fi

    # Check for custom flags in .flags file
    local flags="-Q"
    if [ -f "${file}.flags" ]; then
        flags=$(cat "${file}.flags")
    fi

    # Run test. A .stdin file feeds the REPL, so command-driven behaviour
    # (/save, /list, adds while running) can be regression tested too.
    local input=/dev/null
    if [ -f "${file}.stdin" ]; then
        input="${file}.stdin"
    fi
    $CSP $flags -c "$cycles" "$file" < "$input" > "${ACTUAL}/${testname}.out" 2>&1
    local exitcode=$?

    # Check expected output exists
    if [ ! -f "${EXPECTED}/${testname}.out" ]; then
        echo -e "${YELLOW}NEW${NC}:  $testname (no expected output)"
        ((SKIP++))
        return
    fi

    # Compare
    if diff -q "${EXPECTED}/${testname}.out" "${ACTUAL}/${testname}.out" > /dev/null 2>&1; then
        echo -e "${GREEN}PASS${NC}: $testname"
        ((PASS++))
    else
        echo -e "${RED}FAIL${NC}: $testname"
        echo "--- Expected vs Actual ---"
        diff "${EXPECTED}/${testname}.out" "${ACTUAL}/${testname}.out" | head -20
        echo "---"
        ((FAIL++))
    fi
}

update_expected() {
    local file="$1"
    local name=$(basename "$file" .csp)
    local dir=$(dirname "$file")
    local prefix="${dir##*/}"
    local testname="${prefix}_${name}"

    local cycles=$CYCLES
    if [ -f "${file}.cycles" ]; then
        cycles=$(cat "${file}.cycles")
    fi

    local flags="-Q"
    if [ -f "${file}.flags" ]; then
        flags=$(cat "${file}.flags")
    fi

    local input=/dev/null
    if [ -f "${file}.stdin" ]; then
        input="${file}.stdin"
    fi
    $CSP $flags -c "$cycles" "$file" < "$input" > "${EXPECTED}/${testname}.out" 2>&1
    echo "Updated: $testname"
}

# Parse arguments
UPDATE=0
FILTER=""

while [ $# -gt 0 ]; do
    case "$1" in
        -u|--update)
            UPDATE=1
            shift
            ;;
        -f|--filter)
            FILTER="$2"
            shift 2
            ;;
        -h|--help)
            echo "Usage: $0 [options]"
            echo "  -u, --update     Update expected outputs"
            echo "  -f, --filter X   Only run tests matching X"
            echo "  -h, --help       Show this help"
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            exit 1
            ;;
    esac
done

# Build first
echo "Building csp..."
make -s || exit 1
echo ""

# Collect test files
FILES=()
for f in ${EXAMPLES}/*.csp ${UNITS}/*.csp; do
    [ -f "$f" ] || continue
    if [ -n "$FILTER" ]; then
        [[ "$f" == *"$FILTER"* ]] || continue
    fi
    FILES+=("$f")
done

if [ $UPDATE -eq 1 ]; then
    echo "Updating expected outputs..."
    for f in "${FILES[@]}"; do
        update_expected "$f"
    done
    echo "Done."
else
    echo "Running ${#FILES[@]} tests..."
    echo ""
    for f in "${FILES[@]}"; do
        run_test "$f"
    done
    echo ""
    echo "================================"
    echo -e "Pass: ${GREEN}${PASS}${NC}, Fail: ${RED}${FAIL}${NC}, Skip: ${YELLOW}${SKIP}${NC}"

    if [ $FAIL -gt 0 ]; then
        exit 1
    fi
fi
