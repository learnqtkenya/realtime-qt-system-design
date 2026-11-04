#!/usr/bin/env bash
# The viewer on two physical cores (CPUs 0 and 1; their SMT siblings are 8 and 9): a smaller machine.
exec taskset -c 0,1 "$(dirname "$0")/../build/release/app/viewer" "$@"
