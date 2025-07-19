#ifndef MENU_REFRESH_H
#define MENU_REFRESH_H

// Request a menu refresh (called by external command handlers)
void menu_request_refresh();

// Check if refresh was requested (called by HandleUI)
int menu_check_refresh();

// Handle the refresh
void menu_handle_refresh();

#endif // MENU_REFRESH_H