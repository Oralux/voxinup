#!/bin/bash
touch /tmp/voxinup.ok
FILE=Front_Center.wav
sox /usr/share/sounds/alsa/$FILE -r 11025 $FILE
./test1
./test2
rm $FILE

