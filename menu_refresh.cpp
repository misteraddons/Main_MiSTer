/*
 * Menu Refresh System
 * 
 * Simple system to allow external processes to request menu refreshes
 * This is kept separate to minimize changes to menu.cpp
 */

#include "menu_refresh.h"
#include "file_io.h"
#include "menu.h"

static volatile int refresh_requested = 0;

void menu_request_refresh() {
    refresh_requested = 1;
}

int menu_check_refresh() {
    if (refresh_requested) {
        refresh_requested = 0;
        return 1;
    }
    return 0;
}

void menu_handle_refresh() {
    // Simply call PrintDirectory to refresh the current view
    PrintDirectory();
}