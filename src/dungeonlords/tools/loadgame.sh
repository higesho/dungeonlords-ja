#!/bin/sh
# launch the test game and load the first savegame
cd "$(dirname "$0")/.."
W="python tools/win.py"
$W start "$@"
sleep 9
for i in 1 2 3; do $W key esc; sleep 1; done
sleep 2
$W lclick 640 405; sleep 3      # load game
$W lclick 612 370; sleep 1; $W lclick 612 370; sleep 12
