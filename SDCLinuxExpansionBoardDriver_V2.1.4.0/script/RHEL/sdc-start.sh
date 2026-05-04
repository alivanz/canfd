#!/bin/sh
PATH=/bin:/usr/bin:/sbin:/usr/sbin
DESC="SDC board config start script"
PROGRAM1=/usr/sbin/sdc-uart-cfg
DRIVER1="sunix-sdc"

load_driver()
{
	if [ `lsmod | grep -o ^$DRIVER1` ]; then
		echo "$DRIVER1 driver loaded."
	else
		modprobe $DRIVER1
		sleep 1
		if [ `lsmod | grep -o ^$DRIVER1` ]; then
			echo "$DRIVER1 driver loaded."
		else
			echo "$DRIVER1 driver not loaded."
		fi
	fi
}

echo "Starting $DESC"
load_driver
$PROGRAM1 -r
while true
do
	sleep 2
done

exit 0
