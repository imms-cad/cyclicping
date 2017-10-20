#!/bin/sh
#
# Setup the proposed credit based shaper[1] to provide 3 traffic classes.
# Class A (20 MBit/s) which is mapped to socket priority 3,
# Class B (10 MBit/s) which is mapped to socket priority 2 and best effort
# for all other socket priorities. Use for example '-P 3' with cyclicping to
# utilitze the Class A stream.
#
# Note that at the moment apart from a CBS patched kernel also a patched
# iproute2 is required[2] for this to work.
#
# [1] https://www.spinics.net/lists/netdev/msg460869.html
# [2] https://www.spinics.net/lists/netdev/msg459554.html

if [ "$#" -lt 1 ]; then
	echo "setup credit based shaper for network <interface>"
	echo "$0 <interface>"
	exit 1
fi

# your network interface
NIC=$1
# the CBS enabled tc from iproute2 package
TC=`which tc`
# enable or disable hardware offload for the cbs
OFFLOAD=1

ifconfig $NIC down

$TC qdisc del dev $NIC root &> /dev/null

$TC qdisc replace dev $NIC handle 100: parent root mqprio num_tc 3 \
	map 2 2 1 0 2 2 2 2 2 2 2 2 2 2 2 2 queues 1@0 1@1 2@2 hw 0

$TC qdisc replace dev $NIC parent 100:4 cbs locredit -1470 hicredit 30 \
	sendslope -980000 idleslope 20000 offload $OFFLOAD

$TC qdisc replace dev $NIC parent 100:5 cbs locredit -1485 hicredit 31 \
	sendslope -990000 idleslope 10000 offload $OFFLOAD

ifconfig $NIC 0.0.0.0 up
