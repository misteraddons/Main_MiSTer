#!/bin/bash

# MiSTer Auto-Update Script
# Automatically runs MiSTer update scripts on a schedule
# Can be configured to run via cron or systemd timer

SCRIPT_DIR="/media/fat/Scripts"
LOG_DIR="/media/fat/Scripts/logs"
LOG_FILE="${LOG_DIR}/auto_update_$(date +%Y%m%d_%H%M%S).log"
CONFIG_FILE="/media/fat/Scripts/auto_update.ini"

# Create log directory if it doesn't exist
mkdir -p "${LOG_DIR}"

# Function to log messages
log_message() {
    echo "[$(date '+%Y-%m-%d %H:%M:%S')] $1" | tee -a "${LOG_FILE}"
}

# Function to check if we should run updates
should_update() {
    # Check if auto-update is enabled
    if [ -f "${CONFIG_FILE}" ]; then
        ENABLED=$(grep -E "^auto_update_enabled\s*=\s*yes" "${CONFIG_FILE}" 2>/dev/null)
        if [ -z "${ENABLED}" ]; then
            log_message "Auto-update is disabled in config"
            return 1
        fi
    fi
    
    # Check if we're in the allowed time window
    CURRENT_HOUR=$(date +%H)
    UPDATE_HOUR_START=${UPDATE_HOUR_START:-2}  # Default: 2 AM
    UPDATE_HOUR_END=${UPDATE_HOUR_END:-5}      # Default: 5 AM
    
    if [ "${CURRENT_HOUR}" -lt "${UPDATE_HOUR_START}" ] || [ "${CURRENT_HOUR}" -gt "${UPDATE_HOUR_END}" ]; then
        log_message "Outside update time window (${UPDATE_HOUR_START}:00-${UPDATE_HOUR_END}:00)"
        return 1
    fi
    
    return 0
}

# Function to check network connectivity
check_network() {
    log_message "Checking network connectivity..."
    if ping -c 1 -W 5 github.com >/dev/null 2>&1; then
        log_message "Network connection OK"
        return 0
    else
        log_message "ERROR: No network connection"
        return 1
    fi
}

# Function to run update script
run_update() {
    local update_script="$1"
    local script_name=$(basename "${update_script}")
    
    if [ ! -f "${update_script}" ]; then
        log_message "WARNING: Update script not found: ${update_script}"
        return 1
    fi
    
    log_message "Running ${script_name}..."
    
    # Run the update script and capture output
    if bash "${update_script}" >> "${LOG_FILE}" 2>&1; then
        log_message "${script_name} completed successfully"
        return 0
    else
        log_message "ERROR: ${script_name} failed with exit code $?"
        return 1
    fi
}

# Main update process
main() {
    log_message "Starting MiSTer auto-update process"
    
    # Check if we should run updates
    if ! should_update; then
        exit 0
    fi
    
    # Check network connectivity
    if ! check_network; then
        exit 1
    fi
    
    # Determine which update script to use
    UPDATE_SCRIPT=""
    
    # Priority order: update_all.sh > update.sh > downloader.sh
    if [ -f "${SCRIPT_DIR}/update_all.sh" ]; then
        UPDATE_SCRIPT="${SCRIPT_DIR}/update_all.sh"
        log_message "Using update_all.sh (comprehensive updates)"
    elif [ -f "${SCRIPT_DIR}/update.sh" ]; then
        UPDATE_SCRIPT="${SCRIPT_DIR}/update.sh"
        log_message "Using update.sh (official updates)"
    elif [ -f "${SCRIPT_DIR}/downloader.sh" ]; then
        UPDATE_SCRIPT="${SCRIPT_DIR}/downloader.sh"
        log_message "Using downloader.sh (official updates)"
    else
        log_message "ERROR: No update script found in ${SCRIPT_DIR}"
        exit 1
    fi
    
    # Run the update
    if run_update "${UPDATE_SCRIPT}"; then
        log_message "Auto-update completed successfully"
        
        # Clean up old logs (keep last 10)
        log_message "Cleaning up old log files..."
        ls -t "${LOG_DIR}"/auto_update_*.log 2>/dev/null | tail -n +11 | xargs -r rm -f
    else
        log_message "Auto-update failed"
        exit 1
    fi
    
    log_message "Auto-update process finished"
}

# Run main function
main "$@"