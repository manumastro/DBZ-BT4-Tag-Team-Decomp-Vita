#!/bin/sh
# Makes the Vita's Tag Team film (port/vita/assets/tag_ui_intro.mp4, tag_ui_loop.mp4) from upstream's own films
# (port/tbo/ui/tag_ui_intro.pss, tag_ui_loop.pss: BT4decomp's assets, tracked in git, not game data). The .mp4 files
# are H.264 for the Vita's hardware decoder (SceAvPlayer; 960 x 544, the Vita's screen, so that the frame has no padding); build.py packages them as tbo/ui/tag_ui_{intro,loop}.mp4.
#
# Needs ffmpeg with libx264 on the PATH (or FFMPEG=/path/to/ffmpeg). Run from anywhere (a POSIX shell: Git Bash on Windows):
#   port/vita/tools/make_tagmovie.sh
# The settings are the ones the two committed files were made with; change them only with new files in the commit.
set -eu
ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
FFMPEG=${FFMPEG:-ffmpeg}
SRC="$ROOT/port/tbo/ui"
OUT="$ROOT/port/vita/assets"
mkdir -p "$OUT"
for x in intro loop; do
    "$FFMPEG" -y -i "$SRC/tag_ui_$x.pss" -an -vf scale=960:544 -c:v libx264 -profile:v main -level 3.1 -preset slow \
        -crf 23 -pix_fmt yuv420p "$OUT/tag_ui_$x.mp4"
done
ls -l "$OUT"/tag_ui_intro.mp4 "$OUT"/tag_ui_loop.mp4
