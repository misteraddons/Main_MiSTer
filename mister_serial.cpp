#include "mister_serial.h"
#include "serial_device.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <netpacket/packet.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

extern const char *version;

namespace
{
	constexpr uint64_t RETRY_MS = 1000;
	constexpr uint64_t WRITE_RETRY_MS = 2;
	constexpr uint64_t NETWORK_POLL_MS = 2000;
	constexpr uint64_t PREVIEW_DWELL_MS = 75;
	constexpr size_t MAX_CONTROLLERS = 16;
	constexpr size_t CONTROLLER_EVENT_QUEUE_SIZE = 32;

	enum Dirty : uint32_t
	{
		DIRTY_HELLO = 1 << 0,
		DIRTY_CORE = 1 << 1,
		DIRTY_GAME = 1 << 2,
		DIRTY_LOAD = 1 << 3,
		DIRTY_IDLE = 1 << 4,
		DIRTY_VIDEO = 1 << 5,
		DIRTY_OSD = 1 << 6,
		DIRTY_PREVIEW = 1 << 7,
		DIRTY_LAN = 1 << 8,
		DIRTY_WLAN = 1 << 9,
	};

	enum GameState : uint8_t
	{
		GAME_EMPTY,
		GAME_LOADING,
		GAME_LOADED,
	};

	struct VideoState
	{
		uint32_t source_width;
		uint32_t source_height;
		uint32_t source_refresh_millihz;
		uint32_t output_width;
		uint32_t output_height;
		uint32_t output_refresh_millihz;
		bool interlaced;
		bool direct_video;
	};

	struct NetworkState
	{
		bool present;
		char interface_name[16];
		char ip[INET_ADDRSTRLEN];
		char mac[18];
	};

	struct ControllerState
	{
		char id[80];
		char name[128];
		uint16_t vid;
		uint16_t pid;
		uint8_t player;
	};

	struct ControllerEvent
	{
		ControllerState controller;
		char state[16];
		bool snapshot;
	};

	int serial_fd = -1;
	char device_selector[256] = {};
	char device_path[256] = {};
	char state_core[96] = {};
	char state_game_crc32[9] = {};
	char state_game_serial[64] = {};
	char state_game_name[256] = {};
	char load_state[16] = {};
	VideoState video_state = {};
	NetworkState lan_state = {};
	NetworkState wlan_state = {};
	GameState game_state = GAME_EMPTY;
	char preview_kind[16] = {};
	char preview_name[256] = {};
	char preview_path[768] = {};
	char pending_preview_kind[16] = {};
	char pending_preview_name[256] = {};
	char pending_preview_path[768] = {};
	bool load_active = false;
	bool idle_state = false;
	bool video_known = false;
	bool network_known = false;
	int load_percent = 0;
	bool osd_visible = false;
	bool preview_active = false;
	bool preview_pending = false;
	uint32_t dirty = 0;
	uint32_t sequence = 0;
	char tx_line[1400] = {};
	size_t tx_length = 0;
	size_t tx_offset = 0;
	uint32_t snapshot_dirty = 0;
	uint64_t retry_deadline = 0;
	uint64_t write_deadline = 0;
	uint64_t health_deadline = 0;
	uint64_t network_deadline = 0;
	uint64_t preview_deadline = 0;
	ControllerState controllers[MAX_CONTROLLERS] = {};
	ControllerState controller_scan[MAX_CONTROLLERS] = {};
	size_t controller_count = 0;
	size_t controller_scan_count = 0;
	bool controller_scan_initialized = false;
	ControllerEvent controller_events[CONTROLLER_EVENT_QUEUE_SIZE] = {};
	size_t controller_event_read = 0;
	size_t controller_event_write = 0;

	bool wait_reported = false;
	uint64_t now_ms()
	{
		struct timespec now = {};
		clock_gettime(CLOCK_MONOTONIC, &now);

		return (uint64_t)now.tv_sec * 1000 + (uint64_t)now.tv_nsec / 1000000;
	}

	void copy_string(char *dst, size_t size, const char *src)
	{
		if (!size) return;
		snprintf(dst, size, "%s", src ? src : "");
	}

	uint64_t now_us()
	{
		struct timespec now = {};
		clock_gettime(CLOCK_MONOTONIC, &now);
		return (uint64_t)now.tv_sec * 1000000 + (uint64_t)now.tv_nsec / 1000;
	}

