#!/bin/bash

source $(dirname $0)/vm_config.env

if [ -z "$ETHER" ]; then
	echo "Missing environment variable ETHER"
	exit -1
fi

set -x

sudo ip link set $TAP down
sudo ip link delete $TAP
sudo ip link set $BRIDGE down
sudo ip link delete $BRIDGE

sudo sysctl -w net.ipv4.ip_forward=1

sudo iptables -t nat -D POSTROUTING -o $ETHER -j MASQUERADE
sudo iptables -D FORWARD -i $BRIDGE -j ACCEPT
sudo iptables -D FORWARD -o $BRIDGE -m state --state RELATED,ESTABLISHED -j ACCEPT

sudo iptables -D FORWARD -d $VM_IP -i $ETHER -j ACCEPT
sudo iptables -t nat -D PREROUTING -i $ETHER -p icmp -j DNAT --to-destination $VM_IP

