#!/bin/bash
set -ex

# Configuration
BRIDGE=br0
TAP=vm0
BRIDGE_IP=192.168.100.1/24

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

echo "Network setup complete."
echo "Bridge: $BRIDGE ($BRIDGE_IP)"
echo "TAP device: $TAP"