	void copy_field(char *dst, size_t size, const char *src)
	{
		size_t offset = 0;
		while (src && *src && offset + 1 < size)
		{
			unsigned char c = *src++;
			dst[offset++] = (c < 0x20 || c == 0x7f) ? ' ' : c;
		}
		dst[offset] = 0;
	}

	const char *leaf_name(const char *path)
	{
		if (!path) return "";
		const char *slash = strrchr(path, '/');
		const char *backslash = strrchr(path, '\\');
		if (!slash || (backslash && backslash > slash)) slash = backslash;
		return slash ? slash + 1 : path;
	}

	void copy_game_name(char *dst, size_t size, const char *path)
	{
		copy_field(dst, size, leaf_name(path));
		char *extension = strrchr(dst, '.');
		if (extension && extension != dst) *extension = 0;
	}

	const char *game_state_name()
	{
		switch (game_state)
		{
		case GAME_LOADING: return "loading";
		case GAME_LOADED: return "loaded";
		default: return "empty";
		}
	}

	bool controller_equal(const ControllerState &a, const ControllerState &b)
	{
		return a.vid == b.vid && a.pid == b.pid && !strcmp(a.id, b.id);
	}

	int find_controller(const ControllerState *list, size_t count, const ControllerState &controller)
	{
		for (size_t i = 0; i < count; i++)
		{
			if (controller_equal(list[i], controller)) return (int)i;
		}
		return -1;
	}

	bool controller_event_queue_empty()
	{
		return controller_event_read == controller_event_write;
	}

	void clear_controller_events()
	{
		controller_event_read = controller_event_write = 0;
	}

	void queue_controller_event(const ControllerState &controller, const char *state, bool snapshot)
	{
		size_t next = (controller_event_write + 1) % CONTROLLER_EVENT_QUEUE_SIZE;
		if (next == controller_event_read) return;
		ControllerEvent &event = controller_events[controller_event_write];
		event.controller = controller;
		copy_string(event.state, sizeof(event.state), state);
		event.snapshot = snapshot;
		controller_event_write = next;
	}

	bool network_state_equal(const NetworkState &a, const NetworkState &b)
	{
		return a.present == b.present &&
			!strcmp(a.interface_name, b.interface_name) &&
			!strcmp(a.ip, b.ip) && !strcmp(a.mac, b.mac);
	}

	bool read_network_state(NetworkState *lan, NetworkState *wlan)
	{
		struct ifaddrs *interfaces = 0;
		if (getifaddrs(&interfaces)) return false;

		for (const struct ifaddrs *entry = interfaces; entry; entry = entry->ifa_next)
		{
			if (!entry->ifa_addr || !entry->ifa_name) continue;
			NetworkState *state = 0;
			if (!strcmp(entry->ifa_name, "eth0")) state = lan;
			else if (!strncmp(entry->ifa_name, "wlan", 4)) state = wlan;
			if (!state) continue;
			if (state->present && strcmp(state->interface_name, entry->ifa_name)) continue;

			state->present = true;
			if (!state->interface_name[0])
				copy_field(state->interface_name, sizeof(state->interface_name), entry->ifa_name);

			if (entry->ifa_addr->sa_family == AF_INET && !state->ip[0])
			{
				const struct sockaddr_in *address =
					reinterpret_cast<const struct sockaddr_in *>(entry->ifa_addr);
				inet_ntop(AF_INET, &address->sin_addr, state->ip, sizeof(state->ip));
			}
			else if (entry->ifa_addr->sa_family == AF_PACKET && !state->mac[0])
			{
				const struct sockaddr_ll *address =
					reinterpret_cast<const struct sockaddr_ll *>(entry->ifa_addr);
				if (address->sll_halen >= 6)
				{
					snprintf(state->mac, sizeof(state->mac), "%02x:%02x:%02x:%02x:%02x:%02x",
						address->sll_addr[0], address->sll_addr[1], address->sll_addr[2],
						address->sll_addr[3], address->sll_addr[4], address->sll_addr[5]);
				}
			}
		}

		freeifaddrs(interfaces);
		return true;
	}

