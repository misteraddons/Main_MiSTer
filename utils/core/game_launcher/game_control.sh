#!/bin/bash
#
# MiSTer Game Control Utility
#
# Easy command-line interface for game launcher features
#

FIFO="/dev/MiSTer_game_launcher"

show_help() {
    echo "MiSTer Game Control Utility"
    echo "=========================="
    echo ""
    echo "Usage: $0 <command> [options]"
    echo ""
    echo "Commands:"
    echo "  launch <core> <id_type> <identifier>  - Launch specific game"
    echo "  random-favorite                       - Launch random favorite game"
    echo "  random-game <core>                    - Launch random game from core"
    echo "  last-played                           - Launch last played game"
    echo "  add-favorite <core> <id_type> <id> <title> - Add game to favorites"
    echo "  remove-favorite <core> <identifier>   - Remove game from favorites"
    echo "  list-favorites                        - Show favorites count"
    echo ""
    echo "Examples:"
    echo "  $0 launch PSX serial SLUS-00067"
    echo "  $0 random-favorite"
    echo "  $0 random-game PSX"
    echo "  $0 add-favorite PSX serial SLUS-00067 \"Castlevania SOTN\""
    echo "  $0 remove-favorite PSX SLUS-00067"
    echo ""
    echo "Supported Cores:"
    echo "  PSX, Saturn, MegaCD, SNES, Genesis, NES, SMS, GG"
    echo "  PCE, TG16, Gameboy, GBA, Atari2600, C64, Amiga, Arcade"
    echo ""
    echo "ID Types:"
    echo "  serial - Game serial number (SLUS-00067)"
    echo "  title  - Game title (Super Metroid)"
    echo "  path   - File path (/games/PSX/game.chd)"
}

check_fifo() {
    if [ ! -p "$FIFO" ]; then
        echo "Error: Game launcher service not running"
        echo "Please start the game launcher service first:"
        echo "  /media/fat/utils/game_launcher"
        exit 1
    fi
}

send_command() {
    echo "$1" > "$FIFO"
    if [ $? -eq 0 ]; then
        echo "Command sent successfully"
    else
        echo "Error: Failed to send command"
        exit 1
    fi
}

case "$1" in
    "launch")
        if [ $# -ne 4 ]; then
            echo "Usage: $0 launch <core> <id_type> <identifier>"
            exit 1
        fi
        check_fifo
        send_command "$2:$3:$4:manual"
        ;;
    
    "random-favorite")
        check_fifo
        send_command "COMMAND:random_favorite:"
        ;;
    
    "random-game")
        if [ $# -ne 2 ]; then
            echo "Usage: $0 random-game <core>"
            exit 1
        fi
        check_fifo
        send_command "COMMAND:random_game:$2"
        ;;
    
    "last-played")
        check_fifo
        send_command "COMMAND:last_played:"
        ;;
    
    "add-favorite")
        if [ $# -ne 5 ]; then
            echo "Usage: $0 add-favorite <core> <id_type> <identifier> <title>"
            exit 1
        fi
        check_fifo
        send_command "COMMAND:add_favorite:$2,$3,$4,$5"
        ;;
    
    "remove-favorite")
        if [ $# -ne 3 ]; then
            echo "Usage: $0 remove-favorite <core> <identifier>"
            exit 1
        fi
        check_fifo
        send_command "COMMAND:remove_favorite:$2,$3"
        ;;
    
    "list-favorites")
        check_fifo
        send_command "COMMAND:list_favorites:"
        ;;
    
    "help"|"-h"|"--help"|"")
        show_help
        ;;
    
    *)
        echo "Error: Unknown command '$1'"
        echo "Use '$0 help' for usage information"
        exit 1
        ;;
esac