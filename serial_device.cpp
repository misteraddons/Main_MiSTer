#include "serial_device.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

namespace
{
	bool join_path(char *path, size_t path_size, const char *base, const char *leaf)
	{
		size_t base_length = strlen(base);
		size_t leaf_length = strlen(leaf);
		bool separator = base_length && base[base_length - 1] != '/';
		if (base_length + (separator ? 1 : 0) + leaf_length + 1 > path_size)
		{
			errno = ENAMETOOLONG;
			return false;
		}
		memcpy(path, base, base_length);
		size_t offset = base_length;
		if (separator) path[offset++] = '/';
		memcpy(path + offset, leaf, leaf_length + 1);
		return true;
	}

	bool is_hex4(const char *value)
	{
		for (int i = 0; i < 4; i++)
			if (!isxdigit((unsigned char)value[i])) return false;
		return true;
	}

	bool parse_usb_selector(const char *selector, char *vid, char *pid,
		const char **serial)
	{
		if (!selector || strlen(selector) < 9 || selector[4] != '_' ||
			!is_hex4(selector) || !is_hex4(selector + 5)) return false;
		if (selector[9] && selector[9] != '_') return false;

		snprintf(vid, 5, "%.4s", selector);
		snprintf(pid, 5, "%.4s", selector + 5);
		*serial = selector[9] == '_' ? selector + 10 : "";
		return true;
	}

	bool read_text(const char *path, char *value, size_t value_size)
	{
		FILE *file = fopen(path, "r");
		if (!file) return false;
		bool read = fgets(value, (int)value_size, file) != 0;
		fclose(file);
		if (!read) return false;
		value[strcspn(value, "\r\n")] = 0;
		return true;
	}

	bool find_usb_identity(const char *device_path, char *vid, char *pid,
		char *serial, size_t serial_size)
	{
		char current[PATH_MAX];
		snprintf(current, sizeof(current), "%s", device_path);
		while (current[0] == '/')
		{
			char vid_path[PATH_MAX];
			char pid_path[PATH_MAX];
			if (!join_path(vid_path, sizeof(vid_path), current, "idVendor") ||
				!join_path(pid_path, sizeof(pid_path), current, "idProduct")) return false;
			if (read_text(vid_path, vid, 5) && read_text(pid_path, pid, 5))
			{
				char serial_path[PATH_MAX];
				if (!join_path(serial_path, sizeof(serial_path), current, "serial")) return false;
				if (!read_text(serial_path, serial, serial_size)) serial[0] = 0;
				return true;
			}

			char *slash = strrchr(current, '/');
			if (!slash || slash == current) break;
			*slash = 0;
		}
		return false;
	}

}

bool serial_device_resolve(const char *selector, char *path, size_t path_size)
{
	if (!path || !path_size || !selector || !*selector)
	{
		errno = EINVAL;
		return false;
	}
	path[0] = 0;
	if (selector[0] == '/')
	{
		if (snprintf(path, path_size, "%s", selector) >= (int)path_size)
		{
			errno = ENAMETOOLONG;
			return false;
		}
		return true;
	}

	char wanted_vid[5] = {};
	char wanted_pid[5] = {};
	const char *wanted_serial = "";
	if (!parse_usb_selector(selector, wanted_vid, wanted_pid, &wanted_serial))
	{
		errno = EINVAL;
		return false;
	}
	const char *sysfs_root = getenv("MISTER_SERIAL_SYSFS_ROOT");
	const char *dev_root = getenv("MISTER_SERIAL_DEV_ROOT");
	if (!sysfs_root || !*sysfs_root) sysfs_root = "/sys/class/tty";
	if (!dev_root || !*dev_root) dev_root = "/dev";

	DIR *directory = opendir(sysfs_root);
	if (!directory) return false;
	int matches = 0;
	struct dirent *entry;
	while ((entry = readdir(directory)))
	{
		if (strncmp(entry->d_name, "ttyACM", 6) && strncmp(entry->d_name, "ttyUSB", 6)) continue;
		char tty_path[PATH_MAX];
		char link_path[PATH_MAX];
		char resolved_path[PATH_MAX];
		if (!join_path(tty_path, sizeof(tty_path), sysfs_root, entry->d_name) ||
			!join_path(link_path, sizeof(link_path), tty_path, "device")) continue;
		if (!realpath(link_path, resolved_path)) continue;

		char vid[5] = {};
		char pid[5] = {};
		char serial[128] = {};
		if (!find_usb_identity(resolved_path, vid, pid, serial, sizeof(serial))) continue;
		bool match = !strcasecmp(vid, wanted_vid) && !strcasecmp(pid, wanted_pid) &&
			(!*wanted_serial || !strcasecmp(serial, wanted_serial));
		if (!match) continue;

		matches++;
		if (matches == 1)
		{
			if (!join_path(path, path_size, dev_root, entry->d_name))
			{
				closedir(directory);
				path[0] = 0;
				errno = ENAMETOOLONG;
				return false;
			}
		}
	}
	closedir(directory);

	if (matches == 1 && path[0]) return true;
	path[0] = 0;
	errno = matches > 1 ? EEXIST : ENOENT;
	return false;
}
