@echo off

REM Press Win+R, type "shell:sendto" and put this batch script there. Now you can just right-click, go to "Send to" and select "Create Pass File"
REM Requirements
REM * SCXvid-standalone: https://github.com/soyokaze/SCXvid-standalone/releases
REM * FFmpeg: http://ffmpeg.zeranoe.com/builds/

echo Creating Keyframes...
set video="%~1"
set video2="%~n1"
ffmpeg -i input.mkv -f yuv4mpegpipe -vf scale=640:360 -pix_fmt yuv420p -vsync drop -loglevel quiet - | SCXvid/SCXvid.exe input_keyframes.txt
echo Done

REM pause