	void poll_network(uint64_t now)
	{
		if (network_deadline && now < network_deadline) return;
		network_deadline = now + NETWORK_POLL_MS;

		NetworkState next_lan = {};
		NetworkState next_wlan = {};
		if (!read_network_state(&next_lan, &next_wlan)) return;

		if (!network_known || !network_state_equal(lan_state, next_lan))
		{
			lan_state = next_lan;
			dirty |= DIRTY_LAN;
			snapshot_dirty &= ~DIRTY_LAN;
		}
		if (!network_known || !network_state_equal(wlan_state, next_wlan))
		{
			wlan_state = next_wlan;
			dirty |= DIRTY_WLAN;
			snapshot_dirty &= ~DIRTY_WLAN;
		}
		network_known = true;
	}

	void queue_controller_snapshot()
	{
		for (size_t i = 0; i < controller_count; i++)
		{
			queue_controller_event(controllers[i], "present", true);
		}
	}

	void mark_snapshot()
	{
		uint32_t state = DIRTY_HELLO | DIRTY_CORE | DIRTY_GAME |
			DIRTY_IDLE | DIRTY_OSD | DIRTY_LAN | DIRTY_WLAN;
		if (load_active) state |= DIRTY_LOAD;
		if (video_known) state |= DIRTY_VIDEO;
		if (preview_active) state |= DIRTY_PREVIEW;
		dirty |= state;
		snapshot_dirty |= state;
		queue_controller_snapshot();
	}

	void close_port(const char *reason)
	{
		if (serial_fd >= 0)
		{
			if (reason) printf("MiSTer serial: %s (%s)\n", reason, device_path);
			close(serial_fd);
		}

		serial_fd = -1;
		tx_length = 0;
		tx_offset = 0;
		write_deadline = 0;
		health_deadline = 0;
		retry_deadline = now_ms() + RETRY_MS;
		wait_reported = false;
		clear_controller_events();
		mark_snapshot();
	}

	bool retry_open(const char *action, const char *target)
	{
		if (!wait_reported)
		{
			printf("MiSTer serial: %s %s (%s)\n", action, target, strerror(errno));
			wait_reported = true;
		}
		retry_deadline = now_ms() + RETRY_MS;
		return false;
	}

