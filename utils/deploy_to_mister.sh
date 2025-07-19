#!/bin/bash
#
# Deploy MiSTer addon daemons to MiSTer device
#

MISTER_IP=${1:-}

if [ -z "$MISTER_IP" ]; then
    echo "Usage: $0 <mister_ip>"
    echo "Example: $0 192.168.1.100"
    exit 1
fi

echo "Deploying to MiSTer at $MISTER_IP..."

# Create directory structure on MiSTer
ssh root@$MISTER_IP "mkdir -p /media/fat/utils/{core,input,network,monitors}"

# Copy binaries
echo "Copying binaries..."
scp -r bin/* root@$MISTER_IP:/media/fat/utils/

# Copy configs
echo "Copying configuration files..."
for dir in core input network monitors; do
    for daemon in $dir/*/; do
        if [ -d "$daemon" ]; then
            daemon_name=$(basename $daemon)
            scp $daemon/*.conf root@$MISTER_IP:/media/fat/utils/ 2>/dev/null
        fi
    done
done

echo "Deployment complete!"
