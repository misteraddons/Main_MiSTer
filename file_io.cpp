#include "file_io.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <stdbool.h>
#include <string.h>
#include <fcntl.h>
#include <strings.h>
#include <errno.h>
#include <sys/stat.h>
#include <dirent.h>
#include <ctype.h>
#include <sys/vfs.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <linux/magic.h>
#include <algorithm>
#include <vector>
#include <string>
#include <set>
#include "lib/miniz/miniz.h"
#include "osd.h"
#include "fpga_io.h"
#include "menu.h"
#include "hardware.h"
#include "errno.h"
#include "DiskImage.h"
#include "user_io.h"
#include "cfg.h"
#include "input.h"
#include "miniz.h"
#include "scheduler.h"
#include "video.h"
#include "support.h"

#define MIN(a,b) (((a)<(b)) ? (a) : (b))

// Virtual folder system - disabled for testing, enable for full functionality
#define VIRTUAL_FOLDER_SYSTEM_DISABLED 0

typedef std::vector<direntext_t> DirentVector;
typedef std::set<std::string> DirNameSet;

static const size_t YieldIterations = 128;

DirentVector DirItem;
DirNameSet DirNames;

// Unified games list system for favorites, try, and delete game management
// This provides a cache-based approach to track game states across cores
//
// Virtual Folder Flag System:
// - Directory flags: 0x8000 (favorites dir), 0x4000 (try/delete dirs)  
// - File flags: 0x8001 (favorites files), 0x8002 (try files), 0x8003 (delete files)
// - Regular files/dirs use standard flags (DT_REG, DT_DIR, etc.)
//
// The flag system allows the UI to distinguish between:
// 1. Real directories vs virtual directories (different <DIR> display)
// 2. Real files vs virtual files (different symbol display)  
// 3. Different virtual file types (♥, ?, ✗ symbols)
typedef enum {
    GAME_TYPE_DELETE = 'd',
    GAME_TYPE_FAVORITE = 'f', 
    GAME_TYPE_TRY = 't'
} GameType;

typedef struct {
    char path[192];        // 192 chars covers most paths with good headroom
    GameType type;         // 'd', 'f', or 't'
} GameEntry;               // = 193 bytes per entry

typedef struct {
    GameEntry *entries;     // Dynamically allocated entries (max 512)
    int capacity;           // Current allocated capacity
    int count;              // Current number of entries  
    char current_directory[256]; // Shorter directory path
    bool is_dirty;          // True if changes need to be saved
    uint32_t last_change_time; // Time of last change for delayed writing
    bool auto_save_enabled; // Enable/disable automatic delayed saves
} GamesList;

// Global unified games list  
static GamesList g_games_list = {NULL, 0, 0, "", false, 0, true};

// Forward declarations for functions used throughout the file
static int GamesList_CountByType(GamesList* list, GameType type);
static int GamesLoad(const char* directory);
static void GamesList_Save(GamesList* list, const char* directory);
static void GamesList_Load(GamesList* list, const char* directory);
static void GamesList_RelocateMissingFiles(GamesList* list, const char* directory);
static bool GamesList_SearchForFile(const char* search_dir, const char* filename, char* found_path, size_t path_size);
static bool GamesList_EnsureCapacity(GamesList* list, int needed_capacity);
static void GamesList_Free(GamesList* list);
static void GamesList_RemoveDuplicates(GamesList* list);

// Directory scanning can cause the same zip file to be opened multiple times
// due to testing file types to adjust the path
// (and the fact the code path is shared with regular files)
// cache the opened mz_zip_archive so we only open it once
// this has the extra benefit that if a user is navigating through multiple directories
// in a zip archive, the zip will only be opened once and things will be more responsive
// ** We have to open the file outselves with open() so we can set O_CLOEXEC to prevent
// leaking the file descriptor when the user changes cores

static mz_zip_archive last_zip_archive = {};
static int last_zip_fd = -1;
static FILE *last_zip_cfile = NULL;
static char last_zip_fname[256] = {};
static char scanned_path[1024] = {};
static int scanned_opts = 0;

static int iSelectedEntry = 0;       // selected entry index
static int iFirstEntry = 0;

static char full_path[2100];
uint8_t loadbuf[LOADBUF_SZ];

// Prevent infinite recursion between toggle functions
static bool in_toggle_operation = false;
static bool in_mutual_exclusivity_call = false;

fileTYPE::fileTYPE()
{
	filp = 0;
	mode = 0;
	type = 0;
	zip = 0;
	size = 0;
	offset = 0;
}

fileTYPE::~fileTYPE()
{
	FileClose(this);
}

int fileTYPE::opened()
{
	return filp || zip;
}

struct fileZipArchive
{
	mz_zip_archive                    archive;
	int                               index;
	mz_zip_reader_extract_iter_state* iter;
	__off64_t                         offset;
};


static int OpenZipfileCached(char *path, int flags)
{
  if (last_zip_fname[0] && !strcasecmp(path, last_zip_fname))
  {
    return 1;
  }

  mz_zip_reader_end(&last_zip_archive);
  mz_zip_zero_struct(&last_zip_archive);
  if (last_zip_cfile)
  {
    fclose(last_zip_cfile);
    last_zip_cfile = nullptr;
  }

  last_zip_fname[0] = '\0';
  last_zip_fd = open(path, O_RDONLY|O_CLOEXEC);
  if (last_zip_fd < 0)
  {
    return 0;
  }

  last_zip_cfile = fdopen(last_zip_fd, "r");
  if (!last_zip_cfile)
  {
    close(last_zip_fd);
    last_zip_fd = -1;
    return 0;
  }

  int mz_ret = mz_zip_reader_init_cfile(&last_zip_archive, last_zip_cfile, 0, flags);
  if (mz_ret)
  {
    strncpy(last_zip_fname, path, sizeof(last_zip_fname));
  }
  return mz_ret;
}


static int FileIsZipped(char* path, char** zip_path, char** file_path)
{
	char* z = strcasestr(path, ".zip");
	if (z)
	{
		z += 4;
		if (!z[0]) z[1] = 0;
		*z++ = 0;

		if (zip_path) *zip_path = path;
		if (file_path) *file_path = z;
		return 1;
	}

	return 0;
}

static char* make_fullpath(const char *path, int mode = 0)
{
	if (path[0] != '/')
	{
		sprintf(full_path, "%s/%s", (mode == -1) ? "" : getRootDir(), path);
	}
	else
	{
		sprintf(full_path, "%s",path);
	}

	return full_path;
}

static int get_stmode(const char *path)
{
	struct stat64 st;
	return (stat64(path, &st) < 0) ? 0 : st.st_mode;
}

struct stat64* getPathStat(const char *path)
{
	make_fullpath(path);
	static struct stat64 st;
	return (stat64(full_path, &st) >= 0) ? &st : NULL;
}

static int isPathDirectory(const char *path, int use_zip = 1)
{
	make_fullpath(path);

	char *zip_path, *file_path;
	if (use_zip && FileIsZipped(full_path, &zip_path, &file_path))
	{
		if (!*file_path)
		{
			return 1;
		}

		if (!OpenZipfileCached(full_path, 0))
		{
			printf("isPathDirectory(OpenZipfileCached) Zip:%s, error:%s\n", zip_path,
				mz_zip_get_error_string(mz_zip_get_last_error(&last_zip_archive)));
			return 0;
		}

		// Folder names always end with a slash in the zip
		// file central directory.
		strcat(file_path, "/");

		// Some zip files don't have directory entries
		// Use the locate_file call to try and find the directory entry first, since
		// this is a binary search (usually) If that fails then scan for the first
		// entry that starts with file_path

		const int file_index = mz_zip_reader_locate_file(&last_zip_archive, file_path, NULL, 0);
		if (file_index >= 0 && mz_zip_reader_is_file_a_directory(&last_zip_archive, file_index))
		{
			return 1;
		}

		for (size_t i = 0; i < mz_zip_reader_get_num_files(&last_zip_archive); i++)
		{
			char zip_fname[256];
			mz_zip_reader_get_filename(&last_zip_archive, i, &zip_fname[0], sizeof(zip_fname));
			if (strcasestr(zip_fname, file_path))
			{
				return 1;
			}
		}
		return 0;
	}
	else
	{
		int stmode = get_stmode(full_path);
		if (!stmode)
		{
			//printf("isPathDirectory(stat) path: %s, error: %s.\n", full_path, strerror(errno));
			return 0;
		}

		if (stmode & S_IFDIR) return 1;
	}

	return 0;
}

static int isPathRegularFile(const char *path, int use_zip = 1)
{
	make_fullpath(path);

	char *zip_path, *file_path;
	if (use_zip && FileIsZipped(full_path, &zip_path, &file_path))
	{
		//If there's no path into the zip file, don't bother opening it, we're a "directory"
		if (!*file_path)
		{
			return 0;
		}
		if (!OpenZipfileCached(full_path, 0))
		{
			//printf("isPathRegularFile(mz_zip_reader_init_file) Zip:%s, error:%s\n", zip_path,
			//       mz_zip_get_error_string(mz_zip_get_last_error(&z)));
			return 0;
		}
		const int file_index = mz_zip_reader_locate_file(&last_zip_archive, file_path, NULL, 0);
		if (file_index < 0)
		{
			//printf("isPathRegularFile(mz_zip_reader_locate_file) Zip:%s, file:%s, error: %s\n",
			//		 zip_path, file_path,
			//		 mz_zip_get_error_string(mz_zip_get_last_error(&z)));
			return 0;
		}

		if (!mz_zip_reader_is_file_a_directory(&last_zip_archive, file_index) && mz_zip_reader_is_file_supported(&last_zip_archive, file_index))
		{
			return 1;
		}
	}
	else
	{
		if (get_stmode(full_path) & S_IFREG) return true;
	}

	return 0;
}

void FileClose(fileTYPE *file)
{
	if (file->zip)
	{
		if (file->zip->iter)
		{
			mz_zip_reader_extract_iter_free(file->zip->iter);
		}
		mz_zip_reader_end(&file->zip->archive);

		delete file->zip;
	}

	if (file->filp)
	{
		//printf("closing %p\n", file->filp);
		fclose(file->filp);
		if (file->type == 1)
		{
			if (file->name[0] == '/')
			{
				shm_unlink(file->name);
			}
			file->type = 0;
		}
	}

	file->zip = nullptr;
	file->filp = nullptr;
	file->size = 0;
}

static int zip_search_by_crc(mz_zip_archive *zipArchive, uint32_t crc32)
{
	for (unsigned int file_index = 0; file_index < zipArchive->m_total_files; file_index++)
	{
		mz_zip_archive_file_stat s;
		if (mz_zip_reader_file_stat(zipArchive, file_index, &s))
		{
			if (s.m_crc32 == crc32)
			{
				return file_index;
			}
		}
	}

	return -1;
}

