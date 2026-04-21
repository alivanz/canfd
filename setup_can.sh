#!/bin/bash
# Setup CAN interface with CAN-FD support
# Usage: ./setup_can.sh <interface> [bitrate] [dbitrate]
# Example: ./setup_can.sh can0 1000000 5000000

if [ -z "$1" ]; then
    echo "Usage: $0 <interface> [bitrate] [dbitrate]"
    echo "Example: $0 can0 1000000 5000000"
    exit 1
fi

IFACE=$1
BITRATE=${2:-1000000}
DBITRATE=${3:-5000000}

echo "Setting up $IFACE with bitrate=$BITRATE, dbitrate=$DBITRATE"

sudo ip link set $IFACE down
sudo ip link set $IFACE type can bitrate $BITRATE dbitrate $DBITRATE fd on
sudo ip link set $IFACE txqueuelen 1000
sudo ip link set $IFACE up

echo "$IFACE is now up"
ip -details link show $IFACE
