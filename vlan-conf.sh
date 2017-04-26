#!/bin/bash

if [ "$#" -lt 1 ]; then
	echo "create vlans on network <interface> and prioritize traffic"
	echo "$0 <interface>"
	exit 1
fi

IF=$1

if [ ! -e /proc/net/vlan/config ]; then
	modprobe 8021q &> /dev/null
	if [ "$?" != "0" ]; then
		echo "failed to load 8021q module"
		exit 1
	fi
fi

vconfig rem $IF".2" &> /dev/null
vconfig rem $IF".3" &> /dev/null

vconfig add $IF 2
vconfig add $IF 3

vconfig set_egress_map $IF".2" 255 7 #>/dev/null
vconfig set_ingress_map $IF".2" 255 7 #>/dev/null

ifconfig $IF".2" 192.168.2.1 up
ifconfig $IF".3" 192.168.3.1 up