int FileOpenZip(fileTYPE *file, const char *name, uint32_t crc32)
{
	make_fullpath(name);
	FileClose(file);
	file->mode = 0;
	file->type = 0;

	char *p = strrchr(full_path, '/');
	strcpy(file->name, (p) ? p + 1 : full_path);

	char *zip_path, *file_path;
	if (!FileIsZipped(full_path, &zip_path, &file_path))
	{
		printf("FileOpenZip: %s, is not a zip.\n", full_path);
		return 0;
	}

	file->zip = new fileZipArchive{};
	if (!mz_zip_reader_init_file(&file->zip->archive, zip_path, 0))
	{
		printf("FileOpenZip(mz_zip_reader_init_file) Zip:%s, error:%s\n", zip_path,
					mz_zip_get_error_string(mz_zip_get_last_error(&file->zip->archive)));
		return 0;
	}

	file->zip->index = -1;
	if (crc32) file->zip->index = zip_search_by_crc(&file->zip->archive, crc32);
	if (file->zip->index < 0) file->zip->index = mz_zip_reader_locate_file(&file->zip->archive, file_path, NULL, 0);
	if (file->zip->index < 0)
	{
		printf("FileOpenZip(mz_zip_reader_locate_file) Zip:%s, file:%s, error: %s\n",
					zip_path, file_path,
					mz_zip_get_error_string(mz_zip_get_last_error(&file->zip->archive)));
		FileClose(file);
		return 0;
	}

	mz_zip_archive_file_stat s;
	if (!mz_zip_reader_file_stat(&file->zip->archive, file->zip->index, &s))
	{
		printf("FileOpenZip(mz_zip_reader_file_stat) Zip:%s, file:%s, error:%s\n",
					zip_path, file_path,
					mz_zip_get_error_string(mz_zip_get_last_error(&file->zip->archive)));
		FileClose(file);
		return 0;
	}
	file->size = s.m_uncomp_size;

	file->zip->iter = mz_zip_reader_extract_iter_new(&file->zip->archive, file->zip->index, 0);
	if (!file->zip->iter)
	{
		printf("FileOpenZip(mz_zip_reader_extract_iter_new) Zip:%s, file:%s, error:%s\n",
					zip_path, file_path,
					mz_zip_get_error_string(mz_zip_get_last_error(&file->zip->archive)));
		FileClose(file);
		return 0;
	}

	file->zip->offset = 0;
	file->offset = 0;
	file->mode = O_RDONLY;
	return 1;
}

int FileOpenEx(fileTYPE *file, const char *name, int mode, char mute, int use_zip)
{
	make_fullpath((char*)name, mode);
	FileClose(file);
	file->mode = 0;
	file->type = 0;

	char *p = strrchr(full_path, '/');
	strcpy(file->name, (mode == -1) ? full_path : p + 1);

	char *zip_path, *file_path;
	if (use_zip && (mode != -1) && FileIsZipped(full_path, &zip_path, &file_path))
	{
		if (mode & O_RDWR || mode & O_WRONLY)
		{
			if(!mute) printf("FileOpenEx(mode) Zip:%s, writing to zipped files is not supported.\n",
					 full_path);
			return 0;
		}

		file->zip = new fileZipArchive{};
		if (!mz_zip_reader_init_file(&file->zip->archive, zip_path, 0))
		{
			if(!mute) printf("FileOpenEx(mz_zip_reader_init_file) Zip:%s, error:%s\n", zip_path,
					 mz_zip_get_error_string(mz_zip_get_last_error(&file->zip->archive)));
			return 0;
		}

		file->zip->index = mz_zip_reader_locate_file(&file->zip->archive, file_path, NULL, 0);
		if (file->zip->index < 0)
		{
			if(!mute) printf("FileOpenEx(mz_zip_reader_locate_file) Zip:%s, file:%s, error: %s\n",
					 zip_path, file_path,
					 mz_zip_get_error_string(mz_zip_get_last_error(&file->zip->archive)));
			FileClose(file);
			return 0;
		}

		mz_zip_archive_file_stat s;
		if (!mz_zip_reader_file_stat(&file->zip->archive, file->zip->index, &s))
		{
			if(!mute) printf("FileOpenEx(mz_zip_reader_file_stat) Zip:%s, file:%s, error:%s\n",
					 zip_path, file_path,
					 mz_zip_get_error_string(mz_zip_get_last_error(&file->zip->archive)));
			FileClose(file);
			return 0;
		}
		file->size = s.m_uncomp_size;

		file->zip->iter = mz_zip_reader_extract_iter_new(&file->zip->archive, file->zip->index, 0);
		if (!file->zip->iter)
		{
			if(!mute) printf("FileOpenEx(mz_zip_reader_extract_iter_new) Zip:%s, file:%s, error:%s\n",
					 zip_path, file_path,
					 mz_zip_get_error_string(mz_zip_get_last_error(&file->zip->archive)));
			FileClose(file);
			return 0;
		}
		file->zip->offset = 0;
		file->offset = 0;
		file->mode = mode;
	}
	else
	{
		int fd = (mode == -1) ? shm_open("/vdsk", O_CREAT | O_RDWR | O_TRUNC | O_CLOEXEC, 0777) : open(full_path, mode | O_CLOEXEC, 0777);
		if (fd <= 0)
		{
			if(!mute) printf("FileOpenEx(open) File:%s, error: %s.\n", full_path, strerror(errno));
			return 0;
		}
		const char *fmode = mode & O_RDWR ? "w+" : "r";
		file->filp = fdopen(fd, fmode);
		if (!file->filp)
		{
			if(!mute) printf("FileOpenEx(fdopen) File:%s, error: %s.\n", full_path, strerror(errno));
			close(fd);
			return 0;
		}

		if (mode == -1)
		{
			file->type = 1;
			file->size = 0;
			file->offset = 0;
			file->mode = O_CREAT | O_RDWR | O_TRUNC;
		}
		else
		{
			struct stat64 st;
			int ret = fstat64(fileno(file->filp), &st);
			if (ret < 0)
			{
				if (!mute) printf("FileOpenEx(fstat) File:%s, error: %d.\n", full_path, ret);
				FileClose(file);
				return 0;
			}

			file->size = st.st_size;
			if (st.st_rdev && !st.st_size)  //for special files we need an ioctl call to get the correct size
			{
				unsigned long long blksize;
				int ret = ioctl(fd, BLKGETSIZE64, &blksize);
				if (ret < 0)
				{
					if (!mute) printf("FileOpenEx(ioctl) File:%s, error: %d.\n", full_path, ret);
					FileClose(file);
					return 0;
				}
				file->size = blksize;
			}

			file->offset = 0;
			file->mode = mode;
		}
	}

	//printf("opened %s, size %llu\n", full_path, file->size);
	return 1;
}

__off64_t FileGetSize(fileTYPE *file)
{
	if (file->filp)
	{
		struct stat64 st;
		if (fstat64(fileno(file->filp), &st) < 0) return 0;

		if (st.st_rdev && !st.st_size)  //for special files we need an ioctl call to get the correct size
		{
			unsigned long long blksize;
			int ret = ioctl(fileno(file->filp), BLKGETSIZE64, &blksize);
			if (ret < 0) return 0;
			return blksize;
		}

		return st.st_size;
	}
	else if (file->zip)
	{
		return file->size;
	}
	return 0;
}

int FileOpen(fileTYPE *file, const char *name, char mute)
{
	return FileOpenEx(file, name, O_RDONLY, mute);
}

int FileSeek(fileTYPE *file, __off64_t offset, int origin)
{
	if (file->filp)
	{
		__off64_t res = fseeko64(file->filp, offset, origin);
		if (res < 0)
		{
			printf("Fail to seek the file: offset=%lld, %s.\n", offset, file->name);
			return 0;
		}
		offset = ftello64(file->filp);
	}
	else if (file->zip)
	{
		if (origin == SEEK_CUR)
		{
			offset = file->zip->offset + offset;
		}
		else if (origin == SEEK_END)
		{
			offset = file->size - offset;
		}

		if (offset < file->zip->offset)
		{
			mz_zip_reader_extract_iter_state *iter = mz_zip_reader_extract_iter_new(&file->zip->archive, file->zip->index, 0);
			if (!iter)
			{
				printf("FileSeek(mz_zip_reader_extract_iter_new) Failed to rewind iterator, error:%s\n",
				       mz_zip_get_error_string(mz_zip_get_last_error(&file->zip->archive)));
				return 0;
			}

			mz_zip_reader_extract_iter_free(file->zip->iter);
			file->zip->iter = iter;
			file->zip->offset = 0;
		}

		static char buf[4*1024];
		while (file->zip->offset < offset)
		{
			const size_t want_len = MIN((__off64_t)sizeof(buf), offset - file->zip->offset);
			const size_t read_len = mz_zip_reader_extract_iter_read(file->zip->iter, buf, want_len);
			file->zip->offset += read_len;
			if (read_len < want_len)
			{
				printf("FileSeek(mz_zip_reader_extract_iter_read) Failed to advance iterator, error:%s\n",
				       mz_zip_get_error_string(mz_zip_get_last_error(&file->zip->archive)));
				return 0;
			}
		}
	}
	else
	{
		return 0;
	}

	file->offset = offset;
	return 1;
}

int FileSeekLBA(fileTYPE *file, uint32_t offset)
{
	__off64_t off64 = offset;
	off64 <<= 9;
	return FileSeek(file, off64, SEEK_SET);
}

// Read with offset advancing
int FileReadAdv(fileTYPE *file, void *pBuffer, int length, int failres)
{
	ssize_t ret = 0;

	if (file->filp)
	{
		ret = fread(pBuffer, 1, length, file->filp);
		if (ret < 0)
		{
			printf("FileReadAdv error(%d).\n", ret);
			return failres;
		}
	}
	else if (file->zip)
	{
		ret = mz_zip_reader_extract_iter_read(file->zip->iter, pBuffer, length);
		if (!ret)
		{
			printf("FileReadEx(mz_zip_reader_extract_iter_read) Failed to read, error:%s\n",
			       mz_zip_get_error_string(mz_zip_get_last_error(&file->zip->archive)));
			return failres;
		}
		file->zip->offset += ret;
	}
	else
	{
		printf("FileReadAdv error(unknown file type).\n");
		return failres;
	}

	file->offset += ret;
	return ret;
}

int FileReadSec(fileTYPE *file, void *pBuffer)
{
	return FileReadAdv(file, pBuffer, 512);
}

// Write with offset advancing
int FileWriteAdv(fileTYPE *file, void *pBuffer, int length, int failres)
{
	int ret;

	if (file->filp)
	{
		ret = fwrite(pBuffer, 1, length, file->filp);
		fflush(file->filp);

		if (ret < 0)
		{
			printf("FileWriteAdv error(%d).\n", ret);
			return failres;
		}

		file->offset += ret;
		if (file->offset > file->size) file->size = FileGetSize(file);
		return ret;
	}
	else if (file->zip)
	{
		printf("FileWriteAdv error(not supported for zip).\n");
		return failres;
	}
	else
	{
		printf("FileWriteAdv error(unknown file type).\n");
		return failres;
	}
}

int FileWriteSec(fileTYPE *file, void *pBuffer)
{
	return FileWriteAdv(file, pBuffer, 512);
}

int FileSave(const char *name, void *pBuffer, int size)
{
	make_fullpath(name);

	int fd = open(full_path, O_WRONLY | O_CREAT | O_TRUNC | O_SYNC, S_IRWXU | S_IRWXG | S_IRWXO);
	if (fd < 0)
	{
		printf("FileSave(open) File:%s, error: %d.\n", full_path, fd);
		return 0;
	}

	int ret = write(fd, pBuffer, size);
	close(fd);

	if (ret < 0)
	{
		printf("FileSave(write) File:%s, error: %d.\n", full_path, ret);
		return 0;
	}

	return ret;
}

int FileDelete(const char *name)
{
	make_fullpath(name);
	printf("delete %s\n", full_path);
	return !unlink(full_path);
}

int DirDelete(const char *name)
{
	make_fullpath(name);
	printf("rmdir %s\n", full_path);
	return !rmdir(full_path);
}

