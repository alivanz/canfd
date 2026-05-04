#!/bin/sh
PATH=/bin:/usr/bin:/sbin:/usr/sbin
DESC="SDC board config stop script"
PROGRAM1=/usr/sbin/sdc-uart-cfg
DRIVER1="sunix-sdc"

echo "Stopping $DESC"
$PROGRAM1 -s
sleep 1

exit 0
