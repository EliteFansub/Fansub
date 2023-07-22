#! /bin/bash
ffmpeg -i input.mkv -f yuv4mpegpipe -vf scale=640:360 -pix_fmt yuv420p -vsync drop -loglevel quiet - | SCXvid/scxvid input_keyframes.txt