const char* GetNameFromPath(char *path)
{
	static char res[32];

	char* p = strrchr(path, '/');
	if (!p) p = path;
	else p++;

	if (strlen(p) < 19) strcpy(res, p);
	else
	{
		strncpy(res, p, 19);
		res[19] = 0;
	}

	return res;
}

int FileLoad(const char *name, void *pBuffer, int size)
{
	fileTYPE f;
	if (!FileOpen(&f, name)) return 0;

	int ret = f.size;
	if (pBuffer) ret = FileReadAdv(&f, pBuffer, size ? size : f.size);

	FileClose(&f);
	return ret;
}

int FileLoadConfig(const char *name, void *pBuffer, int size)
{
	char path[256] = { CONFIG_DIR"/" };
	strcat(path, name);
	return FileLoad(path, pBuffer, size);
}

int FileSaveConfig(const char *name, void *pBuffer, int size)
{
	char path[256] = { CONFIG_DIR };
	const char *p;
	while ((p = strchr(name, '/')))
	{
		strcat(path, "/");
		strncat(path, name, p - name);
		name = ++p;
		FileCreatePath(path);
	}

	strcat(path, "/");
	strcat(path, name);
	return FileSave(path, pBuffer, size);
}

int FileDeleteConfig(const char *name)
{
	char path[256] = { CONFIG_DIR"/" };
	strcat(path, name);
	return FileDelete(path);
}

int FileExists(const char *name, int use_zip)
{
	return isPathRegularFile(name, use_zip);
}

int PathIsDir(const char *name, int use_zip)
{
	return isPathDirectory(name, use_zip);
}

int FileCanWrite(const char *name)
{
	make_fullpath(name);

	if (FileIsZipped(full_path, nullptr, nullptr))
	{
		return 0;
	}

	struct stat64 st;
	int ret = stat64(full_path, &st);
	if (ret < 0)
	{
		printf("FileCanWrite(stat) File:%s, error: %d.\n", full_path, ret);
		return 0;
	}

	//printf("FileCanWrite: mode=%04o.\n", st.st_mode);
	return ((st.st_mode & S_IWUSR) != 0);
}

void create_path(const char *base_dir, const char* sub_dir)
{
	make_fullpath(base_dir);
	mkdir(full_path, S_IRWXU | S_IRWXG | S_IRWXO);
	strcat(full_path, "/");
	strcat(full_path, sub_dir);
	mkdir(full_path, S_IRWXU | S_IRWXG | S_IRWXO);
}

int FileCreatePath(const char *dir)
{
	int res = 1;
	if (!isPathDirectory(dir)) {
		make_fullpath(dir);
		res = !mkdir(full_path, S_IRWXU | S_IRWXG | S_IRWXO);
	}
	return res;
}

void FileGenerateScreenshotName(const char *name, char *out_name, int buflen)
{
	// If the name ends with .png then don't modify it
	if( !strcasecmp(name + strlen(name) - 4, ".png") )
	{
		const char *p = strrchr(name, '/');
		make_fullpath(SCREENSHOT_DIR);
		if( p )
		{
			snprintf(out_name, buflen, "%s%s", SCREENSHOT_DIR, p);
		}
		else
		{
			snprintf(out_name, buflen, "%s/%s", SCREENSHOT_DIR, name);
		}
	}
	else
	{
		create_path(SCREENSHOT_DIR, CoreName2);

		time_t t = time(NULL);
		struct tm tm = *localtime(&t);
		char datecode[32] = {};
		if (tm.tm_year >= 119) // 2019 or up considered valid time
		{
			strftime(datecode, 31, "%Y%m%d_%H%M%S", &tm);
			snprintf(out_name, buflen, "%s/%s/%s-%s.png", SCREENSHOT_DIR, CoreName2, datecode, name[0] ? name : SCREENSHOT_DEFAULT);
		}
		else
		{
			for (int i = 1; i < 10000; i++)
			{
				snprintf(out_name, buflen, "%s/%s/NODATE-%s_%04d.png", SCREENSHOT_DIR, CoreName2, name[0] ? name : SCREENSHOT_DEFAULT, i);
				if (!getFileType(out_name)) return;
			}
		}
	}
}

void FileGenerateSavePath(const char *name, char* out_name, int ext_replace)
{
	create_path(SAVE_DIR, CoreName2);

	sprintf(out_name, "%s/%s/", SAVE_DIR, CoreName2);
	char *fname = out_name + strlen(out_name);

	const char *p = strrchr(name, '/');
	if (p)
	{
		strcat(fname, p+1);
	}
	else
	{
		strcat(fname, name);
	}

	char *e = strrchr(fname, '.');
	if (ext_replace && e)
	{
		strcpy(e,".sav");
	}
	else
	{
		strcat(fname, ".sav");
	}

	printf("SavePath=%s\n", out_name);
}

void FileGenerateSavestatePath(const char *name, char* out_name, int sufx)
{
	const char *subdir = is_arcade() ? "Arcade" : CoreName2;

	create_path(SAVESTATE_DIR, subdir);

	sprintf(out_name, "%s/%s/", SAVESTATE_DIR, subdir);
	char *fname = out_name + strlen(out_name);

	const char *p = strrchr(name, '/');
	if (p)
	{
		strcat(fname, p + 1);
	}
	else
	{
		strcat(fname, name);
	}

	char *e = strrchr(fname, '.');
	if (e) e[0] = 0;

	if(sufx) sprintf(e, "_%d.ss", sufx);
	else strcat(e, ".ss");
}

uint32_t getFileType(const char *name)
{
	make_fullpath(name);

	struct stat64 st;
	if (stat64(full_path, &st)) return 0;

	return st.st_mode;
}

int findPrefixDir(char *dir, size_t dir_len)
{
	// Searches for the core's folder in the following order:
	// /media/usb<0..5>
	// /media/usb<0..5>/games
	// /media/network
	// /media/network/games
	// /media/fat/cifs
	// /media/fat/cifs/games
	// /media/fat
	// /media/fat/games/
	// if the core folder is not found anywhere,
	// it will be created in /media/fat/games/<dir>
	static char temp_dir[1024];

	// Usb<0..5>
	for (int x = 0; x < 6; x++) {
		snprintf(temp_dir, 1024, "%s%d/%s", "../usb", x, dir);
		if (isPathDirectory(temp_dir)) {
			printf("Found USB dir: %s\n", temp_dir);
			strncpy(dir, temp_dir, dir_len);
			return 1;
		}

		snprintf(temp_dir, 1024, "%s%d/%s/%s", "../usb", x, GAMES_DIR, dir);
		if (isPathDirectory(temp_dir)) {
			printf("Found USB dir: %s\n", temp_dir);
			strncpy(dir, temp_dir, dir_len);
			return 1;
		}
	}

	// Network share in /media/network/
	snprintf(temp_dir, 1024, "%s/%s", "../network", dir);
	if (isPathDirectory(temp_dir)) {
		printf("Found network dir: %s\n", temp_dir);
		strncpy(dir, temp_dir, dir_len);
		return 1;
	}

	// Network share in /media/network/games
	snprintf(temp_dir, 1024, "%s/%s/%s", "../network", GAMES_DIR, dir);
	if (isPathDirectory(temp_dir)) {
		printf("Found network dir: %s\n", temp_dir);
		strncpy(dir, temp_dir, dir_len);
		return 1;
	}

	// CIFS_DIR directory in /media/fat/cifs
	snprintf(temp_dir, 1024, "%s/%s", CIFS_DIR, dir);
	if (isPathDirectory(temp_dir)) {
		printf("Found CIFS dir: %s\n", temp_dir);
		strncpy(dir, temp_dir, dir_len);
		return 1;
	}

	// CIFS_DIR/GAMES_DIR directory in /media/fat/cifs/games
	snprintf(temp_dir, 1024, "%s/%s/%s", CIFS_DIR, GAMES_DIR, dir);
	if (isPathDirectory(temp_dir)) {
		printf("Found CIFS dir: %s\n", temp_dir);
		strncpy(dir, temp_dir, dir_len);
		return 1;
	}

	// media/fat
	if (isPathDirectory(dir)) {
		printf("Found existing: %s\n", dir);
		return 1;
	}

	// media/fat/GAMES_DIR
	snprintf(temp_dir, 1024, "%s/%s", GAMES_DIR, dir);
	if (isPathDirectory(temp_dir)) {
		printf("Found dir: %s\n", temp_dir);
		strncpy(dir, temp_dir, dir_len);
		return 1;
	}

	return 0;
}

void prefixGameDir(char *dir, size_t dir_len)
{
	if (!findPrefixDir(dir, dir_len))
	{
		static char temp_dir[1024];

		//FileCreatePath(GAMES_DIR);
		snprintf(temp_dir, 1024, "%s/%s", GAMES_DIR, dir);
		strncpy(dir, temp_dir, dir_len);
		printf("Prefixed dir to %s\n", temp_dir);
	}
}

static int device = 0;
static int usbnum = 0;
const char *getStorageDir(int dev)
{
	static char path[32];
	if (!dev) return "/media/fat";
	sprintf(path, "/media/usb%d", usbnum);
	return path;
}

const char *getRootDir()
{
	return getStorageDir(device);
}

const char *getFullPath(const char *name)
{
	make_fullpath(name);
	return full_path;
}

void setStorage(int dev)
{
	device = 0;
	FileSave(CONFIG_DIR"/device.bin", &dev, sizeof(int));
	fpga_load_rbf("menu.rbf");
}

static int orig_device = 0;
int getStorage(int from_setting)
{
	return from_setting ? orig_device : device;
}

int isPathMounted(int n)
{
	char path[32];
	sprintf(path, "/media/usb%d", n);

	struct stat file_stat;
	struct stat parent_stat;

	if (-1 == stat(path, &file_stat))
	{
		printf("failed to stat %s\n", path);
		return 0;
	}

	if (!(file_stat.st_mode & S_IFDIR))
	{
		printf("%s is not a directory.\n", path);
		return 0;
	}

	if (-1 == stat("/media", &parent_stat))
	{
		printf("failed to stat /media\n");
		return 0;
	}

	if (file_stat.st_dev != parent_stat.st_dev ||
		(file_stat.st_dev == parent_stat.st_dev &&
			file_stat.st_ino == parent_stat.st_ino))
	{
		printf("%s IS a mountpoint.\n", path);
		struct statfs fs_stat;
		if (!statfs(path, &fs_stat))
		{
			printf("%s is FS: 0x%08X\n", path, fs_stat.f_type);
			if (fs_stat.f_type != EXT4_SUPER_MAGIC)
			{
				printf("%s is not EXT2/3/4.\n", path);
				return 1;
			}
		}
	}

	printf("%s is NOT a VFAT mountpoint.\n", path);
	return 0;
}

int isUSBMounted()
{
	for (int i = 0; i < 4; i++)
	{
		if (isPathMounted(i))
		{
			usbnum = i;
			return 1;
		}
	}
	return 0;
}

