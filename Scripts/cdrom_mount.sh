#!/bin/bash

# Define the device and mount point
DEVICE=/dev/sr0
MOUNT_POINT=/media/cdrom

# Create mount point if doesn't exist
if [ ! -d "$MOUNT_POINT" ]; then
    echo "$MOUNT_POINT doesn't exist. Creating now..."
    mkdir $MOUNT_POINT
fi

# Function to mount the CD drive
mount_cd() {
    if ! mount | grep $MOUNT_POINT > /dev/null; then
        echo "Mounting CD drive..."
        sudo mount $DEVICE $MOUNT_POINT
        if [ $? -eq 0 ]; then
            echo "CD drive mounted successfully at $MOUNT_POINT."
        else
            echo "Failed to mount CD drive."
            exit 1
        fi
    else
        echo "CD drive is already mounted."
    fi
}

# Function to read sectors off the CD to keep it spinning
keep_cd_active() {
    while true; do
        echo "Reading sectors to keep CD spinning..."
        sudo dd if=$DEVICE of=/dev/null bs=2048 count=10
        sleep 30
    done
}

# Main script execution
mount_cd
# keep_cd_active