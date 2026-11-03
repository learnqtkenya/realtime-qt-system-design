#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."

QT=/opt/Qt/6.12.0/gcc_64

cmake --preset release >/dev/null
cmake --build --preset release
ctest --preset release

lint=$(cmake --build --preset release --target all_qmllint 2>&1 | grep -E "^(Warning|Error)" || true)
if [[ -n "$lint" ]]; then
    echo "$lint"
    echo "qmllint: FAILED"
    exit 1
fi
echo "qmllint: ok"

bad=0
for f in $(git ls-files '*.qml'); do
    if ! "$QT/bin/qmlformat" "$f" | diff -q - "$f" >/dev/null; then
        echo "needs qmlformat: $f"
        bad=1
    fi
done
[[ $bad -eq 0 ]] && echo "qmlformat: ok" || exit 1
