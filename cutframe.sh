#!/bin/bash

if [ $# -ne 3 ]
then
  echo 'Usage: ./cutframe.sh INPUT_VIDEO FRAME_TO_CUT OUTPUT_IMG'
  echo 'OUTPUT_IMG must be .yuv in order to work with framepos!'
  exit 0
fi

input=$1
frame_num=$2
output=$3

ffmpeg -i $input -vf "select=eq(n\,"$frame_num")" -frames 1 $output
