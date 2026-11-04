#!/usr/bin/env bash
# The viewer on one core (CPU 0, without its SMT sibling 8): a smaller machine.
exec taskset -c 0 "$(dirname "$0")/../build/release/app/viewer" "$@"
