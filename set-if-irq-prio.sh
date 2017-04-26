#!/bin/bash

if [ "$#" -lt 3 ]; then
	echo "set IRQ thread priority and cpu affinity of an ethernet interface"
	echo "$0 <interface> <priority> <affinity mask>"
	exit 1
fi

PRIO=$2
AF=$3

mapfile -t PIDS < <(ps aux | grep -e "\[irq/[0-9]\+-$1.*\]" | grep -v grep \
	| awk '{ print $2 }')
mapfile -t IRQS < <(cat /proc/interrupts | grep "$1" \
	| awk -F ":" '{ print $1 }' | tr -d ' ')

for i in "${PIDS[@]}"; do
	chrt -f -p $PRIO "$i"
	chrt -p "$i"
	taskset -p 0"$AF" "$i"
	let "PRIO-=1"
done

for i in "${IRQS[@]}"; do
	echo "$AF" > /proc/irq/"$i"/smp_affinity
done
