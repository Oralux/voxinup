#!/bin/sh

# Helper script to stop jupiter and voxinup
# If ok, jupiter should be configured to be stopped 
# according to the system rules (sysv/systemd,...)

killall jupiter
