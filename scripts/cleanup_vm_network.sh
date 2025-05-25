#!/bin/bash

source $(dirname $0)/vm_config.env

if [ -z "$IF" ]; then
    echo "Missing environment variable IF (outward facing network interface)"
	exit -1
fi

set -x

sudo ip link set $TAP down
sudo ip link delete $TAP
sudo ip link set $BRIDGE down
sudo ip link delete $BRIDGE

sudo sysctl -w net.ipv4.ip_forward=1

sudo iptables -t nat -D POSTROUTING -o $IF -j MASQUERADE
sudo iptables -D FORWARD -i $BRIDGE -j ACCEPT
sudo iptables -D FORWARD -o $BRIDGE -m state --state RELATED,ESTABLISHED -j ACCEPT

sudo iptables -D FORWARD -d $VM_IP -i $IF -j ACCEPT
sudo iptables -t nat -D PREROUTING -i $IF -p icmp -j DNAT --to-destination $VM_IP

