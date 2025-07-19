#!/bin/bash
#
# MiSTer Network Game Launcher - Shell Script Client
#
# Simple command-line interface for launching games on MiSTer
#

MISTER_IP="${MISTER_IP:-192.168.1.100}"
MISTER_PORT="${MISTER_PORT:-8080}"
API_KEY="${MISTER_API_KEY:-}"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Print colored output
print_status() {
    echo -e "${GREEN}✓${NC} $1"
}

print_error() {
    echo -e "${RED}✗${NC} $1" >&2
}

print_warning() {
    echo -e "${YELLOW}⚠${NC} $1"
}

print_info() {
    echo -e "${BLUE}ℹ${NC} $1"
}

# Show usage
show_usage() {
    cat << EOF
MiSTer Network Game Launcher

Usage: $0 [OPTIONS] COMMAND

Commands:
  status                    Get MiSTer system status
  launch CORE ID_TYPE ID    Launch a game
  test                      Test connection to MiSTer
  info                      Show API information

Options:
  -h, --host IP            MiSTer IP address (default: $MISTER_IP)
  -p, --port PORT          MiSTer port (default: $MISTER_PORT)
  -k, --api-key KEY        API key for authentication
  --help                   Show this help message

Environment Variables:
  MISTER_IP               Default MiSTer IP address
  MISTER_PORT             Default MiSTer port
  MISTER_API_KEY          Default API key

Examples:
  # Test connection
  $0 test

  # Get system status
  $0 status

  # Launch PlayStation game by serial
  $0 launch PSX serial SLUS-00067

  # Launch Saturn game with custom IP
  $0 -h 192.168.0.100 launch Saturn serial T-8107G

  # Launch game by title with API key
  $0 -k mykey launch PSX title "Metal Gear Solid"

Supported Cores:
  PSX, Saturn, MegaCD, Arcade, NES, SNES, Genesis, etc.

ID Types:
  serial - Game serial number (SLUS-00067, T-8107G, etc.)
  title  - Game title ("Metal Gear Solid", etc.)
EOF
}

# Make HTTP request with curl
make_request() {
    local method="$1"
    local endpoint="$2"
    local data="$3"
    
    local url="http://${MISTER_IP}:${MISTER_PORT}${endpoint}"
    local curl_args=("-s" "-X" "$method")
    
    if [[ -n "$API_KEY" ]]; then
        curl_args+=("-H" "Authorization: $API_KEY")
    fi
    
    if [[ -n "$data" ]]; then
        curl_args+=("-H" "Content-Type: application/json" "-d" "$data")
    fi
    
    curl_args+=("$url")
    
    curl "${curl_args[@]}"
}

# Test connection
test_connection() {
    print_info "Testing connection to $MISTER_IP:$MISTER_PORT..."
    
    local response
    response=$(make_request "GET" "/status" 2>/dev/null)
    local exit_code=$?
    
    if [[ $exit_code -eq 0 ]] && echo "$response" | grep -q '"status":"running"'; then
        print_status "Connection successful!"
        local port=$(echo "$response" | grep -o '"port":[0-9]*' | cut -d: -f2)
        local game_launcher=$(echo "$response" | grep -o '"game_launcher_available":[^,]*' | cut -d: -f2)
        print_info "MiSTer is running on port $port"
        print_info "Game launcher available: $game_launcher"
        return 0
    else
        print_error "Connection failed!"
        print_error "Could not reach MiSTer at $MISTER_IP:$MISTER_PORT"
        return 1
    fi
}

# Get system status
get_status() {
    print_info "Getting MiSTer status..."
    
    local response
    response=$(make_request "GET" "/status")
    local exit_code=$?
    
    if [[ $exit_code -eq 0 ]]; then
        echo "$response" | python3 -m json.tool 2>/dev/null || echo "$response"
    else
        print_error "Failed to get status"
        return 1
    fi
}

# Get API information
get_info() {
    print_info "Getting API information..."
    
    local response
    response=$(make_request "GET" "/api")
    local exit_code=$?
    
    if [[ $exit_code -eq 0 ]]; then
        echo "$response" | python3 -m json.tool 2>/dev/null || echo "$response"
    else
        print_error "Failed to get API info"
        return 1
    fi
}

# Launch a game
launch_game() {
    local core="$1"
    local id_type="$2"
    local identifier="$3"
    
    if [[ -z "$core" || -z "$id_type" || -z "$identifier" ]]; then
        print_error "Missing required arguments for launch command"
        echo "Usage: $0 launch CORE ID_TYPE IDENTIFIER"
        return 1
    fi
    
    print_info "Launching $core game: $identifier"
    
    local payload
    payload=$(cat << EOF
{
    "core": "$core",
    "id_type": "$id_type",
    "identifier": "$identifier"
}
EOF
)
    
    local response
    response=$(make_request "POST" "/launch" "$payload")
    local exit_code=$?
    
    if [[ $exit_code -eq 0 ]]; then
        if echo "$response" | grep -q '"success":true'; then
            local message=$(echo "$response" | grep -o '"message":"[^"]*"' | cut -d'"' -f4)
            print_status "Game launch successful: $message"
        else
            local error=$(echo "$response" | grep -o '"error":"[^"]*"' | cut -d'"' -f4)
            print_error "Launch failed: ${error:-Unknown error}"
            return 1
        fi
    else
        print_error "Failed to send launch request"
        return 1
    fi
}

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        -h|--host)
            MISTER_IP="$2"
            shift 2
            ;;
        -p|--port)
            MISTER_PORT="$2"
            shift 2
            ;;
        -k|--api-key)
            API_KEY="$2"
            shift 2
            ;;
        --help)
            show_usage
            exit 0
            ;;
        *)
            break
            ;;
    esac
done

# Check if curl is available
if ! command -v curl &> /dev/null; then
    print_error "curl is required but not installed"
    exit 1
fi

# Handle commands
case "$1" in
    test)
        test_connection
        ;;
    status)
        get_status
        ;;
    info)
        get_info
        ;;
    launch)
        launch_game "$2" "$3" "$4"
        ;;
    "")
        print_error "No command specified"
        show_usage
        exit 1
        ;;
    *)
        print_error "Unknown command: $1"
        show_usage
        exit 1
        ;;
esac