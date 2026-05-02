#!/usr/bin/env sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT_DIR"

if [ -t 1 ]; then
    green=$(printf '\033[32m')
    red=$(printf '\033[31m')
    reset=$(printf '\033[0m')
else
    green=
    red=
    reset=
fi

pass() {
    printf '%sPASS%s %s\n' "$green" "$reset" "$1"
}

fail() {
    printf '%sFAIL%s %s\n' "$red" "$reset" "$1" >&2
    shift
    for line in "$@"; do
        printf '  %s\n' "$line" >&2
    done
    exit 1
}

build() {
    cc -Iinclude nob.c -o nob
    ./nob
}

run_contains() {
    name=$1
    input=$2
    expected=$3

    set +e
    raw_output=$(printf '%b' "$input" | ./smorth 2>&1)
    status=$?
    set -e
    output=$(printf '%s' "$raw_output" | tr '\n' ' ')

    if [ "$status" -ne 0 ]; then
        fail "$name" "expected zero exit" "actual status: $status" "actual output: $output"
    fi

    case "$output" in
        *"$expected"*) pass "$name" ;;
        *) fail "$name" "expected substring: $expected" "actual output: $output" ;;
    esac
}

run_fails_contains() {
    name=$1
    input=$2
    expected=$3

    set +e
    raw_output=$(printf '%b' "$input" | ./smorth 2>&1)
    status=$?
    set -e
    output=$(printf '%s' "$raw_output" | tr '\n' ' ')

    if [ "$status" -eq 0 ]; then
        fail "$name" "expected nonzero exit" "actual output: $output"
    fi

    case "$output" in
        *"$expected"*) pass "$name" ;;
        *) fail "$name" "expected substring: $expected" "actual output: $output" ;;
    esac
}

build

run_contains "arithmetic" \
    '1 2 + . bye\n' \
    '3 '

run_contains "colon definition" \
    ': sq dup * ; 7 sq . bye\n' \
    '49 '

run_contains "conditionals" \
    ': choose if 111 else 222 then . ; 1 choose 0 choose bye\n' \
    '111 222 '

run_contains "recursion" \
    ': fact dup 1 > if dup 1- recurse * else drop 1 then ; 5 fact . bye\n' \
    '120 '

run_contains "begin until" \
    ': countdown begin dup . 1- dup 0= until drop ; 3 countdown bye\n' \
    '3 2 1 '

run_contains "do loop" \
    ': loop-test 5 0 do i . loop ; loop-test bye\n' \
    '0 1 2 3 4 '

run_contains "+loop" \
    ': evens 10 0 do i . 2 +loop ; evens bye\n' \
    '0 2 4 6 8 '

run_contains "leave" \
    ': leave-test 10 0 do i dup 3 = if leave then . loop ; leave-test bye\n' \
    '0 1 2 '

run_contains "return stack words" \
    ': return-stack-test 42 >r 1 r> + . ; return-stack-test bye\n' \
    '43 '

run_contains "variables and memory words" \
    'variable x 42 x ! x @ . bye\n' \
    '42 '

run_contains "compiled string output" \
    ': msg ." hello" ; msg bye\n' \
    'hello'

run_fails_contains "undefined word error" \
    'not-a-word\n' \
    'undefined word: not-a-word'

run_fails_contains "compile-time undefined word error" \
    ': broken not-a-word ;\n' \
    'undefined word: not-a-word'

run_fails_contains "integer overflow error" \
    '999999999999999999999999999999999999999999\n' \
    'invalid number:'

run_fails_contains "semicolon outside definition error" \
    ';\n' \
    '; outside word declaration'

printf 'All smoke tests passed.\n'
