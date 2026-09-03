#ifndef SERIAL_DEVICE_H
#define SERIAL_DEVICE_H

#include <stddef.h>

bool serial_device_resolve(const char *selector, char *path, size_t path_size);

#endif
