#!/bin/bash

if [ $# -ne 1 ]
then
  echo 'Usage: '$0' input.yuv'
  exit 1
fi

input="$1"
output=$(basename -- "$input")
output="${output%.*}.png"

convert -size 1920x1080 -sampling-factor 4:2:0 -depth 8 "$input" "$output"
