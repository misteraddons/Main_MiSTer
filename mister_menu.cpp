#include "mister_menu.h"
#include "support/menu/menu_protocol.h"
#include <cstddef>
#include "cfg.h"
#include "file_io.h"
#include "fpga_io.h"
#include "offload.h"
#include "user_io.h"
#include "support/arcade/mra_loader.h"
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <linux/kd.h>
#include <linux/vt.h>
#include <unistd.h>

namespace
{
char launch_path[1024];

bool field(const char *text, size_t size)
{
	const char *end = static_cast<const char *>(memchr(text, 0, size));
	if (!end) return false;
	for (const char *p = text; p < end; ++p)
		if (static_cast<unsigned char>(*p) < 32) return false;
	return true;
}

int ini_slot(const char *name)
{
	if (!*name) return -1;
	for (int i = 0; i < 4; ++i)
		if (!strcasecmp(name, cfg_get_name(i))) return i;
	return -1;
}

bool valid(MenuPacket &packet)
{
	if (memcmp(packet.abi, MISTER_MENU_ABI, sizeof(MISTER_MENU_ABI)) ||
		!field(packet.ini, sizeof(packet.ini)) || !field(packet.path, sizeof(packet.path))) return false;
	if (packet.operation == MenuReady) return !packet.path[0];
	const size_t length = strlen(packet.ini);
	if (packet.operation < MenuLaunch || packet.operation > MenuFallback || length < 10 ||
		strchr(packet.ini, '/') || strchr(packet.ini, '\\') ||
		(strcasecmp(packet.ini, "MiSTer.ini") && (strncasecmp(packet.ini, "MiSTer_", 7) ||
		strcasecmp(packet.ini + length - 4, ".ini")))) return false;
	if (packet.operation != MenuLaunch) return !packet.path[0];
	const char *ext = strrchr(packet.path, '.');
	if (!ext || (strcasecmp(ext, ".rbf") && strcasecmp(ext, ".mra") && strcasecmp(ext, ".mgl"))) return false;
	struct stat st{};
	return !stat(getFullPath(packet.path), &st) && S_ISREG(st.st_mode);
}

// Re-exec starts from clean hardware/config state. The one-use marker skips
// discovery only for this return; the core loader's next exec discovers again.
[[noreturn]] void resume(const MenuPacket &packet)
{
	setenv("MISTER_MENU_RETURN", "1", 1);
	setenv("MISTER_MENU_INI", packet.ini, 1);
	setenv("MISTER_MENU_LAUNCH", packet.operation == MenuLaunch ? packet.path : "", 1);
	if (packet.operation == MenuReload) unsetenv("MISTER_MENU_RETURN");
	const char *self = getappname();
	execl(self, self, "menu.rbf", static_cast<char *>(nullptr));
	perror("MiSTer menu return");
	_exit(1);
}

void reap_menu(pid_t child)
{
	// Subreaping also catches workers which create their own sessions. Reap
// them before loading anything; killing just the menu process group is not enough.
	kill(-child, SIGKILL);
	kill(child, SIGKILL);
	for (;;)
	{
		int status;
		pid_t result;
		do { result = waitpid(-1, &status, WNOHANG); } while (result > 0 || (result < 0 && errno == EINTR));
		if (result < 0 && errno == ECHILD) break;
		char path[80];
		snprintf(path, sizeof(path), "/proc/self/task/%d/children", getpid());
		FILE *children = fopen(path, "r");
		if (children)
		{
			int pid;
			while (fscanf(children, "%d", &pid) == 1) if (pid > 0) kill(pid, SIGKILL);
			fclose(children);
		}
		usleep(10000);
	}
}
}