void FindStorage(void)
{
	char str[128];
	printf("Looking for root device...\n");
	device = 0;
	FileLoad(CONFIG_DIR"/device.bin", &device, sizeof(int));
	orig_device = device;

	if(device && !isUSBMounted())
	{
		uint8_t core_type = (fpga_core_id() & 0xFF);
		if (core_type == CORE_TYPE_8BIT)
		{
			user_io_read_confstr();
			user_io_read_core_name();
		}

		int saveddev = device;
		device = 0;
		cfg_parse();
		device = saveddev;
		video_init();
		user_io_send_buttons(1);

		printf("Waiting for USB...\n");
		int btn = 0;
		int done = 0;

		OsdWrite(16, "", 1);
		OsdWrite(17, "       www.MiSTerFPGA.org       ", 1);
		OsdWrite(18, "", 1);

		for (int i = 30; i >= 0; i--)
		{
			sprintf(str, "\n     Waiting for USB...\n\n             %d   \n\n\n  OSD/USER or ESC to cancel", i);
			InfoMessage(str);
			if (isUSBMounted())
			{
				done = 1;
				break;
			}

			for (int i = 0; i < 10; i++)
			{
				btn = fpga_get_buttons();
				if (!btn) btn = input_poll(1);
				if (btn)
				{
					printf("Button has been pressed %d\n", btn);
					InfoMessage("\n\n         Canceled!\n");
					usleep(500000);
					setStorage(0);
					break;
				}
				usleep(100000);
			}
			if (done) break;
		}

		if (!done)
		{
			InfoMessage("\n\n     No USB storage found\n   Falling back to SD card\n");
			usleep(2000000);
			setStorage(0);
		}
	}

	if (device)
	{
		printf("Using USB as a root device\n");
	}
	else
	{
		printf("Using SD card as a root device\n");
	}

	sprintf(full_path, "%s/" CONFIG_DIR, getRootDir());
	DIR* dir = opendir(full_path);
	if (dir) closedir(dir);
	else if (ENOENT == errno) mkdir(full_path, S_IRWXU | S_IRWXG | S_IRWXO);
}

struct DirentComp
{
	bool operator()(const direntext_t& de1, const direntext_t& de2)
	{

#ifdef USE_SCHEDULER
		if (++iterations % YieldIterations == 0)
		{
			scheduler_yield();
		}
#endif

		if ((de1.de.d_type == DT_DIR) && !strcmp(de1.altname, "..")) return true;
		if ((de2.de.d_type == DT_DIR) && !strcmp(de2.altname, "..")) return false;
		
		// Put virtual folders right after ".." but before other directories
		// Order: "..", "❤ Favorites", "❓ Try", "✖ Delete", then other directories
		if ((de1.de.d_type == DT_DIR) && !strcmp(de1.altname, "\x97 Favorites")) return true;
		if ((de2.de.d_type == DT_DIR) && !strcmp(de2.altname, "\x97 Favorites")) return false;
		
		if ((de1.de.d_type == DT_DIR) && !strcmp(de1.altname, "? Try")) return true;
		if ((de2.de.d_type == DT_DIR) && !strcmp(de2.altname, "? Try")) return false;
		
		if ((de1.de.d_type == DT_DIR) && !strcmp(de1.altname, "\x9C Delete")) return true;
		if ((de2.de.d_type == DT_DIR) && !strcmp(de2.altname, "\x9C Delete")) return false;

		if ((de1.de.d_type == DT_DIR) && (de2.de.d_type != DT_DIR)) return true;
		if ((de1.de.d_type != DT_DIR) && (de2.de.d_type == DT_DIR)) return false;

		int len1 = strlen(de1.altname);
		int len2 = strlen(de2.altname);
		if ((len1 > 4) && (de1.altname[len1 - 4] == '.')) len1 -= 4;
		if ((len2 > 4) && (de2.altname[len2 - 4] == '.')) len2 -= 4;

		int len = (len1 < len2) ? len1 : len2;
		int ret = strncasecmp(de1.altname, de2.altname, len);
		if (!ret)
		{
			if(len1 != len2)
			{
				return len1 < len2;
			}
			ret = strcasecmp(de1.datecode, de2.datecode);
		}

		return ret < 0;
	}

	size_t iterations = 0;
};

void AdjustDirectory(char *path)
{
	if (!FileExists(path)) return;

	char *p = strrchr(path, '/');
	if (p)
	{
		*p = 0;
	}
	else
	{
		path[0] = 0;
	}
}

static const char *GetRelativeFileName(const char *folder, const char *path)
{
	if (!*folder) return path;
	if (strcasestr(path, folder) == path)
	{
		const char *subpath = path + strlen(folder);
		if (*subpath == '/') return subpath + 1;
	}
	return NULL;
}

static bool IsInSameFolder(const char *folder, const char *path)
{
	const char *p = strrchr(path, '/');
	size_t len = p ? p - path : 0;
	return (strlen(folder) == len) && !strncasecmp(path, folder, len);
}

static int names_loaded = 0;
static void get_display_name(direntext_t *dext, const char *ext, int options)
{
	static char *names = 0;
	memcpy(dext->altname, dext->de.d_name, sizeof(dext->altname));
	if (dext->de.d_type == DT_DIR) return;

	int len = strlen(dext->altname);
	int xml = (len > 4 && (!strcasecmp(dext->altname + len - 4, ".mgl") || !strcasecmp(dext->altname + len - 4, ".mra")));
	int rbf = (len > 4 && !strcasecmp(dext->altname + len - 4, ".rbf"));
	if (rbf || xml)
	{
		dext->altname[len - 4] = 0;
		if (rbf)
		{
			char *p = strstr(dext->altname, "_20");
			if (p) if (strlen(p + 3) < 6) p = 0;
			if (p)
			{
				*p = 0;
				strncpy(dext->datecode, p + 3, 15);
				dext->datecode[15] = 0;
			}
			else
			{
				strcpy(dext->datecode, "------");
			}
		}

		if (!names_loaded)
		{
			if (names)
			{
				free(names);
				names = 0;
			}

			int size = FileLoad("names.txt", 0, 0);
			if (size)
			{
				names = (char*)malloc(size + 1);
				if (names)
				{
					names[0] = 0;
					FileLoad("names.txt", names, 0);
					names[size] = 0;
				}
			}
			names_loaded = 1;
		}

		if (names)
		{
			strcat(dext->altname, ":");
			len = strlen(dext->altname);
			char *transl = strstr(names, dext->altname);
			if (transl)
			{
				int copy = 0;
				transl += len;
				len = 0;
				while (*transl && len < (int)sizeof(dext->altname) - 1)
				{
					if (!copy && *transl <= 32)
					{
						transl++;
						continue;
					}

					if (copy && *transl < 32) break;

					copy = 1;
					dext->altname[len++] = *transl++;
				}
				len++;
			}

			dext->altname[len - 1] = 0;
		}
		return;
	}

	//do not remove ext if core supplies more than 1 extension and it's not list of cores
	if (!(options & SCANO_CORES) && strlen(ext) > 3) return;
	if (strchr(ext, '*') || strchr(ext, '?')) return;

	/* find the extension on the end of the name*/
	char *fext = strrchr(dext->altname, '.');
	if (fext) *fext = 0;
}

