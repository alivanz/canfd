#! /bin/sh
#
### BEGIN INIT INFO
# Provides:          sdc-board-cfg.sh
# Required-Start:    $local_fs $syslog
# Required-Stop:
# Default-Start:     2 3 4 5
# Default-Stop:      0 1 6
# Short-Description: sdc-board-cfg - Save and load SDC board config
# Description:       This script save board config to file when system
#                    shutdown or reboot and load config from file when
#                    system bootup.
#
### END INIT INFO

PATH=/bin:/usr/bin:/sbin:/usr/sbin
DESC="SDC board config script"
PROGRAM1=/usr/sbin/sdc-uart-cfg
DRIVER1="sunix-sdc"

. /lib/lsb/init-functions

load_driver()
{
	if [ `lsmod | grep -o ^$DRIVER1` ]; then
		log_success_msg "$DRIVER1 driver loaded."
	else
		modprobe lp
		modprobe $DRIVER1
		sleep 1
		if [ `lsmod | grep -o ^$DRIVER1` ]; then
			log_success_msg "$DRIVER1 driver loaded."
		else
			log_failure_msg "$DRIVER1 driver not loaded."
		fi
	fi
}

case "$1" in
  start)
	log_daemon_msg "Starting $DESC"
	load_driver
	$PROGRAM1 -r
	log_end_msg $?
	;;
  stop)
	log_daemon_msg "Stopping $DESC"
	$PROGRAM1 -s
	log_end_msg $?
	;;
  *)
	echo "Usage: $0 {start|stop}" >&2
	exit 1
	;;
esac

exit 0