void mister_menu_start(const char *core_path)
{
	const char *returned = getenv("MISTER_MENU_RETURN");
	const char *ini = getenv("MISTER_MENU_INI");
	const char *target = getenv("MISTER_MENU_LAUNCH");
	MenuPacket request{};
	memcpy(request.abi, MISTER_MENU_ABI, sizeof(MISTER_MENU_ABI));
	request.operation = target && *target ? MenuLaunch : MenuFallback;
	if (ini && strlen(ini) < sizeof(request.ini)) strcpy(request.ini, ini);
	if (target && strlen(target) < sizeof(request.path)) strcpy(request.path, target);
	bool bypass = returned != nullptr;
	bool restore_ini = ini != nullptr;
	unsetenv("MISTER_MENU_RETURN");
	unsetenv("MISTER_MENU_INI");
	unsetenv("MISTER_MENU_LAUNCH");
	if (valid(request) && ini_slot(request.ini) >= 0)
	{
		altcfg(ini_slot(request.ini));
		if (bypass && request.operation == MenuLaunch) strcpy(launch_path, request.path);
	}
	else if (restore_ini) bypass = true;
	if (bypass || (fpga_get_buttons() & BUTTON_USR)) return;

	char executable[1024];
	snprintf(executable, sizeof(executable), "%s/MiSTer_menu", getStorageDir(0));
	int image = open(executable, O_RDONLY | O_CLOEXEC);
	struct stat st{};
	if (image < 0) return; // Optional: stock Menu remains the default.
	if (fstat(image, &st) || !S_ISREG(st.st_mode) || access(executable, X_OK)) { close(image); return; }
	int pair[2];
	if (socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, pair)) { close(image); return; }
	if (prctl(PR_SET_CHILD_SUBREAPER, 1)) { close(image); close(pair[0]); close(pair[1]); return; }
	// No input, serial or video threads have started at this point.
	offload_stop();
	fflush(nullptr);
	MenuPacket context{};
	memcpy(context.abi, MISTER_MENU_ABI, sizeof(MISTER_MENU_ABI));
	snprintf(context.ini, sizeof(context.ini), "%s", cfg_get_name(altcfg()));
	snprintf(context.path, sizeof(context.path), "%s", core_path);
	request = context;
	request.operation = MenuFallback;
	request.path[0] = 0;
	// The frontend's own VT guard is optional; restore the original mode even
// if it crashes before installing one.
	int console = open("/dev/tty0", O_RDWR | O_NOCTTY | O_CLOEXEC), mode = KD_TEXT;
	if (console >= 0) ioctl(console, KDGETMODE, &mode);
	const pid_t parent = getpid();
	pid_t child = fork();
	if (!child)
	{
		if (prctl(PR_SET_PDEATHSIG, SIGKILL) || getppid() != parent) _exit(127);
		setpgid(0, 0);
		close(pair[0]);
		char descriptor[16];
		snprintf(descriptor, sizeof(descriptor), "%d", pair[1]);
		fcntl(pair[1], F_SETFD, 0);
		const long limit = sysconf(_SC_OPEN_MAX);
		for (int fd = 3; fd < limit; ++fd) if (fd != image && fd != pair[1]) close(fd);
		char *args[] = {executable, const_cast<char *>("--mister-menu-v1"), descriptor,
			const_cast<char *>(getRootDir()), nullptr};
		fexecve(image, args, environ);
		_exit(127);
	}
	close(image);
	close(pair[1]);
	if (child > 0)
	{
		setpgid(child, child);
		bool ready = false;
		int startup_seconds = 0;
		if (send(pair[0], &context, sizeof(context), MSG_NOSIGNAL) == sizeof(context))
		{
			for (;;)
			{
				pollfd fd{pair[0], POLLIN, 0};
				int result;
				do { result = poll(&fd, 1, 1000); } while (result < 0 && errno == EINTR);
				if (result < 0) break;
				if (!result)
				{
					int status;
					if (waitpid(child, &status, WNOHANG) == child || (!ready && ++startup_seconds >= 30)) break;
					continue;
				}
				MenuPacket packet{};
				ssize_t n = recv(pair[0], &packet, sizeof(packet), MSG_TRUNC);
				if (n != sizeof(packet) || !valid(packet)) break;
				if (!ready)
				{
					if (packet.operation != MenuReady) break;
					ready = true;
				}
				else { if (packet.operation != MenuReady) request = packet; break; }
			}
		}
		close(pair[0]);
		reap_menu(child);
	}
	else close(pair[0]);
	if (console >= 0) { ioctl(console, KDSETMODE, mode); close(console); }
	// Reload may select a newly created INI; its slot is resolved on the fresh exec.
	resume(request);
}

void mister_menu_launch()
{
	if (!launch_path[0]) return;
	if (isXmlName(launch_path)) xml_load(getFullPath(launch_path));
	else fpga_load_rbf(launch_path);
	// A failed loader leaves the built-in menu usable, with no discovery loop.
	launch_path[0] = 0;
}
