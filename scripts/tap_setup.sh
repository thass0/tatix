#!/usr/bin/env bash

echo "Setting up TAP networking for QEMU ..."

# Create netdev group if it doesn't exist yet.
if ! getent group netdev > /dev/null; then
  echo "Creating netdev group..."
  sudo groupadd netdev
  echo "Group netdev created."
else
  echo "Group netdev already exists."
fi

# Add user to netdev group if not already a member
CURRENT_USER=$(whoami)
if ! groups "$CURRENT_USER" | grep -q "\bnetdev\b"; then
  echo "Adding user $CURRENT_USER to netdev group..."
  sudo usermod -aG netdev "$CURRENT_USER"
  NEED_NEWGRP=1
  echo "User added to netdev group."
else
  echo "User $CURRENT_USER is already in netdev group."
  NEED_NEWGRP=0
fi

# Create TAP interface if it doesn't exist
if ! ip link show vm0 &> /dev/null; then
  echo "Creating TAP interface vm0..."
  sudo ip tuntap add vm0 mode tap group netdev
  echo "TAP interface vm0 created."
else
  echo "TAP interface vm0 already exists."
  # Make sure it belongs to the netdev group
  sudo ip link set vm0 group netdev
fi

# Bring up the TAP interface
echo "Bringing up TAP interface vm0..."
sudo ip link set vm0 up
echo "TAP interface vm0 is up."

# Show status
echo -e "\nTAP Interface Status:"
ip link show vm0

echo -e "\nSetup complete!"
if [ "$NEED_NEWGRP" -eq 1 ]; then
  echo -e "\nIMPORTANT: You need to log out and log back in for group changes to take effect."
  echo "Alternatively, run 'newgrp netdev' in your current terminal session."

  # Offer to run newgrp for the user
  read -p "Would you like to run 'newgrp netdev' now? (y/n) " -n 1 -r
  echo
  if [[ $REPLY =~ ^[Yy]$ ]]; then
    echo "Running 'newgrp netdev'..."
    exec newgrp netdev
  fi
fi

echo -e "\n Add this to your QEMU command line to use the device:"
echo "-netdev tap,id=net0,ifname=vm0,script=no,downscript=no"
