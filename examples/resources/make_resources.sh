#!/bin/sh
# Rebuilds sheet.sprat and atlas.png from sources/ with sprat-cli (v0.24 or later).
# The frames in sources/ are drawn by make_sources.py.
set -e
cd "$(dirname "$0")"
spratlayout sources --trim-transparent --rotate --padding 2 --extrude 1 > layout.txt
spratpack -a atlas.png < layout.txt > /dev/null
spratconvert --transform raw -a atlas.png \
  --markers markers.txt --animations animations.txt < layout.txt > sheet.sprat
rm layout.txt
