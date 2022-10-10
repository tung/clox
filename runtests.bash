#!/usr/bin/env bash

# Run tests given as arguments.
#
# Usage: ./runtests.bash ./foo_test ./bar_test ./baz_test ...
#
# Prints headings before tests, lists failed tests
# and returns non-zero when any test fails.

set -Eeuo pipefail

failures=()

# Run each test and add it to the failures array if it fails.
for test in "$@"; do \
	printf "\033[96;1m==[ %s ]==\033[0m\n" "${test}"
	"${test}" || failures+=("${test}")
done

# If any tests failed, summarize them here.
if (( ${#failures[*]} > 0 )); then
	printf "\033[91;1mFAILED TESTS (%d):\033[0m\n" ${#failures[*]}
	for failed in "${failures[@]}"; do
		printf "  \033[93;1m%s\033[0m\n" "${failed}"
	done
fi

# Exit with the number of failed tests.
exit ${#failures[*]}
