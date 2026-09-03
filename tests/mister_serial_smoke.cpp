#include <fcntl.h>
#include <assert.h>
#include <pty.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <string>

#include "mister_serial.h"

const char *version = "$VER:test-main";

namespace
{
	std::string collect(int master, int timeout_ms)
	{
		std::string result;
		for (int elapsed = 0; elapsed < timeout_ms; elapsed++)
		{
			mister_serial_poll();
			char buffer[2048];
			ssize_t count = read(master, buffer, sizeof(buffer));
			if (count > 0) result.append(buffer, (size_t)count);
			usleep(1000);
		}
		return result;
	}

	void require(const std::string &text, const char *needle)
	{
		if (text.find(needle) != std::string::npos) return;
		fprintf(stderr, "missing: %s\noutput:\n%s\n", needle, text.c_str());
		exit(1);
	}

	void make_pty(int *master, int *slave, char *name, size_t name_size)
	{
		char path[128] = {};
		if (openpty(master, slave, path, 0, 0))
		{
			perror("openpty");
			exit(1);
		}
		snprintf(name, name_size, "%s", path);
		fcntl(*master, F_SETFL, fcntl(*master, F_GETFL) | O_NONBLOCK);
	}
}

int main()
{
	char link_path[128];
	snprintf(link_path, sizeof(link_path), "/tmp/mister-status-serial-%d", getpid());

	int master1, slave1;
	char slave_name1[128];
	make_pty(&master1, &slave1, slave_name1, sizeof(slave_name1));
	unlink(link_path);
	if (symlink(slave_name1, link_path))
	{
		perror("symlink");
		return 1;
	}

	mister_serial_init(link_path, "SNES");
	close(slave1);
	std::string initial = collect(master1, 100);
	require(initial, "MISTER_STATUS/1\t1\tHELLO\tmain=test-main");
	require(initial, "caps=core,game,load,state,video,osd,preview,controller,net");
	require(initial, "\tNET\ttype=lan\tpresent=");
	require(initial, "\tNET\ttype=wlan\tpresent=");
	require(initial, "\tCORE\tname=SNES\ttime_us=");
	require(initial, "\tGAME\tstate=empty\tname=\tcrc32=\tserial=\ttime_us=");
	require(initial, "\tSTATE\tstate=active\ttime_us=");
	require(initial, "snapshot=1");


	mister_serial_set_osd(1);
	mister_serial_set_preview("core", "Genesis", "/media/fat/_Console/Genesis_20260101.rbf");
	mister_serial_controller_scan_begin();
	mister_serial_controller_scan_add("usb-1", "DualShock 4", 0x054c, 0x09cc, 1);
	mister_serial_controller_scan_end();
	std::string surfaces = collect(master1, 650);
	require(surfaces, "\tOSD\tvisible=1\ttime_us=");
	require(surfaces, "\tPREVIEW\tstate=active\tkind=core\tname=Genesis\tpath=/media/fat/_Console/Genesis_20260101.rbf\ttime_us=");
	require(surfaces, "\tCONTROLLER\tstate=present\tplayer=1\tvid=054c\tpid=09cc\tid=usb-1\tname=DualShock 4\ttime_us=");

	mister_serial_controller_scan_begin();
	mister_serial_controller_scan_add("usb-1", "DualShock 4", 0x054c, 0x09cc, 1);
	mister_serial_controller_scan_add("usb-2", "Reflex Adapt", 0x16d0, 0x1460, 2);
	mister_serial_controller_scan_end();
	mister_serial_controller_player("usb-2", 3);
	std::string connected = collect(master1, 100);
	require(connected, "\tCONTROLLER\tstate=connected\tplayer=2\tvid=16d0\tpid=1460\tid=usb-2");
	require(connected, "\tCONTROLLER\tstate=assigned\tplayer=3\tvid=16d0\tpid=1460\tid=usb-2");
	mister_serial_set_game_loading("/media/fat/games/SNES/Super Mario World.sfc");
	mister_serial_set_progress("Loading", "Super Mario World.sfc", 0, 100);
	mister_serial_set_progress("Loading", "Super Mario World.sfc", 50, 100);
	mister_serial_set_progress(0, 0, 0, 0);
	mister_serial_set_game("/media/fat/games/SNES/Super Mario World.sfc",
		0x42CF9B5B, "SNS-MW-USA");
	mister_serial_set_video(256, 224, 60098, 0, 1920, 1080, 59940, 0);
	mister_serial_set_idle(1);
	std::string events = collect(master1, 100);
	require(events, "\tGAME\tstate=loading\tname=Super Mario World\tcrc32=\tserial=\ttime_us=");
	require(events, "\tGAME\tstate=loaded\tname=Super Mario World\tcrc32=42CF9B5B\tserial=SNS-MW-USA\ttime_us=");
	require(events, "\tLOAD\tstate=start\tpercent=0\ttime_us=");
	if (events.find("\tGAME\tstate=loading") > events.find("\tLOAD\tstate=start")) return 1;
	require(events, "\tLOAD\tstate=progress\tpercent=50\ttime_us=");
	require(events, "\tLOAD\tstate=done\tpercent=100\ttime_us=");
	require(events, "\tVIDEO\tsource_width=256\tsource_height=224\tsource_refresh_millihz=60098\tinterlaced=0\toutput_width=1920\toutput_height=1080\toutput_refresh_millihz=59940\tmode=scaled\ttime_us=");
	require(events, "\tSTATE\tstate=idle\ttime_us=");
	if (events.find("\tGAMEID\t") != std::string::npos) return 1;

	// Menu completion may commit the same title again without identifiers.
	// It must not clear the atomic GAME metadata or emit a duplicate event.
	mister_serial_set_game("/media/fat/games/SNES/Super Mario World.sfc");
	std::string duplicate_game = collect(master1, 50);
	if (duplicate_game.find("\tGAME\t") != std::string::npos) return 1;

	mister_serial_controller_scan_begin();
	mister_serial_controller_scan_add("usb-2", "Reflex Adapt", 0x16d0, 0x1460, 3);
	mister_serial_controller_scan_end();
	std::string disconnected = collect(master1, 100);
	require(disconnected, "\tCONTROLLER\tstate=disconnected\tplayer=1\tvid=054c\tpid=09cc\tid=usb-1");

	std::string quiet = collect(master1, 2100);
	if (quiet.find("\tHEARTBEAT\t") != std::string::npos) return 1;
	close(master1);

	usleep(300000);
	collect(-1, 20);
	unlink(link_path);

	int master2, slave2;
	char slave_name2[128];
	make_pty(&master2, &slave2, slave_name2, sizeof(slave_name2));
	if (symlink(slave_name2, link_path))
	{
		perror("symlink reconnect");
		return 1;
	}
	close(slave2);
	usleep(1100000);
	std::string replay = collect(master2, 150);
	require(replay, "\tHELLO\tmain=test-main");
	require(replay, "\tNET\ttype=lan\tpresent=");
	require(replay, "\tNET\ttype=wlan\tpresent=");
	require(replay, "\tCORE\tname=SNES\ttime_us=");
	require(replay, "\tGAME\tstate=loaded\tname=Super Mario World\tcrc32=42CF9B5B\tserial=SNS-MW-USA");
	if (replay.find("\tLOAD\t") != std::string::npos)
	{
		fprintf(stderr, "completed LOAD state was replayed:\n%s\n", replay.c_str());
		return 1;
	}
	require(replay, "\tVIDEO\tsource_width=256");
	require(replay, "\tSTATE\tstate=idle\ttime_us=");
	require(replay, "\tOSD\tvisible=1\ttime_us=");
	require(replay, "\tPREVIEW\tstate=active\tkind=core\tname=Genesis");
	require(replay, "\tCONTROLLER\tstate=present\tplayer=3\tvid=16d0\tpid=1460\tid=usb-2");

	close(master2);
	unlink(link_path);
	puts("mister_serial smoke test passed");
	return 0;
}
