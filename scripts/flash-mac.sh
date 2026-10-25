#!/bin/bash
# Drives the official firmware tool UI: select the .bin, click 开始升级.
# Needs Accessibility permission for the terminal app. UI automation is tied to
# this tool's layout; if it stops clicking the right things, do it by hand:
# launch the tool with the shim, then 选取文件 -> 开始升级.
#
# usage: flash-mac.sh /tmp/niztool.app /path/to/firmware.bin
set -u
APP=${1:?usage: flash-mac.sh <app path> <firmware.bin>}
BIN=${2:?usage: flash-mac.sh <app path> <firmware.bin>}
SHIM="$(cd "$(dirname "$0")/.." && pwd)/shim/shim.dylib"
BIN=$(cd "$(dirname "$BIN")" && pwd)/$(basename "$BIN")

osa() { osascript -e "$1" >/dev/null 2>&1; }

# A tool left running re-queries the keyboard on every replug and jams it.
pkill -f FirmwareUpgradeProject 2>/dev/null
sleep 1

DYLD_INSERT_LIBRARIES="$SHIM" "$APP/Contents/MacOS/FirmwareUpgradeProject" \
    > /tmp/niztool.log 2>&1 &
sleep 3

osa 'tell application "System Events" to tell process "FirmwareUpgradeProject" to set frontmost to true'
osa 'tell application "System Events" to tell process "FirmwareUpgradeProject" to click button "选取文件" of window 1'
sleep 2

printf '%s' "$BIN" | pbcopy
osa 'tell application "System Events" to keystroke "g" using {command down, shift down}'
sleep 1.5
osa 'tell application "System Events" to keystroke "v" using command down'
sleep 0.6
osa 'tell application "System Events" to key code 36'
sleep 1.5
osa 'tell application "System Events" to key code 36'
sleep 2

if ! grep -q "file url" /tmp/niztool.log; then
    echo "file not selected - pick it by hand, then click 开始升级" >&2
    exit 1
fi

osa 'tell application "System Events" to tell process "FirmwareUpgradeProject" to set frontmost to true'
osa 'tell application "System Events" to tell process "FirmwareUpgradeProject" to click button "开始升级" of window 1'

echo "flashing, watching /tmp/niztool.log ..."
for _ in $(seq 1 120); do
    if grep -q "结束固件升级" /tmp/niztool.log; then
        echo "done"
        grep -i error /tmp/niztool.log | tail -3
        exit 0
    fi
    if grep -q "upgrade error" /tmp/niztool.log; then
        echo "FAILED - see /tmp/niztool.log" >&2
        exit 2
    fi
    sleep 1
done
echo "timed out - see /tmp/niztool.log" >&2
exit 3
