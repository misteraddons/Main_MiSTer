#!/bin/bash

# MiSTer Auto-Update Setup Script
# Configures automatic updates using cron

SCRIPT_DIR="/media/fat/Scripts"
AUTO_UPDATE_SCRIPT="${SCRIPT_DIR}/auto_update.sh"
CRON_FILE="/media/fat/Scripts/crontab_auto_update"

echo "MiSTer Auto-Update Setup"
echo "========================"

# Check if auto_update.sh exists
if [ ! -f "${AUTO_UPDATE_SCRIPT}" ]; then
    echo "ERROR: auto_update.sh not found in ${SCRIPT_DIR}"
    echo "Please ensure the script is installed correctly."
    exit 1
fi

# Make sure the script is executable
chmod +x "${AUTO_UPDATE_SCRIPT}"

echo ""
echo "Choose update schedule:"
echo "1) Daily at 3 AM"
echo "2) Weekly (Sunday at 3 AM)"
echo "3) Monthly (1st of each month at 3 AM)"
echo "4) Custom schedule"
echo "5) Disable auto-updates"
echo ""
read -p "Enter your choice (1-5): " choice

case $choice in
    1)
        CRON_SCHEDULE="0 3 * * *"
        echo "Setting up daily updates at 3 AM..."
        ;;
    2)
        CRON_SCHEDULE="0 3 * * 0"
        echo "Setting up weekly updates (Sunday at 3 AM)..."
        ;;
    3)
        CRON_SCHEDULE="0 3 1 * *"
        echo "Setting up monthly updates (1st of month at 3 AM)..."
        ;;
    4)
        echo ""
        echo "Enter custom cron schedule"
        echo "Format: minute hour day month day_of_week"
        echo "Example: 0 3 * * * (daily at 3 AM)"
        read -p "Schedule: " CRON_SCHEDULE
        ;;
    5)
        echo "Removing auto-update from cron..."
        crontab -l 2>/dev/null | grep -v "${AUTO_UPDATE_SCRIPT}" | crontab -
        echo "Auto-updates disabled."
        exit 0
        ;;
    *)
        echo "Invalid choice. Exiting."
        exit 1
        ;;
esac

# Create cron entry
echo "${CRON_SCHEDULE} ${AUTO_UPDATE_SCRIPT} >/dev/null 2>&1" > "${CRON_FILE}"

# Install cron entry
echo ""
echo "Installing cron job..."

# Get current crontab (if exists) and remove old auto_update entries
crontab -l 2>/dev/null | grep -v "${AUTO_UPDATE_SCRIPT}" > /tmp/current_cron 2>/dev/null || true

# Add new entry
cat "${CRON_FILE}" >> /tmp/current_cron

# Install new crontab
if crontab /tmp/current_cron; then
    echo "Cron job installed successfully!"
    echo ""
    echo "Auto-update schedule: ${CRON_SCHEDULE}"
    echo ""
    echo "You can view logs in: ${SCRIPT_DIR}/logs/"
    echo "Configuration file: ${SCRIPT_DIR}/auto_update.ini"
    echo ""
    echo "To check cron status: crontab -l"
    echo "To disable: run this script again and choose option 5"
else
    echo "ERROR: Failed to install cron job"
    exit 1
fi

# Clean up
rm -f /tmp/current_cron "${CRON_FILE}"

echo ""
echo "Setup complete!"