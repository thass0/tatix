#!/bin/bash

source $(dirname $0)/vm_config.env

if [ -z "$IF" ]; then
    echo "Missing environment variable IF (outward-facing network interface)"
	exit -1
fi

set -ex

sudo ip link add name $BRIDGE type bridge || true
sudo ip addr add $BRIDGE_IP dev $BRIDGE || true
sudo ip link set $BRIDGE up

sudo ip tuntap add dev $TAP mode tap user "$USER" || true
sudo ip link set $TAP master $BRIDGE || true
sudo ip link set $TAP up

sudo chown "$USER":"$USER" /sys/class/net/$BRIDGE
sudo chown "$USER":"$USER" /sys/class/net/$TAP

sudo chmod u+rw /sys/class/net/$BRIDGE/bridge
sudo chmod u+rw /sys/class/net/$TAP

sudo sysctl -w net.ipv4.ip_forward=1

sudo iptables -t nat -A POSTROUTING -o $IF -j MASQUERADE
sudo iptables -A FORWARD -i $BRIDGE -j ACCEPT
sudo iptables -A FORWARD -o $BRIDGE -m state --state RELATED,ESTABLISHED -j ACCEPT

# Forward ICMP messages to the VM:
sudo iptables -A FORWARD -d $VM_IP -i $IF -j ACCEPT
sudo iptables -t nat -A PREROUTING -i $IF -p icmp -j DNAT --to-destination $VM_IP

echo "Network setup complete."
echo "Bridge: $BRIDGE ($BRIDGE_IP)"
echo "TAP device: $TAP"
echo "Interface: $IF"
