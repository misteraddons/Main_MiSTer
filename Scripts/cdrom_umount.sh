#!/bin/bash

# Define the device and mount point
# DEVICE=/dev/sr0
MOUNT_POINT=/media/cdrom

# Create mount point if doesn't exist
if [ ! -d "$MOUNT_POINT" ]; then
    echo "$MOUNT_POINT doesn't exist. Creating now..."
    mkdir $MOUNT_POINT
fi

# Function to mount the CD drive
umount_cd() {
    # if mount | grep $MOUNT_POINT > /dev/null; then
    #     echo "Unmounting CD drive..."
    sudo umount $MOUNT_POINT
    if [ $? -eq 0 ]; then
        echo "CD drive unmounted successfully at $MOUNT_POINT."
    else
        echo "Failed to unmount CD drive."
        exit 1
    fi
    # else
    #     echo "CD drive isn't already mounted."
    # fi
}

# Main script execution
umount_cd
