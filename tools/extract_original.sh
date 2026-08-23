#!/usr/bin/env bash
set -euo pipefail

image="/mnt/c/Users/kando/Documents/ChatGPT/mario fundamentals native windows port/work/source/MarioFundamentals.hfs"
destination="/mnt/c/Users/kando/Documents/ChatGPT/mario fundamentals native windows port/work/extracted"

mkdir -p "$destination"
hmount "$image" >/dev/null

hcopy -r ":Mario's FUNdamentals 1.1:Mario's FUNdamentals 1.1" \
  "$destination/MarioFundamentals.data"
hcopy -m ":Mario's FUNdamentals 1.1:Mario's FUNdamentals 1.1" \
  "$destination/MarioFundamentals.bin"
hcopy -r ":Mario's FUNdamentals 1.1:Read Me - Mario's FUNdamentals" \
  "$destination/Read Me - Mario's FUNdamentals.txt"
hcopy -r ":Mario's FUNdamentals 1.1:What To Do If The Game Crashes" \
  "$destination/What To Do If The Game Crashes.txt"

pref_name="$(hls ':System Folder:Preferences:' | tr '\r' '\n' | sed -n "/Mario's FUNdamentals.*Prefs/p" | head -n 1)"
if [[ -n "$pref_name" ]]; then
  hcopy -m ":System Folder:Preferences:$pref_name" \
    "$destination/MarioFundamentalsPrefs.bin"
fi