int ScanDirectory(char* path, int mode, const char *extension, int options, const char *prefix, const char *filter)
{
	static char file_name[1024];
	static char full_path[1024];

	int has_trd = 0;
	const char *ext = extension;
	while (*ext)
	{
		if (!strncasecmp(ext, "TRD", 3)) has_trd = 1;
		ext += 3;
	}

	int extlen = strlen(extension);
    int filterlen = filter ? strlen(filter) : 0;
	//printf("scan dir\n");

	if (mode == SCANF_INIT)
	{
		iFirstEntry = 0;
		iSelectedEntry = 0;
		printf("DirItem.clear() called in SCANF_INIT mode\n");
		DirItem.clear();
		DirNames.clear();

		file_name[0] = 0;

		if ((options & SCANO_NOENTER) || isPathRegularFile(path))
		{
			char *p = strrchr(path, '/');
			if (p)
			{
				strcpy(file_name, p + 1);
				*p = 0;
			}
			else
			{
				strcpy(file_name, path);
				path[0] = 0;
			}
		}

		if (!isPathDirectory(path)) return 0;
		snprintf(scanned_path, sizeof(scanned_path), "%s", path);
		scanned_opts = options;

		if (options & SCANO_NEOGEO) neogeo_scan_xml(path);

		sprintf(full_path, "%s/%s", getRootDir(), path);
		int path_len = strlen(full_path);

		const char* is_zipped = strcasestr(full_path, ".zip");
		if (is_zipped && strcasestr(is_zipped + 4, ".zip"))
		{
			printf("Nested zip-files are not supported: %s\n", full_path);
			return 0;
		}

		printf("Start to scan %sdir: %s\n", is_zipped ? "zipped " : "", full_path);
		printf("Position on item: %s\n", file_name);

		char *zip_path, *file_path_in_zip = (char*)"";
		FileIsZipped(full_path, &zip_path, &file_path_in_zip);

		DIR *d = nullptr;
		mz_zip_archive *z = nullptr;
		if (is_zipped)
		{
			if (!OpenZipfileCached(full_path, 0))
			{
				printf("Couldn't open zip file %s: %s\n", full_path, mz_zip_get_error_string(mz_zip_get_last_error(&last_zip_archive)));
				return 0;
			}
			z = &last_zip_archive;
		}
		else
		{
			d = opendir(full_path);
			if (!d)
			{
				printf("Couldn't open dir: %s\n", full_path);
				return 0;
			}
		}

		struct dirent64 *de = nullptr;
		for (size_t i = 0; (d && (de = readdir64(d)))
				 || (z && i < mz_zip_reader_get_num_files(z)); i++)
		{
#ifdef USE_SCHEDULER
			if (0 < i && i % YieldIterations == 0)
			{
				scheduler_yield();
			}
#endif
			struct dirent64 _de = {};
			int isZip = 0;

			if (z)
			{
				mz_zip_reader_get_filename(z, i, &_de.d_name[0], sizeof(_de.d_name));
				const char *rname = GetRelativeFileName(file_path_in_zip, _de.d_name);
				if (rname)
				{
					const char *fslash = strchr(rname, '/');
					if (fslash)
					{
						char dirname[256] = {};
						strncpy(dirname, rname, fslash - rname);
						if (rname[0] != '/' && !(DirNames.find(dirname) != DirNames.end()))
						{
							direntext_t dirext;
							memset(&dirext, 0, sizeof(dirext));
							strncpy(dirext.de.d_name, rname, fslash - rname);
							dirext.de.d_type = DT_DIR;
							memcpy(dirext.altname, dirext.de.d_name, sizeof(dirext.de.d_name));
							DirItem.push_back(dirext);
							DirNames.insert(dirname);
						}
					}
				}

				if (!IsInSameFolder(file_path_in_zip, _de.d_name))
				{
					continue;
				}

				// Remove leading folders.
				const char *subpath = _de.d_name + strlen(file_path_in_zip);
				if (*subpath == '/') subpath++;
				strcpy(_de.d_name, subpath);

				de = &_de;

				_de.d_type = mz_zip_reader_is_file_a_directory(z, i) ? DT_DIR : DT_REG;
				if (_de.d_type == DT_DIR) {
					// Remove trailing slash.
					if (DirNames.find(_de.d_name) != DirNames.end())
					{
						DirNames.insert(_de.d_name);
						_de.d_name[strlen(_de.d_name) - 1] = '\0';
					}
					else
					{
						continue;
					}
				}
			}
			// Handle (possible) symbolic link type in the directory entry
			else if (de->d_type == DT_LNK || de->d_type == DT_REG)
			{
				sprintf(full_path + path_len, "/%s", de->d_name);

				struct stat entrystat;

				if (!stat(full_path, &entrystat))
				{
					if (S_ISREG(entrystat.st_mode))
					{
						de->d_type = DT_REG;
					}
					else if (S_ISDIR(entrystat.st_mode))
					{
						de->d_type = DT_DIR;
					}
				}
			}

            if (filter)
			{
                bool passes_filter = false;

                for(const char *str = de->d_name; *str; str++)
				{
                    if (strncasecmp(str, filter, filterlen) == 0)
					{
                        passes_filter = true;
                        break;
                    }
                }

                if (!passes_filter) continue;
            }


			if (options & SCANO_NEOGEO)
			{
				if (de->d_type == DT_REG && !strcasecmp(de->d_name + strlen(de->d_name) - 4, ".zip"))
				{
					de->d_type = DT_DIR;
				}

				if (strcasecmp(de->d_name + strlen(de->d_name) - 4, ".neo"))
				{
					if (de->d_type != DT_DIR) continue;
				}

				if (!strcmp(de->d_name, ".."))
				{
					if (!strlen(path)) continue;
				}
				else
				{
					// skip hidden folders
					if (!strncasecmp(de->d_name, ".", 1)) continue;
				}

				direntext_t dext;
				memset(&dext, 0, sizeof(dext));
				memcpy(&dext.de, de, sizeof(dext.de));
				memcpy(dext.altname, de->d_name, sizeof(dext.altname));
				if (!strcasecmp(dext.altname + strlen(dext.altname) - 4, ".zip")) dext.altname[strlen(dext.altname) - 4] = 0;

				full_path[path_len] = 0;
				char *altname = neogeo_get_altname(full_path, dext.de.d_name, dext.altname);
				if (altname)
				{
					if (altname == (char*)-1) continue;

					dext.de.d_type = DT_REG;
					memcpy(dext.altname, altname, sizeof(dext.altname));
				}

				DirItem.push_back(dext);
			}
			else
			{
				if (de->d_type == DT_DIR)
				{
					// skip System Volume Information folder
					if (!strcmp(de->d_name, "System Volume Information")) continue;
					if (!strcmp(de->d_name, ".."))
					{
						if (!strlen(path)) continue;
					}
					else
					{
						// skip hidden folder
						if (!strncasecmp(de->d_name, ".", 1)) continue;
					}

					if (!(options & SCANO_DIR))
					{
						if (de->d_name[0] != '_' && strcmp(de->d_name, "..")) continue;
						if (!(options & SCANO_CORES)) continue;
					}
				}
				else if (de->d_type == DT_REG)
				{
					// skip hidden files
					if (!strncasecmp(de->d_name, ".", 1)) continue;
					//skip non-selectable files
					if (!strcasecmp(de->d_name, "menu.rbf")) continue;
					if (!strncasecmp(de->d_name, "menu_20", 7)) continue;
					if (!strcasecmp(de->d_name, "boot.rom")) continue;

					//check the prefix if given
					if (prefix && strncasecmp(prefix, de->d_name, strlen(prefix))) continue;

					if (extlen > 0)
					{
						const char *ext = extension;
						int found = (has_trd && x2trd_ext_supp(de->d_name));
						if (!found && !(options & SCANO_NOZIP) && !strcasecmp(de->d_name + strlen(de->d_name) - 4, ".zip") && (options & SCANO_DIR))
						{
							// Fake that zip-file is a directory.
							de->d_type = DT_DIR;
							isZip = 1;
							found = 1;
						}
						if (!found && is_minimig() && !memcmp(extension, "HDF", 3))
						{
							found = !strcasecmp(de->d_name + strlen(de->d_name) - 4, ".iso");
						}

						char *fext = strrchr(de->d_name, '.');
						if (fext) fext++;
						while (!found && *ext && fext)
						{
							char e[4];
							memcpy(e, ext, 3);
							if (e[2] == ' ')
							{
								e[2] = 0;
								if (e[1] == ' ') e[1] = 0;
							}

							e[3] = 0;
							found = 1;
							for (int i = 0; i < 4; i++)
							{
								if (e[i] == '*') break;
								if (e[i] == '?' && fext[i]) continue;

								if (tolower(e[i]) != tolower(fext[i])) found = 0;

								if (!e[i] || !found) break;
							}
							if (found) break;

							if (strlen(ext) < 3) break;
							ext += 3;
						}
						if (!found) continue;
					}
				}
				else
				{
					continue;
				}

        {
			      direntext_t dext;
				    memset(&dext, 0, sizeof(dext));
				    memcpy(&dext.de, de, sizeof(dext.de));
				    if (isZip)
				        dext.flags |= DT_EXT_ZIP;
				    get_display_name(&dext, extension, options);
				    DirItem.push_back(dext);
        }
			}
		}

		if (z)
		{
			// Since zip files aren't actually folders the entry to
			// exit the zip file must be added manually.
			direntext_t dext;
			memset(&dext, 0, sizeof(dext));
			dext.de.d_type = DT_DIR;
			strcpy(dext.de.d_name, "..");
			get_display_name(&dext, extension, options);
			DirItem.push_back(dext);
		}

		if (d)
		{
			closedir(d);
		}

		// Add virtual favorites folder if we're in a games directory or _Arcade directory with favorites
		char *games_pos = strstr(scanned_path, "games/");
		char *arcade_pos = strstr(scanned_path, "_Arcade");
		if (games_pos)
		{
			char *core_name = games_pos + 6; // skip "games/"
			char *slash_pos = strchr(core_name, '/');
			if (slash_pos)
			{
				// We're in a subdirectory of a core, extract core name
				char core_dir[256];
				int core_len = slash_pos - core_name;
				strncpy(core_dir, core_name, core_len);
				core_dir[core_len] = 0;
				
				// Check if favorites.txt exists for this core
				char favorites_path[1024];
				snprintf(favorites_path, sizeof(favorites_path), "%s/games/%s/favorites.txt", getRootDir(), core_dir);
				if (FileExists(favorites_path, 0))
				{
					// Add virtual favorites folder
					direntext_t favorites_dir;
					memset(&favorites_dir, 0, sizeof(favorites_dir));
					favorites_dir.de.d_type = DT_DIR;
					strcpy(favorites_dir.de.d_name, "Favorites"); // Test without heart symbol
					strcpy(favorites_dir.altname, "Favorites");
					favorites_dir.flags = 0x8000; // Special flag to identify virtual favorites folder
					DirItem.push_back(favorites_dir);
				}
			}
			else if (strchr(core_name, '/') == NULL)
			{
				// We're directly in a core directory (games/N64/), not in a subdirectory
				// Only add virtual folders if we're not already inside a virtual folder
				bool in_virtual_folder = (strstr(scanned_path, "\x97 Favorites") != NULL || strstr(scanned_path, "? Try") != NULL || strstr(scanned_path, "\x9C Delete") != NULL);
				printf("ScanDirectory: scanned_path='%s', in_virtual_folder=%d\n", scanned_path, in_virtual_folder);
				
				if (!in_virtual_folder)
				{
					// Check for games.txt and add virtual folders based on content
					char games_path[1024];
					snprintf(games_path, sizeof(games_path), "%s/games/%s/games.txt", getRootDir(), core_name);
					if (FileExists(games_path, 0))
					{
						// Load games.txt to determine which virtual folders to create
						GamesLoad(core_name);
						
						// Add virtual favorites folder if there are favorites
						// Only show the virtual folder if it contains items to avoid empty folders
						if (GamesList_CountByType(&g_games_list, GAME_TYPE_FAVORITE) > 0)
						{
							direntext_t favorites_dir;
							memset(&favorites_dir, 0, sizeof(favorites_dir));
							favorites_dir.de.d_type = DT_DIR;
							strcpy(favorites_dir.de.d_name, "\x97 Favorites"); // \x97 = heart symbol
							strcpy(favorites_dir.altname, "\x97 Favorites");
							// Flag 0x8000 identifies virtual directory (vs 0x8001 for virtual files inside)
							favorites_dir.flags = 0x8000; 
							DirItem.push_back(favorites_dir);
						}
						
						// Add virtual try folder if there are try entries  
						if (GamesList_CountByType(&g_games_list, GAME_TYPE_TRY) > 0)
						{
							direntext_t try_dir;
							memset(&try_dir, 0, sizeof(try_dir));
							try_dir.de.d_type = DT_DIR;
							strcpy(try_dir.de.d_name, "? Try"); // ? = question mark symbol
							strcpy(try_dir.altname, "? Try");
							// Flag 0x4000 identifies virtual try directory (vs 0x8002 for virtual files inside)
							try_dir.flags = 0x4000;
							DirItem.push_back(try_dir);
						}
						
						// Add virtual delete folder if there are delete entries
						if (GamesList_CountByType(&g_games_list, GAME_TYPE_DELETE) > 0)
						{
							direntext_t delete_dir;
							memset(&delete_dir, 0, sizeof(delete_dir));
							delete_dir.de.d_type = DT_DIR;
							strcpy(delete_dir.de.d_name, "\x9C Delete"); // \x9C = bold X symbol
							strcpy(delete_dir.altname, "\x9C Delete");
							// Flag 0x4000 identifies virtual delete directory (vs 0x8003 for virtual files inside)
							delete_dir.flags = 0x4000;
							DirItem.push_back(delete_dir);
						}
						
						// Restore old counts if needed
						// (No longer needed with unified system)
					}
				}
			}
		}
		else if (arcade_pos && (strcmp(scanned_path, "_Arcade") == 0 || (strstr(scanned_path, "_Arcade") && strchr(arcade_pos + 7, '/') == NULL)))
		{
			// We're in _Arcade directory specifically (not a subdirectory)
			// Only add virtual folders if we're not already inside a virtual folder
			bool in_virtual_folder = (strstr(scanned_path, "\x97 Favorites") != NULL || strstr(scanned_path, "? Try") != NULL || strstr(scanned_path, "\x9C Delete") != NULL);
			printf("ScanDirectory _Arcade: scanned_path='%s', in_virtual_folder=%d\n", scanned_path, in_virtual_folder);
			
			if (!in_virtual_folder)
			{
				// Check for games.txt and add virtual folders based on content
				char games_path[1024];
				snprintf(games_path, sizeof(games_path), "%s/_Arcade/games.txt", getRootDir());
				if (FileExists(games_path, 0))
				{
					// Load games.txt to determine which virtual folders to create
					GamesLoad("_Arcade");
					
					// Add virtual favorites folder if there are favorites
					if (GamesList_CountByType(&g_games_list, GAME_TYPE_FAVORITE) > 0)
					{
						direntext_t favorites_dir;
						memset(&favorites_dir, 0, sizeof(favorites_dir));
						favorites_dir.de.d_type = DT_DIR;
						strcpy(favorites_dir.de.d_name, "\x97 Favorites"); // Heart symbol + Favorites
						strcpy(favorites_dir.altname, "\x97 Favorites");
						favorites_dir.flags = 0x8000; // Special flag to identify virtual favorites folder
						DirItem.push_back(favorites_dir);
					}
					
					// Add virtual try folder if there are try entries
					if (GamesList_CountByType(&g_games_list, GAME_TYPE_TRY) > 0)
					{
						direntext_t try_dir;
						memset(&try_dir, 0, sizeof(try_dir));
						try_dir.de.d_type = DT_DIR;
						strcpy(try_dir.de.d_name, "? Try"); // Question mark + Try
						strcpy(try_dir.altname, "? Try");
						try_dir.flags = 0x4000; // Special flag to identify virtual try folder
						DirItem.push_back(try_dir);
					}
					
					// Add virtual delete folder if there are delete entries
					if (GamesList_CountByType(&g_games_list, GAME_TYPE_DELETE) > 0)
					{
						direntext_t delete_dir;
						memset(&delete_dir, 0, sizeof(delete_dir));
						delete_dir.de.d_type = DT_DIR;
						strcpy(delete_dir.de.d_name, "\x9C Delete"); // Bold X + Delete
						strcpy(delete_dir.altname, "\x9C Delete");
						delete_dir.flags = 0x4000; // Special flag to identify virtual delete folder
						DirItem.push_back(delete_dir);
					}
					
					// Restore old counts if needed
					// (No longer needed with unified system)
				}
			}
		}

		printf("Got %d dir entries\n", flist_nDirEntries());
		if (!flist_nDirEntries()) return 0;

		std::sort(DirItem.begin(), DirItem.end(), DirentComp());
		if (file_name[0])
		{
			int pos = -1;
			for (int i = 0; i < flist_nDirEntries(); i++)
			{
				if (!strcmp(file_name, DirItem[i].de.d_name))
				{
					pos = i;
					break;
				}
				else if (!strcasecmp(file_name, DirItem[i].de.d_name))
				{
					pos = i;
				}
			}

			if(pos>=0)
			{
				iSelectedEntry = pos;
				if (iSelectedEntry + (OsdGetSize() / 2) >= flist_nDirEntries()) iFirstEntry = flist_nDirEntries() - OsdGetSize();
				else iFirstEntry = iSelectedEntry - (OsdGetSize() / 2) + 1;
				if (iFirstEntry < 0) iFirstEntry = 0;
			}
		}
		return flist_nDirEntries();
	}
	else
	{
		if (flist_nDirEntries() == 0) // directory is empty so there is no point in searching for any entry
			return 0;

		if (mode == SCANF_END || (mode == SCANF_PREV && iSelectedEntry <= 0))
		{
			iSelectedEntry = flist_nDirEntries() - 1;
			iFirstEntry = iSelectedEntry - OsdGetSize() + 1;
			if (iFirstEntry < 0) iFirstEntry = 0;
			return 0;
		}
		else if (mode == SCANF_NEXT)
		{
			if(iSelectedEntry + 1 < flist_nDirEntries()) // scroll within visible items
			{
				iSelectedEntry++;
				if (iSelectedEntry > iFirstEntry + OsdGetSize() - 1) iFirstEntry = iSelectedEntry - OsdGetSize() + 1;
			}
            else
            {
				// jump to first visible item
				iFirstEntry = 0;
				iSelectedEntry = 0;
            }
            return 0;
		}
		else if (mode == SCANF_PREV)
		{
			if (iSelectedEntry > 0) // scroll within visible items
			{
				iSelectedEntry--;
				if (iSelectedEntry < iFirstEntry) iFirstEntry = iSelectedEntry;
			}
            return 0;
		}
		else if (mode == SCANF_NEXT_PAGE)
		{
			if (iSelectedEntry < iFirstEntry + OsdGetSize() - 2)
			{
				iSelectedEntry = iFirstEntry + OsdGetSize() - 1;
				if (iSelectedEntry >= flist_nDirEntries()) iSelectedEntry = flist_nDirEntries() - 1;
			}
			else
			{
				iSelectedEntry += OsdGetSize();
				iFirstEntry += OsdGetSize();
				if (iSelectedEntry >= flist_nDirEntries())
				{
					iSelectedEntry = flist_nDirEntries() - 1;
					iFirstEntry = iSelectedEntry - OsdGetSize() + 1;
					if (iFirstEntry < 0) iFirstEntry = 0;
				}
				else if (iFirstEntry + OsdGetSize() > flist_nDirEntries())
				{
					iFirstEntry = flist_nDirEntries() - OsdGetSize();
				}
			}
			return 0;
		}
		else if (mode == SCANF_PREV_PAGE)
		{
			if(iSelectedEntry != iFirstEntry)
			{
				iSelectedEntry = iFirstEntry;
			}
			else
			{
				iFirstEntry -= OsdGetSize();
				if (iFirstEntry < 0) iFirstEntry = 0;
				iSelectedEntry = iFirstEntry;
			}
		}
		else if (mode == SCANF_SET_ITEM)
		{
			int pos = -1;
			for (int i = 0; i < flist_nDirEntries(); i++)
			{
				if ((DirItem[i].de.d_type == DT_DIR) && !strcmp(DirItem[i].altname, extension))
				{
					pos = i;
					break;
				}
				else if ((DirItem[i].de.d_type == DT_DIR) && !strcasecmp(DirItem[i].altname, extension))
				{
					pos = i;
				}
			}

			if(pos>=0)
			{
				iSelectedEntry = pos;
				if (iSelectedEntry + (OsdGetSize() / 2) >= flist_nDirEntries()) iFirstEntry = flist_nDirEntries() - OsdGetSize();
				else iFirstEntry = iSelectedEntry - (OsdGetSize() / 2) + 1;
				if (iFirstEntry < 0) iFirstEntry = 0;
			}
		}
		else
		{
			//printf("dir scan for key: %x/%c\n", mode, mode);
			mode = toupper(mode);
			if ((mode >= '0' && mode <= '9') || (mode >= 'A' && mode <= 'Z'))
			{
				int found = -1;
				for (int i = iSelectedEntry+1; i < flist_nDirEntries(); i++)
				{
					if (toupper(DirItem[i].altname[0]) == mode)
					{
						found = i;
						break;
					}
				}

				if (found < 0)
				{
					for (int i = 0; i < flist_nDirEntries(); i++)
					{
						if (toupper(DirItem[i].altname[0]) == mode)
						{
							found = i;
							break;
						}
					}
				}

				if (found >= 0)
				{
					iSelectedEntry = found;
					if (iSelectedEntry + (OsdGetSize() / 2) >= flist_nDirEntries()) iFirstEntry = flist_nDirEntries() - OsdGetSize();
					else iFirstEntry = iSelectedEntry - (OsdGetSize()/2) + 1;
					if (iFirstEntry < 0) iFirstEntry = 0;
				}
			}
		}
	}

	return 0;
}

