#ifndef SATURN_RAMCART_H
#define SATURN_RAMCART_H

#include <stddef.h>
#include <stdint.h>

#define SATURN_RAM_CART_NONE 0
#define SATURN_RAM_CART_1M   2
#define SATURN_RAM_CART_4M   3
#define SATURN_RAM_CART_BACKUP 5

#define SATURN_RAM_CART_MODE_AUTO 0

#define SATURN_RAM_CART_MODE_STATUS_OPT "[23:21]"
#define SATURN_RAM_CART_AUTO_STATUS_OPT "[84:82]"
#define SATURN_RAM_CART_LEGACY_OPTION "O[23:21],Cartridge,None,ROM 2M,DRAM 1M,DRAM 4M,DRAM 6M DEV,BACKUP"
#define SATURN_RAM_CART_AUTO_OPTION   "O[23:21],Cartridge,Auto,ROM 2M,DRAM 1M,DRAM 4M,DRAM 6M DEV,BACKUP,None"
#define SATURN_RAM_CART_DB_PATH "cartridge.txt"

bool saturn_ramcart_make_uuid(const uint8_t *boot_header, char *uuid, size_t uuid_size);
bool saturn_ramcart_parse_db_line(const char *line, const char *uuid, uint32_t *cart_type);
const char *saturn_ramcart_type_name(uint32_t cart_type);

#endif
