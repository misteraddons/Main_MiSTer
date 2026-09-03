#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <initializer_list>
#include <string>

#include "serial_device.h"

namespace
{
	std::string path(std::initializer_list<std::string> parts)
	{
		std::string result;
		for (const std::string &part : parts)
		{
			if (!result.empty() && result.back() != '/') result += '/';
			result += part;
		}
		return result;
	}

	void make_directory(const std::string &directory)
	{
		if (mkdir(directory.c_str(), 0700) && errno != EEXIST)
		{
			perror(directory.c_str());
			exit(1);
		}
	}

	void write_text(const std::string &filename, const char *value)
	{
		FILE *file = fopen(filename.c_str(), "w");
		if (!file)
		{
			perror(filename.c_str());
			exit(1);
		}
		fputs(value, file);
		fclose(file);
	}

	void add_device(const std::string &root, const char *tty,
		const char *usb_path, const char *serial)
	{
		std::string usb_device = path({root, "devices", usb_path});
		make_directory(usb_device.substr(0, usb_device.rfind('/')));
		make_directory(usb_device);
		write_text(path({usb_device, "idVendor"}), "16d0\n");
		write_text(path({usb_device, "idProduct"}), "14f7\n");
		write_text(path({usb_device, "serial"}), serial);

		std::string interface = path({usb_device, "1-1:1.0"});
		make_directory(interface);
		make_directory(path({interface, "tty"}));
		make_directory(path({interface, "tty", tty}));
		std::string class_tty = path({root, "class", "tty", tty});
		make_directory(class_tty);
		std::string device_link = path({class_tty, "device"});
		if (symlink(interface.c_str(), device_link.c_str()))
		{
			perror(device_link.c_str());
			exit(1);
		}
	}
}

int main()
{
	char root_buffer[] = "/tmp/mister-serial-device-XXXXXX";
	assert(mkdtemp(root_buffer));
	std::string root = root_buffer;
	for (const char *directory : {"class", "class/tty", "devices", "dev"})
		make_directory(path({root, directory}));
	add_device(root, "ttyACM0", "usb1/1-1", "NOVA1234\n");

	std::string sysfs = path({root, "class", "tty"});
	std::string dev = path({root, "dev"});
	setenv("MISTER_SERIAL_SYSFS_ROOT", sysfs.c_str(), 1);
	setenv("MISTER_SERIAL_DEV_ROOT", dev.c_str(), 1);

	char resolved[PATH_MAX];
	assert(serial_device_resolve("16d0_14f7", resolved, sizeof(resolved)));
	assert(strstr(resolved, "/dev/ttyACM0"));
	assert(serial_device_resolve("16D0_14F7_NOVA1234", resolved, sizeof(resolved)));
	assert(!serial_device_resolve("1-1:1.0", resolved, sizeof(resolved)) && errno == EINVAL);
	assert(serial_device_resolve("/dev/serial/by-id/example", resolved, sizeof(resolved)));
	assert(!strcmp(resolved, "/dev/serial/by-id/example"));
	assert(!serial_device_resolve("054c_09cc", resolved, sizeof(resolved)) && errno == ENOENT);

	add_device(root, "ttyACM1", "usb2/2-1", "NOVA5678\n");
	assert(!serial_device_resolve("16d0_14f7", resolved, sizeof(resolved)) && errno == EEXIST);
	assert(serial_device_resolve("16d0_14f7_NOVA1234", resolved, sizeof(resolved)));
	assert(strstr(resolved, "/dev/ttyACM0"));

	puts("serial device resolver test passed");
	return 0;
}