char* flist_Path()
{
	return scanned_path;
}

int flist_nDirEntries()
{
	return DirItem.size();
}

int flist_iFirstEntry()
{
	return iFirstEntry;
}

void flist_iFirstEntryInc()
{
	iFirstEntry++;
}

int flist_iSelectedEntry()
{
	return iSelectedEntry;
}

direntext_t* flist_DirItem(int n)
{
	return &DirItem[n];
}

direntext_t* flist_SelectedItem()
{
	return &DirItem[iSelectedEntry];
}

char* flist_GetPrevNext(const char* base_path, const char* file, const char* ext, int next)
{
	static char path[1024];
	snprintf(path, sizeof(path), "%s/%s", base_path, file);
	char *p = strrchr(path, '/');
	if (!FileExists(path))
	{
		snprintf(path, sizeof(path), "%s", base_path);
		p = 0;
	}

	int len = (p) ? p - path : strlen(path);
	if (strncasecmp(scanned_path, path, len) || (scanned_opts & SCANO_DIR)) ScanDirectory(path, SCANF_INIT, ext, 0);

	if (!DirItem.size()) return NULL;
	if (p) ScanDirectory(path, next ? SCANF_NEXT : SCANF_PREV, "", 0);
	snprintf(path, sizeof(path), "%s/%s", scanned_path, DirItem[iSelectedEntry].de.d_name);

	return path + strlen(base_path) + 1;
}

int isXmlName(const char *path)
{
	int len = strlen(path);
	if (len > 4)
	{
		if (!strcasecmp(path + len - 4, ".mra")) return 1;
		if (!strcasecmp(path + len - 4, ".mgl")) return 2;
	}
	return 0;
}

fileTextReader::fileTextReader()
{
	buffer = nullptr;
}

fileTextReader::~fileTextReader()
{
	if( buffer != nullptr )
	{
		free(buffer);
	}
	buffer = nullptr;
}

bool FileOpenTextReader( fileTextReader *reader, const char *filename )
{
	fileTYPE f;

	// ensure buffer is freed if the reader is being reused
	reader->~fileTextReader();

	if (FileOpen(&f, filename))
	{
		char *buf = (char*)malloc(f.size+1);
		if (buf)
		{
			memset(buf, 0, f.size + 1);
			int size;
			if ((size = FileReadAdv(&f, buf, f.size)))
			{
				reader->size = f.size;
				reader->buffer = buf;
				reader->pos = reader->buffer;
				return true;
			}
		}
	}
	return false;
}

#define IS_NEWLINE(c) (((c) == '\r') || ((c) == '\n'))
#define IS_WHITESPACE(c) (IS_NEWLINE(c) || ((c) == ' ') || ((c) == '\t'))

const char *FileReadLine(fileTextReader *reader)
{
	const char *end = reader->buffer + reader->size;
	while (reader->pos < end)
	{
		char *st = reader->pos;
		while ((reader->pos < end) && *reader->pos && !IS_NEWLINE(*reader->pos))
			reader->pos++;
		*reader->pos = 0;
		while (IS_WHITESPACE(*st)) st++;
		if (*st == '#' || *st == ';' || !*st)
		{
			reader->pos++;
		}
		else
		{
			return st;
		}
	}
	return nullptr;
}

// Write cache settings
#define GAMES_CACHE_DELAY_MS 60000  // 60 seconds (1 minute) delay before auto-save
#define GAMES_CACHE_MAX_DIRTY_TIME_MS 120000 // 2 minutes max before forced save

// Legacy cache arrays removed - unified GamesList is now the single source of truth

// Broken heart feedback system
char broken_heart_paths[256][1024];
int broken_heart_count = 0;

// Unified GamesList Caching Functions
static void GamesList_MarkDirty(GamesList* list)
{
	if (!list->is_dirty)
	{
		list->is_dirty = true;
		list->last_change_time = GetTimer(0);
		printf("GamesList: Marked dirty, will auto-save in %d seconds\n", GAMES_CACHE_DELAY_MS / 1000);
	}
	
	// Debug: Print current cache contents
	for (int i = 0; i < list->count; i++)
	{
		printf("  [%d] %c: %s\n", i, list->entries[i].type, list->entries[i].path);
	}
	printf("=== END CACHE DEBUG ===\n");
}

static void GamesList_MarkClean(GamesList* list)
{
	list->is_dirty = false;
	list->last_change_time = 0;
}

static bool GamesList_ShouldAutoSave(GamesList* list)
{
	if (!list->is_dirty || !list->auto_save_enabled)
		return false;
		
	uint32_t current_time = GetTimer(0);
	uint32_t time_since_change = current_time - list->last_change_time;
	
	// Auto-save after delay period or if max dirty time exceeded
	return (time_since_change >= GAMES_CACHE_DELAY_MS) || 
	       (time_since_change >= GAMES_CACHE_MAX_DIRTY_TIME_MS);
}

static void GamesList_ForceFlush(GamesList* list, const char* directory)
{
	if (list->is_dirty)
	{
		printf("GamesList: Force flushing changes to disk\n");
		GamesList_Save(list, directory);
	}
}

static void GamesList_CheckAutoSave(GamesList* list, const char* directory)
{
	if (GamesList_ShouldAutoSave(list))
	{
		printf("GamesList: Auto-saving after %dms delay\n", GAMES_CACHE_DELAY_MS);
		GamesList_Save(list, directory);
	}
}

// Legacy GamesList_UpdateLegacyCaches function removed - no longer needed

