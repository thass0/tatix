#!/bin/bash

set -x

sudo ip link set vm0 down
sudo ip link delete vm0
sudo ip link set br0 down
sudo ip link delete br0
