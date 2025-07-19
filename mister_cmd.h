#ifndef MISTER_CMD_H
#define MISTER_CMD_H

// Initialize the MiSTer command interface
void mister_cmd_init();

// Cleanup the command interface
void mister_cmd_cleanup();

// This is now handled by menu_refresh.h

// Define CD-ROM selection menu type
#define MENU_CDROM_SELECT 0x80

// CD-ROM scan option
#define SCANO_CDROM 0x10000

#endif // MISTER_CMD_H