static int GamesList_Compare(const void* a, const void* b)
{
	const GameEntry* entry_a = (const GameEntry*)a;
	const GameEntry* entry_b = (const GameEntry*)b;
	
	// First, sort by type: d, f, t
	int priority_a = (entry_a->type == 'd') ? 0 : (entry_a->type == 'f') ? 1 : 2;
	int priority_b = (entry_b->type == 'd') ? 0 : (entry_b->type == 'f') ? 1 : 2;
	
	if (priority_a != priority_b)
		return priority_a - priority_b;
	
	// Same type, sort by filename (excluding path)
	const char* filename_a = strrchr(entry_a->path, '/');
	const char* filename_b = strrchr(entry_b->path, '/');
	filename_a = filename_a ? filename_a + 1 : entry_a->path;
	filename_b = filename_b ? filename_b + 1 : entry_b->path;
	
	return strcasecmp(filename_a, filename_b);
}

static void GamesList_Sort(GamesList* list)
{
	if (list->count > 1)
		qsort(list->entries, list->count, sizeof(GameEntry), GamesList_Compare);
}

// Ensure the GamesList has enough capacity for the given number of entries
static bool GamesList_EnsureCapacity(GamesList* list, int needed_capacity)
{
	// Cap at maximum of 512 entries
	if (needed_capacity > 512) needed_capacity = 512;
	
	if (list->capacity >= needed_capacity) return true;
	
	// Allocate memory in chunks of 64 entries to reduce fragmentation
	int new_capacity = ((needed_capacity + 63) / 64) * 64;
	if (new_capacity > 512) new_capacity = 512;
	
	GameEntry *new_entries = (GameEntry*)realloc(list->entries, new_capacity * sizeof(GameEntry));
	if (!new_entries) return false; // Allocation failed
	
	list->entries = new_entries;
	list->capacity = new_capacity;
	return true;
}

// Free the dynamically allocated memory
static void GamesList_Free(GamesList* list)
{
	if (list->entries) {
		free(list->entries);
		list->entries = NULL;
	}
	list->capacity = 0;
	list->count = 0;
}

static void GamesList_Load(GamesList* list, const char* directory)
{
	// If changing directories and we have unsaved changes, flush them first
	if (strlen(list->current_directory) > 0 && strcmp(list->current_directory, directory) != 0)
	{
		if (list->is_dirty)
		{
			printf("GamesList: Directory change - flushing pending changes for '%s'\n", list->current_directory);
			GamesList_Save(list, list->current_directory);
		}
	}
	
	char games_path[1024];
	char core_name[256];
	
	// Extract core name from directory path
	// Examples: "SNES" -> "SNES", "games/SNES/0 Try" -> "SNES", "SNES/subdir" -> "SNES"
	const char* dir_to_parse = directory;
	
	// Skip "games/" prefix if present
	if (strncmp(directory, "games/", 6) == 0)
	{
		dir_to_parse = directory + 6;
	}
	
	// Find first slash to get core name
	const char* slash_pos = strchr(dir_to_parse, '/');
	if (slash_pos)
	{
		// Copy everything before the first slash
		int core_len = slash_pos - dir_to_parse;
		strncpy(core_name, dir_to_parse, core_len);
		core_name[core_len] = 0;
	}
	else
	{
		// No slash, use the whole string
		strncpy(core_name, dir_to_parse, sizeof(core_name) - 1);
		core_name[sizeof(core_name) - 1] = 0;
	}
	
	// Check if this is _Arcade directory
	if (strcmp(core_name, "_Arcade") == 0)
	{
		snprintf(games_path, sizeof(games_path), "/media/fat/_Arcade/games.txt");
	}
	else
	{
		snprintf(games_path, sizeof(games_path), "/media/fat/games/%s/games.txt", core_name);
	}
	
	// Clear the list (keep allocated memory to avoid frequent reallocation)
	list->count = 0;
	strncpy(list->current_directory, directory, sizeof(list->current_directory) - 1);
	list->current_directory[sizeof(list->current_directory) - 1] = 0;
	
	// Mark as clean when loading
	GamesList_MarkClean(list);
	
	FILE *file = fopen(games_path, "r");
	if (!file) 
	{
			// Legacy cache update removed
		return;
	}
	
	char line[1024];
	int line_num = 0;
	int corrupt_lines = 0;
	
	while (fgets(line, sizeof(line), file))
	{
		line_num++;
		line[strcspn(line, "\r\n")] = 0;
		
		// Skip empty lines gracefully
		if (strlen(line) < 3) continue; 
		
		// Validate basic format: "type,path"
		char type_char = line[0];
		if (line[1] != ',') {
			corrupt_lines++;
			continue; // Skip malformed lines
		}
		
		char *filepath = &line[2];
		
		// Validate type character
		if (!(type_char == 'd' || type_char == 'f' || type_char == 't')) {
			corrupt_lines++;
			continue; // Skip invalid type
		}
		
		// Validate filepath is not empty and not too long
		if (strlen(filepath) == 0 || strlen(filepath) >= sizeof(list->entries[list->count].path)) {
			corrupt_lines++;
			continue; // Skip invalid paths
		}
		
		// Ensure we have capacity for this entry
		if (!GamesList_EnsureCapacity(list, list->count + 1)) {
			break; // Out of memory or hit 512 limit
		}
		
		// Add valid entry
		strncpy(list->entries[list->count].path, filepath, sizeof(list->entries[list->count].path) - 1);
		list->entries[list->count].path[sizeof(list->entries[list->count].path) - 1] = 0;
		list->entries[list->count].type = (GameType)type_char;
		list->count++;
	}
	
	// Silently handle corruption - the system continues to work with valid entries
	// In a production system, this could optionally log corruption for debugging
	
	fclose(file);
	
	
	// Check for missing files and attempt to relocate them
	GamesList_RelocateMissingFiles(list, directory);
	
	// Remove duplicate entries (same file, same type)
	GamesList_RemoveDuplicates(list);
	
	// Sort the unified list
	GamesList_Sort(list);
	
	// Legacy cache update removed - unified GamesList is now the single source
}

// Function to search for missing files and update their paths if found
static void GamesList_RelocateMissingFiles(GamesList* list, const char* directory)
{
	bool files_relocated = false;
	
	for (int i = 0; i < list->count; i++)
	{
		// Check if the file exists at its recorded path
		if (!FileExists(list->entries[i].path))
		{
			// Extract just the filename with extension
			const char *filename = strrchr(list->entries[i].path, '/');
			if (filename) filename++; else filename = list->entries[i].path;
			
			// Search for the file in the core directory
			char search_path[512];
			snprintf(search_path, sizeof(search_path), "/media/fat/games/%s", directory);
			
			char found_path[512];
			if (GamesList_SearchForFile(search_path, filename, found_path, sizeof(found_path)))
			{
				// File found at new location - update the path
				strncpy(list->entries[i].path, found_path, sizeof(list->entries[i].path) - 1);
				list->entries[i].path[sizeof(list->entries[i].path) - 1] = 0;
				
				// Mark as dirty so the updated path gets saved
				list->is_dirty = true;
				files_relocated = true;
			}
		}
	}
	
	// If files were relocated, mark for virtual folder refresh
	if (files_relocated)
	{
		// This will be checked by the UI to know it needs to refresh virtual folders
		list->last_change_time = time(NULL);
	}
}

// Recursively search for a file by name and extension in a directory tree
static bool GamesList_SearchForFile(const char* search_dir, const char* filename, char* found_path, size_t path_size)
{
	DIR *dir = opendir(search_dir);
	if (!dir) return false;
	
	struct dirent *entry;
	while ((entry = readdir(dir)) != NULL)
	{
		// Skip . and ..
		if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
			continue;
			
		char full_path[512];
		snprintf(full_path, sizeof(full_path), "%s/%s", search_dir, entry->d_name);
		
		if (entry->d_type == DT_REG)
		{
			// Check if this file matches the name we're looking for
			if (!strcmp(entry->d_name, filename))
			{
				strncpy(found_path, full_path, path_size - 1);
				found_path[path_size - 1] = 0;
				closedir(dir);
				return true;
			}
		}
		else if (entry->d_type == DT_DIR)
		{
			// Recursively search subdirectories
			if (GamesList_SearchForFile(full_path, filename, found_path, path_size))
			{
				closedir(dir);
				return true;
			}
		}
	}
	
	closedir(dir);
	return false;
}

// Remove duplicate entries (same path and type)
static void GamesList_RemoveDuplicates(GamesList* list)
{
	for (int i = 0; i < list->count; i++)
	{
		for (int j = i + 1; j < list->count; j++)
		{
			// Check if entries i and j are duplicates (same path and type)
			if (list->entries[i].type == list->entries[j].type && 
			    strcmp(list->entries[i].path, list->entries[j].path) == 0)
			{
				// Remove entry j by shifting remaining entries left
				for (int k = j; k < list->count - 1; k++)
				{
					list->entries[k] = list->entries[k + 1];
				}
				list->count--;
				j--; // Check this position again since we shifted
			}
		}
	}
}

static void GamesList_Save(GamesList* list, const char* directory)
{
	char games_path[1024];
	char core_name[256];
	
	// Extract core name from directory path
	// Examples: "SNES" -> "SNES", "games/SNES/0 Try" -> "SNES", "SNES/subdir" -> "SNES"
	const char* dir_to_parse = directory;
	
	// Skip "games/" prefix if present
	if (strncmp(directory, "games/", 6) == 0)
	{
		dir_to_parse = directory + 6;
	}
	
	// Find first slash to get core name
	const char* slash_pos = strchr(dir_to_parse, '/');
	if (slash_pos)
	{
		// Copy everything before the first slash
		int core_len = slash_pos - dir_to_parse;
		strncpy(core_name, dir_to_parse, core_len);
		core_name[core_len] = 0;
	}
	else
	{
		// No slash, use the whole string
		strncpy(core_name, dir_to_parse, sizeof(core_name) - 1);
		core_name[sizeof(core_name) - 1] = 0;
	}
	
	// Check if this is _Arcade directory
	if (strcmp(core_name, "_Arcade") == 0)
	{
		snprintf(games_path, sizeof(games_path), "/media/fat/_Arcade/games.txt");
	}
	else
	{
		snprintf(games_path, sizeof(games_path), "/media/fat/games/%s/games.txt", core_name);
	}
	
	printf("GamesList_Save: Saving to path: %s (directory='%s', count=%d)\n", games_path, directory, list->count);
	
	// If no entries, delete the file
	if (list->count == 0)
	{
		printf("GamesList_Save: No entries, removing file\n");
		remove(games_path);
		GamesList_MarkClean(list);
		// Legacy cache update removed
		return;
	}
	
	// Sort before saving
	GamesList_Sort(list);
	
	FILE *file = fopen(games_path, "w");
	if (!file) 
	{
		printf("ERROR: Could not open games file for writing: %s\n", games_path);
		return;
	}
	
	// Write all entries (already sorted by type and filename)
	for (int i = 0; i < list->count; i++)
	{
		fprintf(file, "%c,%s\n", list->entries[i].type, list->entries[i].path);
	}
	
	fclose(file);
	printf("Games file saved successfully\n");
	
	// Mark as clean after successful save
	GamesList_MarkClean(list);
	
	// Legacy cache update removed - unified GamesList handles everything
}

