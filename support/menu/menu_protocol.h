#ifndef MISTER_MENU_PROTOCOL_H
#define MISTER_MENU_PROTOCOL_H
#include <stdint.h>

// Local SOCK_SEQPACKET protocol on the descriptor passed to --mister-menu-v1.
// Both executables run on the same host; integers use native byte order.
// Context supplies the active OEM INI filename and current Menu RBF path.
// Send Ready after initializing, then one Launch/Reload/Fallback and exit.
// Main reaps the entire process tree before resuming. No frame/input IPC.
#define MISTER_MENU_ABI "MISTER_MENU/1"
enum MenuOperation { MenuContext = 0, MenuReady, MenuLaunch, MenuReload, MenuFallback };
struct MenuPacket
{
	char abi[16];
	uint32_t operation;
	char ini[64];
	char path[1024];
};

#endif
