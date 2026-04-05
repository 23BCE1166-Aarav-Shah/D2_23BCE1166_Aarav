#!/usr/bin/env bash
set -euo pipefail

DEST_DIR="${1:-}"

if [[ -z "$DEST_DIR" ]]; then
  echo "usage: $0 <fixture-dir>" >&2
  exit 2
fi

if ! command -v ffmpeg >/dev/null 2>&1; then
  echo "ffmpeg is required to generate mixed media fixtures." >&2
  exit 1
fi

rm -rf "$DEST_DIR"
mkdir -p "$DEST_DIR"/{text,set-a,set-b,images,videos,audio,bulk}

printf 'duplicate-text-payload\n' > "$DEST_DIR/text/dup-1.txt"
cp "$DEST_DIR/text/dup-1.txt" "$DEST_DIR/text/dup-2.txt"
printf 'unique-text-payload\n' > "$DEST_DIR/text/unique.txt"

printf 'same-bytes-0001' > "$DEST_DIR/set-a/bin-1.dat"
cp "$DEST_DIR/set-a/bin-1.dat" "$DEST_DIR/set-b/bin-2.dat"
printf 'other-bytes-001' > "$DEST_DIR/set-b/bin-3.dat"

ffmpeg -loglevel error -y -f lavfi -i testsrc=size=96x96:rate=1 -frames:v 1 "$DEST_DIR/images/base.png"
ffmpeg -loglevel error -y -i "$DEST_DIR/images/base.png" "$DEST_DIR/images/base-copy.jpg"
ffmpeg -loglevel error -y -f lavfi -i smptebars=size=96x96:rate=1 -frames:v 1 "$DEST_DIR/images/unique.png"

ffmpeg -loglevel error -y -f lavfi -i testsrc=size=96x96:rate=5 -t 1 "$DEST_DIR/videos/base.mp4"
cp "$DEST_DIR/videos/base.mp4" "$DEST_DIR/videos/base-copy.mp4"
ffmpeg -loglevel error -y -f lavfi -i color=c=green:size=96x96:rate=5 -t 1 "$DEST_DIR/videos/unique.mp4"

ffmpeg -loglevel error -y -f lavfi -i sine=frequency=440:duration=1 "$DEST_DIR/audio/base.wav"
cp "$DEST_DIR/audio/base.wav" "$DEST_DIR/audio/base-copy.wav"
ffmpeg -loglevel error -y -f lavfi -i sine=frequency=880:duration=1 "$DEST_DIR/audio/unique.wav"

for i in $(seq 1 2000); do
  printf 'filler-%04d\n' "$i" > "$DEST_DIR/bulk/file-$i.txt"
done

for i in $(seq 1 200); do
  cp "$DEST_DIR/text/dup-1.txt" "$DEST_DIR/bulk/dup-copy-$i.txt"
done

echo "Fixture created at $DEST_DIR"
