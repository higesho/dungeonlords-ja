#!/bin/sh
# 訳をまとめて dinput8.dll を作り直す（配布版 build/dinput8.dll と試験版 build/test/dinput8.dll）
set -e
cd "$(dirname "$0")/.."
python -X utf8 tools/merge.py
python -X utf8 tools/make_data.py
python -X utf8 tools/make_font.py
cmd //c "src\build.bat" 2>&1 | grep -iE "error|warning C" || true
cmd //c "src\build.bat test" 2>&1 | grep -iE "error|warning C" || true
ls -la build/dinput8.dll build/test/dinput8.dll