static bool GamesList_Contains(GamesList* list, const char* directory, const char* filename, GameType type)
{
	// Load if directory changed
	if (strcmp(list->current_directory, directory) != 0)
	{
		GamesList_Load(list, directory);
	}
	
	// Build full path - we need the actual current path, not just the core directory
	char full_path[1024];
	char *current_path = flist_Path();
	snprintf(full_path, sizeof(full_path), "/media/fat/%s/%s", current_path, filename);
	
	// Search for entry
	for (int i = 0; i < list->count; i++)
	{
		if (list->entries[i].type == type && strcmp(list->entries[i].path, full_path) == 0)
			return true;
	}
	return false;
}

static void GamesList_Toggle(GamesList* list, const char* directory, const char* filename, GameType type)
{
	// Load if directory changed
	if (strcmp(list->current_directory, directory) != 0)
	{
		GamesList_Load(list, directory);
	}
	
	// Build full path - we need the actual current path, not just the core directory
	char full_path[1024];
	if (filename[0] == '/') {
		// Already a full path (from virtual folder altname)
		strncpy(full_path, filename, sizeof(full_path) - 1);
		full_path[sizeof(full_path) - 1] = 0;
	} else {
		// Regular filename, construct full path
		char *current_path = flist_Path();
		snprintf(full_path, sizeof(full_path), "/media/fat/%s/%s", current_path, filename);
	}
	
	// Single-pass toggle: find and modify/remove/add in one loop
	for (int i = 0; i < list->count; i++)
	{
		if (strcmp(list->entries[i].path, full_path) == 0)
		{
			// Found existing entry - handle toggle logic
			if (list->entries[i].type == type)
			{
				// Same type - remove it (toggle off) using swap-and-pop for O(1)
				list->entries[i] = list->entries[list->count - 1];
				list->count--;
			}
			else
			{
				// Different type - change the state
				list->entries[i].type = type;
			}
			GamesList_MarkDirty(list);
			return; // Exit early - no need to continue loop
		}
	}
	
	// No existing entry found - add new entry
	if (GamesList_EnsureCapacity(list, list->count + 1))
	{
		strncpy(list->entries[list->count].path, full_path, sizeof(list->entries[list->count].path) - 1);
		list->entries[list->count].path[sizeof(list->entries[list->count].path) - 1] = 0;
		list->entries[list->count].type = type;
		list->count++;
		GamesList_MarkDirty(list);
	}
}

static int GamesList_CountByType(GamesList* list, GameType type)
{
	int count = 0;
	for (int i = 0; i < list->count; i++)
	{
		if (list->entries[i].type == type)
		{
			count++;
		}
	}
	return count;
}

// Backward compatibility wrapper functions
static int GamesLoad(const char* directory)
{
	GamesList_Load(&g_games_list, directory);
	return g_games_list.count;
}

static void GamesSave(const char* directory)
{
	GamesList_Save(&g_games_list, directory);
}

#if !VIRTUAL_FOLDER_SYSTEM_DISABLED
bool FavoritesIsFile(const char *directory, const char *filename)
{
	return GamesList_Contains(&g_games_list, directory, filename, GAME_TYPE_FAVORITE);
}

void FavoritesToggle(const char *directory, const char *filename)
{
	// Block favorites toggle when L+R combo is active (user trying to delete)
	if (is_lr_combo_active())
	{
		return;
	}
	GamesList_Toggle(&g_games_list, directory, filename, GAME_TYPE_FAVORITE);
}

bool TryIsFile(const char *directory, const char *filename)
{
	return GamesList_Contains(&g_games_list, directory, filename, GAME_TYPE_TRY);
}

void TryToggle(const char *directory, const char *filename)
{
	// Block try toggle when L+R combo is active (user trying to delete)
	if (is_lr_combo_active())
	{
		return;
	}
	GamesList_Toggle(&g_games_list, directory, filename, GAME_TYPE_TRY);
}

void DeleteToggle(const char *directory, const char *filename)
{
	GamesList_Toggle(&g_games_list, directory, filename, GAME_TYPE_DELETE);
}
#endif

#if !VIRTUAL_FOLDER_SYSTEM_DISABLED
// Public caching control functions
void GamesList_ProcessAutoSave()
{
	// Check if auto-save should happen for the current directory
	if (strlen(g_games_list.current_directory) > 0)
	{
		GamesList_CheckAutoSave(&g_games_list, g_games_list.current_directory);
	}
}

void GamesList_FlushChanges()
{
	// Force flush any pending changes
	if (strlen(g_games_list.current_directory) > 0)
	{
		GamesList_ForceFlush(&g_games_list, g_games_list.current_directory);
	}
}

void GamesList_SetAutoSave(bool enabled)
{
	g_games_list.auto_save_enabled = enabled;
	printf("GamesList: Auto-save %s\n", enabled ? "enabled" : "disabled");
}
#endif

#if !VIRTUAL_FOLDER_SYSTEM_DISABLED
// Additional missing functions needed by menu.cpp
bool FavoritesIsFullPath(const char *directory, const char *full_path)
{
	// Load if directory changed
	if (strcmp(g_games_list.current_directory, directory) != 0)
	{
		GamesList_Load(&g_games_list, directory);
	}
	
	// Search for entry
	for (int i = 0; i < g_games_list.count; i++)
	{
		if (g_games_list.entries[i].type == GAME_TYPE_FAVORITE && strcmp(g_games_list.entries[i].path, full_path) == 0)
			return true;
	}
	return false;
}

bool TryIsFullPath(const char *directory, const char *full_path)
{
	// Load if directory changed
	if (strcmp(g_games_list.current_directory, directory) != 0)
	{
		GamesList_Load(&g_games_list, directory);
	}
	
	// Search for entry
	for (int i = 0; i < g_games_list.count; i++)
	{
		if (g_games_list.entries[i].type == GAME_TYPE_TRY && strcmp(g_games_list.entries[i].path, full_path) == 0)
			return true;
	}
	return false;
}

bool DeleteIsFile(const char *directory, const char *filename)
{
	return GamesList_Contains(&g_games_list, directory, filename, GAME_TYPE_DELETE);
}

bool DeleteIsFullPath(const char *directory, const char *full_path)
{
	// Load if directory changed
	if (strcmp(g_games_list.current_directory, directory) != 0)
	{
		GamesList_Load(&g_games_list, directory);
	}
	
	// Search for entry
	for (int i = 0; i < g_games_list.count; i++)
	{
		if (g_games_list.entries[i].type == GAME_TYPE_DELETE && strcmp(g_games_list.entries[i].path, full_path) == 0)
			return true;
	}
	return false;
}
#endif

#if VIRTUAL_FOLDER_SYSTEM_DISABLED
// Stubbed virtual folder functions for size testing
static int ScanVirtualFolder(const char *core_path, GameType game_type, uint32_t flags, const char *type_name) { return 0; }
int ScanVirtualFavorites(const char *core_path) { return 0; }
int ScanVirtualTry(const char *core_path) { return 0; }
int ScanVirtualDelete(const char *core_path) { return 0; }
void FavoritesToggle(const char *directory, const char *filename) {}
void TryToggle(const char *directory, const char *filename) {}
void DeleteToggle(const char *directory, const char *filename) {}
void GamesList_ProcessAutoSave() {}
void GamesList_FlushChanges() {}
void GamesList_SetAutoSave(bool enabled) {}
bool FavoritesIsFile(const char *directory, const char *filename) { return false; }
bool TryIsFile(const char *directory, const char *filename) { return false; }
bool DeleteIsFile(const char *directory, const char *filename) { return false; }
bool FavoritesIsFullPath(const char *directory, const char *full_path) { return false; }
bool TryIsFullPath(const char *directory, const char *full_path) { return false; }
bool DeleteIsFullPath(const char *directory, const char *full_path) { return false; }
#else
// Unified virtual folder scanner for all game types
static int ScanVirtualFolder(const char *core_path, GameType game_type, uint32_t flags, const char *type_name)
{
	
	// Extract core name from path first
	// This handles two different path formats:
	// 1. Standard games paths: "games/SNES" -> core_name="SNES"  
	// 2. _Arcade paths: "_Arcade" -> core_name="_Arcade"
	const char *core_name;
	const char *games_pos = strstr(core_path, "games/");
	if (games_pos) {
		// Standard games path like "games/SNES" - extract just the core name
		core_name = games_pos + 6; // Skip "games/" prefix
	} else if (core_path[0] == '_') {
		// _Arcade path like "_Arcade" - use the full path as core name
		core_name = core_path;
	} else {
		// Unrecognized path format - skip virtual folder creation
		return 0;
	}
	
	
	// Clear current directory listing
	DirItem.clear();
	DirNames.clear();
	iSelectedEntry = 0;
	iFirstEntry = 0;
	
	// Add ".." entry as first item to allow going back to parent directory
	direntext_t parent_item;
	memset(&parent_item, 0, sizeof(parent_item));
	parent_item.de.d_type = DT_DIR;
	strcpy(parent_item.de.d_name, "..");
	strcpy(parent_item.altname, core_path); // Store parent path in altname
	DirItem.push_back(parent_item);
	
	// Load the games list for this core (only if not already loaded for this directory)
	if (strcmp(g_games_list.current_directory, core_name) != 0) {
		GamesList_Load(&g_games_list, core_name);
	} else {
	}
	
	// Add items of the specified type as virtual files
	int count = 1; // Start at 1 to account for ".." entry
	for (int i = 0; i < g_games_list.count; i++)
	{
		if (g_games_list.entries[i].type == game_type)
		{
			direntext_t item;
			memset(&item, 0, sizeof(item));
			
			// Extract just the filename from the full path
			// Example: "/media/fat/games/SNES/Super Mario World.sfc" -> "Super Mario World.sfc"
			const char *filename = strrchr(g_games_list.entries[i].path, '/');
			if (filename) filename++; else filename = g_games_list.entries[i].path;
			
			// Create clean display name (remove file extension for better UI)
			char clean_name[256];
			strncpy(clean_name, filename, sizeof(clean_name) - 1);
			clean_name[sizeof(clean_name) - 1] = 0;
			
			// Remove file extension from clean name  
			// Example: "Super Mario World.sfc" -> "Super Mario World"
			char *ext_pos = strrchr(clean_name, '.');
			if (ext_pos) *ext_pos = 0;
			
			// Set as regular file with clean name (special symbols added by PrintDirectory)
			item.de.d_type = DT_REG;
			snprintf(item.de.d_name, sizeof(item.de.d_name), "%s", clean_name);
			
			// Store full path in altname for game loading - this is critical for virtual folders
			// The altname field contains the real file path while d_name contains the display name
			strncpy(item.altname, g_games_list.entries[i].path, sizeof(item.altname) - 1);
			item.altname[sizeof(item.altname) - 1] = 0;
			
			item.flags = flags; // Mark as virtual item of specified type
			
			printf("ScanVirtual%s: Adding item[%d] d_name='%s', altname='%s', flags=0x%X\n", 
			       type_name, count, item.de.d_name, item.altname, item.flags);
			
			DirItem.push_back(item);
			count++;
		}
	}
	
	return count;
}

int ScanVirtualFavorites(const char *core_path)
{
	return ScanVirtualFolder(core_path, GAME_TYPE_FAVORITE, 0x8001, "Favorites");
}

int ScanVirtualTry(const char *core_path)
{
	return ScanVirtualFolder(core_path, GAME_TYPE_TRY, 0x8002, "Try");
}

int ScanVirtualDelete(const char *core_path)
{
	return ScanVirtualFolder(core_path, GAME_TYPE_DELETE, 0x8003, "Delete");
}
#endif