	bool open_port()
	{
		if (!serial_device_resolve(device_selector, device_path, sizeof(device_path)))
			return retry_open("waiting for", device_selector);
		int fd = open(device_path, O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
		if (fd < 0) return retry_open("waiting for", device_path);

		struct termios options;
		if (tcgetattr(fd, &options))
		{
			int error = errno;
			close(fd);
			errno = error;
			return retry_open("cannot use", device_path);
		}

		cfmakeraw(&options);
		options.c_cflag &= ~(PARENB | CSTOPB | CSIZE);
#ifdef CRTSCTS
		options.c_cflag &= ~CRTSCTS;
#endif
		options.c_cflag |= CS8 | CLOCAL | CREAD;
		options.c_iflag &= ~(IXON | IXOFF | IXANY);
		cfsetispeed(&options, B115200);
		cfsetospeed(&options, B115200);
		if (tcsetattr(fd, TCSANOW, &options))
		{
			int error = errno;
			close(fd);
			errno = error;
			return retry_open("cannot configure", device_path);
		}

		serial_fd = fd;
		tx_length = 0;
		tx_offset = 0;
		write_deadline = 0;
		health_deadline = 0;
		retry_deadline = 0;
		wait_reported = false;
		clear_controller_events();
		mark_snapshot();
		printf("MiSTer serial: connected to %s (%s)\n", device_path, device_selector);
		return true;
	}

	void format_line(uint32_t flag)
	{
		uint32_t next_sequence = ++sequence;
		bool snapshot = (snapshot_dirty & flag) != 0;
		snapshot_dirty &= ~flag;
		uint64_t timestamp = now_us();
		if (!next_sequence) next_sequence = ++sequence;

		int length = 0;
		switch (flag)
		{
		case DIRTY_HELLO:
		{
			char main_version[64];
			copy_field(main_version, sizeof(main_version), version && !strncmp(version, "$VER:", 5) ? version + 5 : version);
			length = snprintf(tx_line, sizeof(tx_line),
				"MISTER_STATUS/1\t%u\tHELLO\tmain=%s\tcaps=core,game,load,state,video,osd,preview,controller,net\n",
				next_sequence, main_version);
			break;
		}
		case DIRTY_CORE:
			length = snprintf(tx_line, sizeof(tx_line),
				"MISTER_STATUS/1\t%u\tCORE\tname=%s\n", next_sequence, state_core);
			break;
		case DIRTY_GAME:
			length = snprintf(tx_line, sizeof(tx_line),
				"MISTER_STATUS/1\t%u\tGAME\tstate=%s\tname=%s\tcrc32=%s\tserial=%s\n",
				next_sequence, game_state_name(), state_game_name,
				state_game_crc32, state_game_serial);
			break;
		case DIRTY_LAN:
			length = snprintf(tx_line, sizeof(tx_line),
				"MISTER_STATUS/1\t%u\tNET\ttype=lan\tpresent=%d\tinterface=%s\tip=%s\tmac=%s\n",
				next_sequence, lan_state.present ? 1 : 0, lan_state.interface_name,
				lan_state.ip, lan_state.mac);
			break;
		case DIRTY_WLAN:
			length = snprintf(tx_line, sizeof(tx_line),
				"MISTER_STATUS/1\t%u\tNET\ttype=wlan\tpresent=%d\tinterface=%s\tip=%s\tmac=%s\n",
				next_sequence, wlan_state.present ? 1 : 0, wlan_state.interface_name,
				wlan_state.ip, wlan_state.mac);
			break;
		case DIRTY_LOAD:
			length = snprintf(tx_line, sizeof(tx_line),
				"MISTER_STATUS/1\t%u\tLOAD\tstate=%s\tpercent=%d\n",
				next_sequence, load_state, load_percent);
			break;
		case DIRTY_IDLE:
			length = snprintf(tx_line, sizeof(tx_line),
				"MISTER_STATUS/1\t%u\tSTATE\tstate=%s\n", next_sequence, idle_state ? "idle" : "active");
			break;
		case DIRTY_VIDEO:
			length = snprintf(tx_line, sizeof(tx_line),
				"MISTER_STATUS/1\t%u\tVIDEO\tsource_width=%u\tsource_height=%u\tsource_refresh_millihz=%u\tinterlaced=%d\toutput_width=%u\toutput_height=%u\toutput_refresh_millihz=%u\tmode=%s\n",
				next_sequence, video_state.source_width, video_state.source_height,
				video_state.source_refresh_millihz, video_state.interlaced ? 1 : 0,
				video_state.output_width, video_state.output_height,
				video_state.output_refresh_millihz,
				video_state.direct_video ? "direct" : "scaled");
			break;
		case DIRTY_OSD:
			length = snprintf(tx_line, sizeof(tx_line),
				"MISTER_STATUS/1\t%u\tOSD\tvisible=%d\n", next_sequence, osd_visible ? 1 : 0);
			break;
		case DIRTY_PREVIEW:
			length = snprintf(tx_line, sizeof(tx_line),
				"MISTER_STATUS/1\t%u\tPREVIEW\tstate=%s\tkind=%s\tname=%s\tpath=%s\n",
				next_sequence, preview_active ? "active" : "clear",
				preview_kind, preview_name, preview_path);
			break;

		}


		if (length > 0 && (size_t)length < sizeof(tx_line) && tx_line[length - 1] == '\n')
		{
			length--;
			int appended = snprintf(tx_line + length, sizeof(tx_line) - (size_t)length,
				"\ttime_us=%llu\tsnapshot=%d\n",
				(unsigned long long)timestamp, snapshot ? 1 : 0);
			length = appended > 0 ? length + appended : 0;
		}
		tx_length = length > 0 && (size_t)length < sizeof(tx_line) ? (size_t)length : 0;
		tx_offset = 0;
	}


	void format_controller_event()
	{
		if (controller_event_queue_empty()) return;
		ControllerEvent &event = controller_events[controller_event_read];
		uint32_t next_sequence = ++sequence;
		if (!next_sequence) next_sequence = ++sequence;

		int length = snprintf(tx_line, sizeof(tx_line),
			"MISTER_STATUS/1\t%u\tCONTROLLER\tstate=%s\tplayer=%u\tvid=%04x\tpid=%04x\tid=%s\tname=%s\ttime_us=%llu\tsnapshot=%d\n",
			next_sequence, event.state, event.controller.player,
			event.controller.vid, event.controller.pid,
			event.controller.id, event.controller.name,
			(unsigned long long)now_us(), event.snapshot ? 1 : 0);
		controller_event_read = (controller_event_read + 1) % CONTROLLER_EVENT_QUEUE_SIZE;
		tx_length = length > 0 && (size_t)length < sizeof(tx_line) ? (size_t)length : 0;
		tx_offset = 0;
	}

	void format_next()
	{
		static const uint32_t order[] = {
			DIRTY_HELLO, DIRTY_LAN, DIRTY_WLAN, DIRTY_CORE, DIRTY_GAME,
			DIRTY_VIDEO, DIRTY_OSD, DIRTY_PREVIEW, DIRTY_IDLE, DIRTY_LOAD

		};
		for (uint32_t flag : order)
		{
			if (dirty & flag)
			{
				dirty &= ~flag;
				format_line(flag);
				return;
			}
		}
		if (!controller_event_queue_empty()) format_controller_event();
	}

	void io_failed(const char *operation)
	{
		char reason[96];
		snprintf(reason, sizeof(reason), "%s failed: %s", operation, strerror(errno));
		close_port(reason);
	}

	void set_game_state(const char *filename, GameState next_state,
		uint32_t crc32, const char *serial)
	{
		char next_name[sizeof(state_game_name)];
		char next_crc32[sizeof(state_game_crc32)] = {};
		char next_serial[sizeof(state_game_serial)] = {};
		copy_game_name(next_name, sizeof(next_name), filename);
		bool preserve_ids = next_state == GAME_LOADED && game_state == GAME_LOADED &&
			!strcmp(state_game_name, next_name) && !crc32 && (!serial || !*serial);
		if (preserve_ids)
		{
			copy_string(next_crc32, sizeof(next_crc32), state_game_crc32);
			copy_string(next_serial, sizeof(next_serial), state_game_serial);
		}
		else if (next_state == GAME_LOADED)
		{
			if (crc32) snprintf(next_crc32, sizeof(next_crc32), "%08X", crc32);
			copy_field(next_serial, sizeof(next_serial), serial);
		}
		if (game_state == next_state && !strcmp(state_game_name, next_name) &&
			!strcmp(state_game_crc32, next_crc32) &&
			!strcmp(state_game_serial, next_serial)) return;
		copy_string(state_game_name, sizeof(state_game_name), next_name);
		copy_string(state_game_crc32, sizeof(state_game_crc32), next_crc32);
		copy_string(state_game_serial, sizeof(state_game_serial), next_serial);
		game_state = next_state;
		dirty |= DIRTY_GAME;
		snapshot_dirty &= ~DIRTY_GAME;
		mister_serial_poll();
	}
}

void mister_serial_init(const char *device, const char *core)
{
	char next_device[sizeof(device_selector)];
	copy_string(next_device, sizeof(next_device), device);
	if (strcmp(device_selector, next_device) && serial_fd >= 0) close_port("configuration changed");

	copy_string(device_selector, sizeof(device_selector), next_device);
	device_path[0] = 0;
	copy_field(state_core, sizeof(state_core), core);
	state_game_name[0] = 0;
	game_state = GAME_EMPTY;
	load_active = false;
	state_game_crc32[0] = state_game_serial[0] = 0;
	idle_state = false;
	dirty |= DIRTY_CORE | DIRTY_GAME | DIRTY_IDLE;
	bool clear_preview = preview_pending || preview_active;
	preview_pending = false;
	preview_active = false;
	preview_deadline = 0;
	preview_kind[0] = preview_name[0] = preview_path[0] = 0;
	snapshot_dirty &= ~(DIRTY_CORE | DIRTY_GAME | DIRTY_IDLE | DIRTY_PREVIEW);
	if (clear_preview) dirty |= DIRTY_PREVIEW;

	if (!device_selector[0])
	{
		if (serial_fd >= 0) close_port(0);
		return;
	}

	retry_deadline = 0;
	wait_reported = false;
	mister_serial_poll();
}

void mister_serial_set_game_loading(const char *filename)
{
	set_game_state(filename, filename && *filename ? GAME_LOADING : GAME_EMPTY, 0, 0);
}

void mister_serial_set_game(const char *filename, uint32_t crc32, const char *serial)
{
	set_game_state(filename, filename && *filename ? GAME_LOADED : GAME_EMPTY,
		crc32, serial);
}

void mister_serial_set_osd(int visible)
{
	bool next_visible = visible != 0;
	if (osd_visible == next_visible) return;
	osd_visible = next_visible;
	dirty |= DIRTY_OSD;
	snapshot_dirty &= ~DIRTY_OSD;
	if (!osd_visible)
	{
		preview_pending = false;
		preview_deadline = 0;
		if (preview_active)
		{
			preview_active = false;
			preview_kind[0] = preview_name[0] = preview_path[0] = 0;
			dirty |= DIRTY_PREVIEW;
			snapshot_dirty &= ~DIRTY_PREVIEW;
		}
	}
	mister_serial_poll();
}

void mister_serial_set_preview(const char *kind, const char *name, const char *path)
{
	if (!name || !*name)
	{
		mister_serial_clear_preview();
		return;
	}

	char next_kind[sizeof(pending_preview_kind)];
	char next_name[sizeof(pending_preview_name)];
	char next_path[sizeof(pending_preview_path)];
	copy_field(next_kind, sizeof(next_kind), kind);
	copy_field(next_name, sizeof(next_name), name);
	copy_field(next_path, sizeof(next_path), path);
	if ((!preview_pending && preview_active && !strcmp(preview_kind, next_kind) &&
		!strcmp(preview_name, next_name) && !strcmp(preview_path, next_path)) ||
		(preview_pending && !strcmp(pending_preview_kind, next_kind) &&
		!strcmp(pending_preview_name, next_name) && !strcmp(pending_preview_path, next_path))) return;

	copy_string(pending_preview_kind, sizeof(pending_preview_kind), next_kind);
	copy_string(pending_preview_name, sizeof(pending_preview_name), next_name);
	copy_string(pending_preview_path, sizeof(pending_preview_path), next_path);
	preview_pending = true;
	preview_deadline = now_ms() + PREVIEW_DWELL_MS;
}

void mister_serial_clear_preview()
{
	preview_pending = false;
	preview_deadline = 0;
	if (!preview_active) return;
	preview_active = false;
	preview_kind[0] = preview_name[0] = preview_path[0] = 0;
	dirty |= DIRTY_PREVIEW;
	snapshot_dirty &= ~DIRTY_PREVIEW;
	mister_serial_poll();
}

void mister_serial_controller_scan_begin()
{
	controller_scan_count = 0;
}

void mister_serial_controller_scan_add(const char *id, const char *name,
	uint16_t vid, uint16_t pid, int player)
{
	if (!id || !*id || controller_scan_count >= MAX_CONTROLLERS) return;
	ControllerState &controller = controller_scan[controller_scan_count++];
	copy_field(controller.id, sizeof(controller.id), id);
	copy_field(controller.name, sizeof(controller.name), name);
	controller.vid = vid;
	controller.pid = pid;
	controller.player = player < 0 ? 0 : player > 255 ? 255 : (uint8_t)player;
}

void mister_serial_controller_scan_end()
{
	if (!controller_scan_initialized)
	{
		for (size_t i = 0; i < controller_scan_count; i++)
			queue_controller_event(controller_scan[i], "present", true);
		controller_scan_initialized = true;
	}
	else
	{
		for (size_t i = 0; i < controller_count; i++)
			if (find_controller(controller_scan, controller_scan_count, controllers[i]) < 0)
				queue_controller_event(controllers[i], "disconnected", false);
		for (size_t i = 0; i < controller_scan_count; i++)
		{
			int old = find_controller(controllers, controller_count, controller_scan[i]);
			if (old < 0) queue_controller_event(controller_scan[i], "connected", false);
			else if (controllers[old].player != controller_scan[i].player)
				queue_controller_event(controller_scan[i], "assigned", false);
		}
	}

	controller_count = controller_scan_count;
	for (size_t i = 0; i < controller_count; i++) controllers[i] = controller_scan[i];
	mister_serial_poll();
}

void mister_serial_controller_player(const char *id, int player)
{
	if (!id || !*id) return;
	uint8_t next_player = player < 0 ? 0 : player > 255 ? 255 : (uint8_t)player;
	for (size_t i = 0; i < controller_count; i++)
	{
		if (strcmp(controllers[i].id, id)) continue;
		if (controllers[i].player == next_player) return;
		controllers[i].player = next_player;
		queue_controller_event(controllers[i], "assigned", false);
		mister_serial_poll();
		return;
	}
}

void mister_serial_set_progress(const char *action, const char *item, int current, int maximum)
{
	(void)action;
	(void)item;
	if (current <= 0 && maximum <= 0)
	{
		if (!load_active) return;
		copy_string(load_state, sizeof(load_state), "done");
		load_percent = 100;
		load_active = false;
		dirty |= DIRTY_LOAD;
		snapshot_dirty &= ~DIRTY_LOAD;
		mister_serial_poll();
		return;
	}

	int next_percent = maximum > 0 ? (int)((int64_t)current * 100 / maximum) : 0;
	if (next_percent < 0) next_percent = 0;
	if (next_percent > 100) next_percent = 100;
	if (load_active && load_percent == next_percent) return;

	copy_string(load_state, sizeof(load_state), load_active ? "progress" : "start");
	load_percent = next_percent;
	load_active = true;
	dirty |= DIRTY_LOAD;
	snapshot_dirty &= ~DIRTY_LOAD;
	mister_serial_poll();
}
void mister_serial_set_idle(int idle)
{
	bool next_idle = idle != 0;
	if (idle_state == next_idle) return;
	idle_state = next_idle;
	dirty |= DIRTY_IDLE;
	snapshot_dirty &= ~DIRTY_IDLE;
	mister_serial_poll();
}

void mister_serial_set_video(uint32_t source_width, uint32_t source_height,
	uint32_t source_refresh_millihz, int interlaced,
	uint32_t output_width, uint32_t output_height,
	uint32_t output_refresh_millihz, int direct_video)
{
	VideoState next = {
		source_width, source_height, source_refresh_millihz,
		output_width, output_height, output_refresh_millihz,
		interlaced != 0, direct_video != 0
	};
	if (video_known &&
		video_state.source_width == next.source_width &&
		video_state.source_height == next.source_height &&
		video_state.source_refresh_millihz == next.source_refresh_millihz &&
		video_state.output_width == next.output_width &&
		video_state.output_height == next.output_height &&
		video_state.output_refresh_millihz == next.output_refresh_millihz &&
		video_state.interlaced == next.interlaced &&
		video_state.direct_video == next.direct_video) return;
	video_state = next;
	video_known = true;
	dirty |= DIRTY_VIDEO;
	snapshot_dirty &= ~DIRTY_VIDEO;
	mister_serial_poll();
}

void mister_serial_poll()
{
	if (!device_selector[0]) return;
	uint64_t now = now_ms();
	poll_network(now);
	if (preview_pending && now >= preview_deadline)
	{
		copy_string(preview_kind, sizeof(preview_kind), pending_preview_kind);
		copy_string(preview_name, sizeof(preview_name), pending_preview_name);
		copy_string(preview_path, sizeof(preview_path), pending_preview_path);
		preview_pending = false;
		preview_active = true;
		preview_deadline = 0;
		dirty |= DIRTY_PREVIEW;
		snapshot_dirty &= ~DIRTY_PREVIEW;
	}


	if (serial_fd < 0)
	{
		if (retry_deadline && now < retry_deadline) return;
		if (!open_port()) return;
	}

	if (!tx_length && !dirty && controller_event_queue_empty())
	{
		if (health_deadline && now < health_deadline) return;
		health_deadline = now + 250;

		struct pollfd descriptor = {serial_fd, 0, 0};
		int result = poll(&descriptor, 1, 0);
		if (result < 0 && errno != EINTR) io_failed("poll");
		else if (descriptor.revents & (POLLERR | POLLHUP | POLLNVAL)) close_port("device disconnected");
		return;
	}

	if (!tx_length) format_next();
	if (!tx_length) return;
	if (write_deadline && now < write_deadline) return;
	write_deadline = 0;

	ssize_t written = write(serial_fd, tx_line + tx_offset, tx_length - tx_offset);
	if (written > 0)
	{
		tx_offset += (size_t)written;
		if (tx_offset == tx_length)
		{
			tx_length = 0;
			tx_offset = 0;
		}
		return;
	}

	if (!written || errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
	{
		if (errno != EINTR) write_deadline = now + WRITE_RETRY_MS;
		return;
	}
	io_failed("write");
}
