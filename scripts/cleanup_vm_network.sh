#!/bin/bash

source $(dirname $0)/vm_config.env

set -x

sudo ip link set $TAP down
sudo ip link delete $TAP
sudo ip link set $BRIDGE down
sudo ip link delete $BRIDGE

sudo sysctl -w net.ipv4.ip_forward=1

sudo iptables -t nat -A POSTROUTING -o $ETHER -j MASQUERADE
sudo iptables -A FORWARD -i $BRIDGE -j ACCEPT
sudo iptables -A FORWARD -o $BRIDGE -m state --state RELATED,ESTABLISHED -j ACCEPT
