#!/bin/sh

# Helper script to test jupiter and voxinup
#
# If this test is ok, jupiter should be configured to select 
# voxinup at init time according to the 
# system rules (systemd, sysv,...).
#
bye() {
    echo $@ >&2 && exit 0
}

# check conditions

[ "$(id -u)" = "0" ] || bye "Please run this installer as root"
(which jupiter >/dev/null) || bye "jupiter not found"
(which voxinup >/dev/null) || bye "voxinup not found"
[ -e /var/opt/IBM/ibmtts/cfg/eci.ini ] || bye "Install please voxin before"
$(lsmod | grep -q acsint) || modprobe acsint 2>/dev/null || bye "acsint module not found"

# run jupiter
jupiter -d esp "|exec voxinup -j"
