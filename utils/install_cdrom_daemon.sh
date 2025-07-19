#!/bin/bash
#
# Install CD-ROM daemon for MiSTer
# This copies the daemon and sets it up to start at boot
#

DAEMON_NAME="cdrom_daemon"
DAEMON_PATH="/media/fat/utils/${DAEMON_NAME}"
STARTUP_SCRIPT="/media/fat/linux/_startup_cdrom_daemon.sh"

echo "Installing MiSTer CD-ROM daemon..."

# Check if daemon binary exists
if [ ! -f "./${DAEMON_NAME}" ]; then
    echo "Error: ${DAEMON_NAME} not found. Build it first with build_cdrom_daemon.sh"
    exit 1
fi

# Create utils directory if it doesn't exist
mkdir -p /media/fat/utils
mkdir -p /media/fat/utils/gamedb

# Copy daemon to system location
echo "Copying daemon to ${DAEMON_PATH}..."
cp ./${DAEMON_NAME} ${DAEMON_PATH}
chmod +x ${DAEMON_PATH}

# Copy sample config if it exists and no config exists
if [ -f "./cdrom_daemon.conf.sample" ] && [ ! -f "/media/fat/utils/cdrom_daemon.conf" ]; then
    echo "Installing sample configuration..."
    cp ./cdrom_daemon.conf.sample /media/fat/utils/cdrom_daemon.conf.sample
fi


# Create startup script for MiSTer
echo "Creating startup script..."
mkdir -p /media/fat/linux
cat > ${STARTUP_SCRIPT} << 'EOF'
#!/bin/bash
#
# MiSTer CD-ROM daemon startup script
#

DAEMON_PATH="/media/fat/utils/cdrom_daemon"
PIDFILE="/tmp/cdrom_daemon.pid"

# Check if daemon is already running
if [ -f "$PIDFILE" ]; then
    PID=$(cat "$PIDFILE")
    if kill -0 "$PID" 2>/dev/null; then
        echo "CD-ROM daemon already running (PID: $PID)"
        exit 0
    else
        # Stale PID file
        rm -f "$PIDFILE"
    fi
fi

# Start daemon if binary exists
if [ -f "$DAEMON_PATH" ]; then
    echo "Starting CD-ROM daemon..."
    $DAEMON_PATH &
    echo $! > "$PIDFILE"
    echo "CD-ROM daemon started (PID: $(cat $PIDFILE))"
else
    echo "CD-ROM daemon not found at $DAEMON_PATH"
fi
EOF

chmod +x ${STARTUP_SCRIPT}

# Start the daemon now
echo "Starting CD-ROM daemon..."
${STARTUP_SCRIPT}

# Check if it's running
sleep 2
if [ -f "/tmp/cdrom_daemon.pid" ]; then
    PID=$(cat /tmp/cdrom_daemon.pid)
    if kill -0 "$PID" 2>/dev/null; then
        echo ""
        echo "CD-ROM daemon installed and started successfully!"
        echo "PID: $PID"
        echo ""
        echo "The daemon will automatically start on boot via:"
        echo "  ${STARTUP_SCRIPT}"
        echo ""
        echo "Manual control:"
        echo "  ${STARTUP_SCRIPT}                    # Start daemon"
        echo "  kill \$(cat /tmp/cdrom_daemon.pid)   # Stop daemon"
        echo "  ${DAEMON_PATH} -f                    # Run in foreground"
        echo ""
        echo "Configuration:"
        echo "  cp /media/fat/utils/cdrom_daemon.conf.sample /media/fat/utils/cdrom_daemon.conf"
        echo "  Edit /media/fat/utils/cdrom_daemon.conf to customize behavior"
        echo ""
        echo "GameDB Setup:"
        echo "  Place GameDB files in /media/fat/utils/gamedb/"
        echo "  Required: PSX.data.json, Saturn.data.json, SegaCD.data.json, PCECD.data.json"
    else
        echo "Warning: Daemon may not have started properly"
    fi
else
    echo "Warning: Daemon may not have started properly"
fi