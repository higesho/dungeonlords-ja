#!/bin/sh
# launch the test game and go straight into a new game (hero "Aria", Human Fighter)
cd "$(dirname "$0")/.."
W="python tools/win.py"
$W start "$@"
sleep 9
for i in 1 2 3; do $W key esc; sleep 1; done
sleep 2
$W lclick 640 350; sleep 3      # new game
$W lclick 98 968; sleep 2       # new hero
$W lclick 98 968; sleep 2       # continue -> name
$W type aria; $W vk 13; sleep 2
$W lclick 992 968; sleep 12     # play
$W key esc; sleep 12            # skip narrative video
