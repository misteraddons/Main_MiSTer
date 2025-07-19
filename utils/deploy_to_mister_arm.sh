#!/bin/bash
#
# Deploy MiSTer addon ARM binaries to MiSTer device
#

MISTER_IP=${1:-}
PASSWORD=${2:-1}

if [ -z "$MISTER_IP" ]; then
    echo "Usage: $0 <mister_ip> [password]"
    echo "Example: $0 192.168.1.100 1"
    exit 1
fi

echo "Deploying ARM binaries to MiSTer at $MISTER_IP..."
echo "Password: $PASSWORD"

# Function to run SSH commands with password
run_ssh() {
    sshpass -p "$PASSWORD" ssh -o StrictHostKeyChecking=no root@$MISTER_IP "$1"
}

# Function to copy files with password  
copy_files() {
    sshpass -p "$PASSWORD" scp -o StrictHostKeyChecking=no "$1" root@$MISTER_IP:"$2"
}

# Check if sshpass is available
if ! command -v sshpass &> /dev/null; then
    echo "sshpass is required but not installed. Installing..."
    sudo apt-get update && sudo apt-get install -y sshpass
fi

# Test connection
echo "Testing connection..."
if ! run_ssh "echo 'Connection successful'"; then
    echo "Failed to connect to MiSTer at $MISTER_IP"
    exit 1
fi

# Create directory structure on MiSTer
echo "Creating directory structure..."
run_ssh "mkdir -p /media/fat/utils"

# Copy ARM binaries
echo "Copying ARM binaries..."
copy_files "bin_arm/*" "/media/fat/utils/"

# Copy configuration files
echo "Copying configuration files..."
for dir in core input network monitors; do
    for daemon_dir in $dir/*/; do
        if [ -d "$daemon_dir" ]; then
            daemon_name=$(basename "$daemon_dir")
            if [ -f "$daemon_dir$daemon_name.conf" ]; then
                copy_files "$daemon_dir$daemon_name.conf" "/media/fat/utils/"
            fi
        fi
    done
done

# Set permissions
echo "Setting permissions..."
run_ssh "chmod +x /media/fat/utils/*"

# Create startup script
echo "Creating startup script..."
cat > /tmp/start_game_services.sh << 'EOF'
#!/bin/bash
#
# Start MiSTer Game Services
#

echo "Starting MiSTer Game Services..."

# Start game launcher (core service)
if [ -f /media/fat/utils/game_launcher ]; then
    echo "Starting game launcher..."
    /media/fat/utils/game_launcher &
    sleep 2
fi

# Start other daemons
for daemon in cdrom_daemon nfc_daemon uart_daemon gpio_daemon filesystem_daemon network_daemon; do
    if [ -f /media/fat/utils/$daemon ]; then
        echo "Starting $daemon..."
        /media/fat/utils/$daemon &
        sleep 1
    fi
done

echo "Services started. Check with 'ps aux | grep -E \"game_launcher|daemon\"'"
EOF

copy_files "/tmp/start_game_services.sh" "/media/fat/utils/"
run_ssh "chmod +x /media/fat/utils/start_game_services.sh"

# Test game_launcher
echo "Testing game_launcher..."
if run_ssh "file /media/fat/utils/game_launcher | grep -q 'ARM'"; then
    echo "✓ game_launcher is ARM binary"
else
    echo "✗ game_launcher binary check failed"
fi

echo ""
echo "=========================================="
echo "Deployment Complete!"
echo "=========================================="
echo ""
echo "To start services on MiSTer:"
echo "  ssh root@$MISTER_IP"
echo "  /media/fat/utils/start_game_services.sh"
echo ""
echo "To start individual services:"
echo "  /media/fat/utils/game_launcher &"
echo "  /media/fat/utils/attract_mode --start &"
echo "  /media/fat/utils/nfc_daemon &"
echo ""
echo "To check running services:"
echo "  ps aux | grep -E \"game_launcher|daemon\""