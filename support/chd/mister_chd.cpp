#include <stdarg.h>
#include <libchdr/chd.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <time.h>
#include "../../file_io.h"
#include "../../cd.h"
#include "mister_chd.h"

void lba_to_hunkinfo(chd_file *chd_f, int lba, int *hunknumber, int *hunkoffset)
{
	const chd_header *chd_header = chd_get_header(chd_f);
	int sectors_per_hunk = chd_header->hunkbytes / chd_header->unitbytes;
	*hunknumber = lba / sectors_per_hunk;
	*hunkoffset = lba % sectors_per_hunk;
	return;
}

int mister_chd_log(const char *format, ...)
{
	char logline[1024];
	va_list args;
	va_start(args, format);
	vsnprintf(logline, sizeof(logline), format, args);
	va_end(args);
	return printf("\x1b[32m%s\x1b[0m", logline);
}

struct ChdProfileStats
{
	bool env_loaded;
	bool enabled;
	chd_file *file;
	uint64_t interval;
	uint64_t sector_reads;
	uint64_t hunk_hits;
	uint64_t hunk_misses;
	uint64_t prefetch_hits;
	uint64_t prefetch_waits;
	uint64_t sequential_hunk_misses;
	uint64_t backward_reads;
	uint64_t bytes_copied;
	uint64_t chd_read_ns;
	uint64_t prefetch_wait_ns;
	uint64_t memcpy_ns;
	uint64_t max_chd_read_ns;
	uint64_t max_prefetch_wait_ns;
	uint64_t max_memcpy_ns;
	bool has_last_read;
	int last_lba;
	int last_hunk;
	int sectors_per_hunk;
};

static ChdProfileStats chd_profile = {};

static uint64_t chd_profile_now_ns()
{
	struct timespec ts = {};
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

static bool chd_profile_enabled()
{
	if (chd_profile.env_loaded) return chd_profile.enabled;

	const char *enabled = getenv("CHD_PROFILE");
	chd_profile.enabled = enabled && enabled[0] && strcmp(enabled, "0");
	chd_profile.interval = 4096;

	const char *interval = getenv("CHD_PROFILE_INTERVAL");
	if (interval && interval[0])
	{
		uint64_t parsed = strtoull(interval, NULL, 10);
		if (parsed) chd_profile.interval = parsed;
	}

	chd_profile.env_loaded = true;
	return chd_profile.enabled;
}

void mister_chd_profile_dump()
{
	if (!chd_profile_enabled() || !chd_profile.sector_reads) return;

	uint64_t hit_pct = (chd_profile.hunk_hits * 100) / chd_profile.sector_reads;
	uint64_t avg_chd_us = chd_profile.hunk_misses ? (chd_profile.chd_read_ns / chd_profile.hunk_misses) / 1000 : 0;
	uint64_t max_chd_us = chd_profile.max_chd_read_ns / 1000;
	uint64_t avg_prefetch_wait_us = chd_profile.prefetch_waits ? (chd_profile.prefetch_wait_ns / chd_profile.prefetch_waits) / 1000 : 0;
	uint64_t max_prefetch_wait_us = chd_profile.max_prefetch_wait_ns / 1000;
	uint64_t avg_memcpy_ns = chd_profile.sector_reads ? chd_profile.memcpy_ns / chd_profile.sector_reads : 0;

	mister_chd_log(
		"CHD profile: reads=%llu bytes=%llu hunk_hits=%llu hunk_misses=%llu hit=%llu%% prefetch_hits=%llu prefetch_waits=%llu sequential_misses=%llu backward_reads=%llu avg_chd_us=%llu max_chd_us=%llu avg_prefetch_wait_us=%llu max_prefetch_wait_us=%llu avg_memcpy_ns=%llu max_memcpy_ns=%llu sectors_per_hunk=%d\n",
		(unsigned long long)chd_profile.sector_reads,
		(unsigned long long)chd_profile.bytes_copied,
		(unsigned long long)chd_profile.hunk_hits,
		(unsigned long long)chd_profile.hunk_misses,
		(unsigned long long)hit_pct,
		(unsigned long long)chd_profile.prefetch_hits,
		(unsigned long long)chd_profile.prefetch_waits,
		(unsigned long long)chd_profile.sequential_hunk_misses,
		(unsigned long long)chd_profile.backward_reads,
		(unsigned long long)avg_chd_us,
		(unsigned long long)max_chd_us,
		(unsigned long long)avg_prefetch_wait_us,
		(unsigned long long)max_prefetch_wait_us,
		(unsigned long long)avg_memcpy_ns,
		(unsigned long long)chd_profile.max_memcpy_ns,
		chd_profile.sectors_per_hunk);
}

static void chd_profile_reset_file(chd_file *chd_f)
{
	if (!chd_profile_enabled()) return;
	if (chd_profile.file == chd_f) return;

	mister_chd_profile_dump();

	bool env_loaded = chd_profile.env_loaded;
	bool enabled = chd_profile.enabled;
	uint64_t interval = chd_profile.interval;
	memset(&chd_profile, 0, sizeof(chd_profile));
	chd_profile.env_loaded = env_loaded;
	chd_profile.enabled = enabled;
	chd_profile.interval = interval;
	chd_profile.file = chd_f;

	const chd_header *chd_header = chd_get_header(chd_f);
	if (chd_header && chd_header->unitbytes)
	{
		chd_profile.sectors_per_hunk = chd_header->hunkbytes / chd_header->unitbytes;
	}
}

static void chd_profile_record(chd_file *chd_f, int lba, int hunknum, bool hunk_hit, bool prefetch_hit, bool prefetch_wait, int length, uint64_t chd_read_ns, uint64_t prefetch_wait_ns, uint64_t memcpy_ns)
{
	if (!chd_profile_enabled()) return;

	chd_profile_reset_file(chd_f);
	chd_profile.sector_reads++;
	chd_profile.bytes_copied += length;
	chd_profile.chd_read_ns += chd_read_ns;
	chd_profile.prefetch_wait_ns += prefetch_wait_ns;
	chd_profile.memcpy_ns += memcpy_ns;
	if (chd_read_ns > chd_profile.max_chd_read_ns) chd_profile.max_chd_read_ns = chd_read_ns;
	if (prefetch_wait_ns > chd_profile.max_prefetch_wait_ns) chd_profile.max_prefetch_wait_ns = prefetch_wait_ns;
	if (memcpy_ns > chd_profile.max_memcpy_ns) chd_profile.max_memcpy_ns = memcpy_ns;

	if (hunk_hit)
	{
		chd_profile.hunk_hits++;
	}
	else
	{
		if (chd_profile.has_last_read && hunknum == chd_profile.last_hunk + 1) chd_profile.sequential_hunk_misses++;
		chd_profile.hunk_misses++;
	}
	if (prefetch_hit) chd_profile.prefetch_hits++;
	if (prefetch_wait) chd_profile.prefetch_waits++;

	if (chd_profile.has_last_read && lba < chd_profile.last_lba) chd_profile.backward_reads++;
	chd_profile.has_last_read = true;
	chd_profile.last_lba = lba;
	chd_profile.last_hunk = hunknum;

	if (chd_profile.interval && !(chd_profile.sector_reads % chd_profile.interval)) mister_chd_profile_dump();
}

struct ChdPrefetchState
{
	bool env_loaded;
	bool enabled;
	bool active;
	bool thread_started;
	bool stop;
	bool request_pending;
	bool in_progress;
	bool ready;
	chd_file *main_file;
	chd_file *prefetch_file;
	uint8_t *buffer;
	uint32_t hunkbytes;
	uint32_t totalhunks;
	int request_hunk;
	int in_progress_hunk;
	int ready_hunk;
	chd_error ready_error;
	pthread_t thread;
};

static ChdPrefetchState chd_prefetch = {};
static pthread_mutex_t chd_prefetch_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t chd_prefetch_cond = PTHREAD_COND_INITIALIZER;

static bool chd_prefetch_enabled()
{
	if (chd_prefetch.env_loaded) return chd_prefetch.enabled;

	const char *enabled = getenv("CHD_PREFETCH");
	chd_prefetch.enabled = enabled && enabled[0] && strcmp(enabled, "0");
	chd_prefetch.env_loaded = true;
	return chd_prefetch.enabled;
}

static void *chd_prefetch_thread(void *)
{
	while (1)
	{
		pthread_mutex_lock(&chd_prefetch_lock);
		while (!chd_prefetch.stop && !chd_prefetch.request_pending)
		{
			pthread_cond_wait(&chd_prefetch_cond, &chd_prefetch_lock);
		}

		if (chd_prefetch.stop)
		{
			pthread_mutex_unlock(&chd_prefetch_lock);
			return NULL;
		}

		int hunknum = chd_prefetch.request_hunk;
		chd_file *prefetch_file = chd_prefetch.prefetch_file;
		uint8_t *buffer = chd_prefetch.buffer;
		chd_prefetch.request_pending = false;
		chd_prefetch.in_progress = true;
		chd_prefetch.in_progress_hunk = hunknum;
		pthread_mutex_unlock(&chd_prefetch_lock);

		chd_error err = chd_read(prefetch_file, hunknum, buffer);

		pthread_mutex_lock(&chd_prefetch_lock);
		chd_prefetch.in_progress = false;
		if (!chd_prefetch.stop)
		{
			chd_prefetch.ready = true;
			chd_prefetch.ready_hunk = hunknum;
			chd_prefetch.ready_error = err;
		}
		pthread_cond_broadcast(&chd_prefetch_cond);
		pthread_mutex_unlock(&chd_prefetch_lock);
	}
}

static void chd_prefetch_stop_locked()
{
	if (chd_prefetch.thread_started)
	{
		chd_prefetch.stop = true;
		pthread_cond_broadcast(&chd_prefetch_cond);
		pthread_mutex_unlock(&chd_prefetch_lock);
		pthread_join(chd_prefetch.thread, NULL);
		pthread_mutex_lock(&chd_prefetch_lock);
	}

	if (chd_prefetch.prefetch_file)
	{
		chd_close(chd_prefetch.prefetch_file);
	}
	free(chd_prefetch.buffer);

	bool env_loaded = chd_prefetch.env_loaded;
	bool enabled = chd_prefetch.enabled;
	memset(&chd_prefetch, 0, sizeof(chd_prefetch));
	chd_prefetch.env_loaded = env_loaded;
	chd_prefetch.enabled = enabled;
}

static void chd_prefetch_set_file(const char *filename, chd_file *main_file, const chd_header *chd_header)
{
	if (!chd_prefetch_enabled()) return;

	pthread_mutex_lock(&chd_prefetch_lock);
	chd_prefetch_stop_locked();

	chd_prefetch.buffer = (uint8_t *)malloc(chd_header->hunkbytes);
	if (!chd_prefetch.buffer)
	{
		pthread_mutex_unlock(&chd_prefetch_lock);
		mister_chd_log("CHD prefetch disabled: cannot allocate %u bytes\n", chd_header->hunkbytes);
		return;
	}

	chd_error err = chd_open(getFullPath(filename), CHD_OPEN_READ, NULL, &chd_prefetch.prefetch_file);
	if (err != CHDERR_NONE)
	{
		free(chd_prefetch.buffer);
		chd_prefetch.buffer = NULL;
		pthread_mutex_unlock(&chd_prefetch_lock);
		mister_chd_log("CHD prefetch disabled: %s\n", chd_error_string(err));
		return;
	}

	int chd_fd = fileno((FILE *)chd_core_file(chd_prefetch.prefetch_file)->argp);
	if (chd_fd) fcntl(chd_fd, F_SETFD, FD_CLOEXEC);

	chd_prefetch.active = true;
	chd_prefetch.main_file = main_file;
	chd_prefetch.hunkbytes = chd_header->hunkbytes;
	chd_prefetch.totalhunks = chd_header->totalhunks;
	chd_prefetch.request_hunk = -1;
	chd_prefetch.in_progress_hunk = -1;
	chd_prefetch.ready_hunk = -1;

	int ret = pthread_create(&chd_prefetch.thread, NULL, chd_prefetch_thread, NULL);
	if (ret)
	{
		chd_prefetch_stop_locked();
		pthread_mutex_unlock(&chd_prefetch_lock);
		mister_chd_log("CHD prefetch disabled: pthread_create failed\n");
		return;
	}

	chd_prefetch.thread_started = true;
	pthread_mutex_unlock(&chd_prefetch_lock);
	mister_chd_log("CHD prefetch enabled\n");
}

static bool chd_prefetch_try_read(chd_file *chd_f, int hunknum, uint8_t *hunkbuf, uint64_t *wait_ns, bool *waited)
{
	*wait_ns = 0;
	*waited = false;
	if (!chd_prefetch_enabled()) return false;

	uint64_t wait_start_ns = 0;
	pthread_mutex_lock(&chd_prefetch_lock);
	if (!chd_prefetch.active || chd_prefetch.main_file != chd_f)
	{
		pthread_mutex_unlock(&chd_prefetch_lock);
		return false;
	}

	if ((chd_prefetch.request_pending && chd_prefetch.request_hunk == hunknum) ||
		(chd_prefetch.in_progress && chd_prefetch.in_progress_hunk == hunknum))
	{
		wait_start_ns = chd_profile_now_ns();
		*waited = true;
		while (!chd_prefetch.stop &&
			((chd_prefetch.request_pending && chd_prefetch.request_hunk == hunknum) ||
			 (chd_prefetch.in_progress && chd_prefetch.in_progress_hunk == hunknum)))
		{
			pthread_cond_wait(&chd_prefetch_cond, &chd_prefetch_lock);
		}
		*wait_ns = chd_profile_now_ns() - wait_start_ns;
	}

	bool hit = chd_prefetch.ready && chd_prefetch.ready_hunk == hunknum && chd_prefetch.ready_error == CHDERR_NONE;
	if (hit)
	{
		memcpy(hunkbuf, chd_prefetch.buffer, chd_prefetch.hunkbytes);
		chd_prefetch.ready = false;
		chd_prefetch.ready_hunk = -1;
	}
	pthread_mutex_unlock(&chd_prefetch_lock);
	return hit;
}

static void chd_prefetch_schedule(chd_file *chd_f, int hunknum)
{
	if (!chd_prefetch_enabled()) return;

	pthread_mutex_lock(&chd_prefetch_lock);
	if (!chd_prefetch.active || chd_prefetch.main_file != chd_f || hunknum < 0 || hunknum >= (int)chd_prefetch.totalhunks)
	{
		pthread_mutex_unlock(&chd_prefetch_lock);
		return;
	}

	if ((chd_prefetch.ready && chd_prefetch.ready_hunk == hunknum) ||
		(chd_prefetch.request_pending && chd_prefetch.request_hunk == hunknum) ||
		(chd_prefetch.in_progress && chd_prefetch.in_progress_hunk == hunknum))
	{
		pthread_mutex_unlock(&chd_prefetch_lock);
		return;
	}

	chd_prefetch.request_hunk = hunknum;
	chd_prefetch.request_pending = true;
	pthread_cond_signal(&chd_prefetch_cond);
	pthread_mutex_unlock(&chd_prefetch_lock);
}

void mister_chd_close(chd_file *chd_f)
{
	if (!chd_f) return;

	mister_chd_profile_dump();

	pthread_mutex_lock(&chd_prefetch_lock);
	if (chd_prefetch.active && chd_prefetch.main_file == chd_f)
	{
		chd_prefetch_stop_locked();
	}
	pthread_mutex_unlock(&chd_prefetch_lock);

	chd_close(chd_f);
}

chd_error mister_load_chd(const char *filename, toc_t *cd_toc)
{
	cd_toc->last = -1;
	
	chd_error err = chd_open(getFullPath(filename), CHD_OPEN_READ, NULL, &cd_toc->chd_f);
	if (err != CHDERR_NONE)
	{
		cd_toc->chd_f = NULL;
		return err;
	}

	//TODO: deal with non v5 chd versions
	const chd_header *chd_header = chd_get_header(cd_toc->chd_f);
	if (!chd_header)
	{
		mister_chd_close(cd_toc->chd_f);
		return CHDERR_NO_INTERFACE; //I'm not sure this error condition is possible, so just use whatever
	}

	mister_chd_log("hunkbytes %d unitbytes %d logical length %llu\n", chd_header->hunkbytes, chd_header->unitbytes, chd_header->logicalbytes);
	cd_toc->chd_hunksize = chd_header->hunkbytes;
	chd_profile_reset_file(cd_toc->chd_f);
	chd_prefetch_set_file(filename, cd_toc->chd_f, chd_header);

	//Set CLOEXEC on underlying FD
	int chd_fd = fileno((FILE *)chd_core_file(cd_toc->chd_f)->argp);
	if (chd_fd) fcntl(chd_fd, F_SETFD, FD_CLOEXEC);

	//Load track info
	int sector_cnt = 0;
	for (cd_toc->last = 0; cd_toc->last < 99; cd_toc->last++)
	{
		char tmp[512];
		int track_id = 0, frames = 0, pregap = 0, postgap = 0;
		char track_type[64], subtype[32], pgtype[32], pgsub[32];

		if (chd_get_metadata(cd_toc->chd_f, CDROM_TRACK_METADATA2_TAG, cd_toc->last, tmp, sizeof(tmp), NULL, NULL, NULL) == CHDERR_NONE)
		{
			if (sscanf(tmp, CDROM_TRACK_METADATA2_FORMAT, &track_id, track_type, subtype, &frames, &pregap, pgtype, pgsub, &postgap) != 8) break;
		}
		else if (chd_get_metadata(cd_toc->chd_f, CDROM_TRACK_METADATA_TAG, cd_toc->last, tmp, sizeof(tmp), NULL, NULL, NULL) == CHDERR_NONE) {
			if (sscanf(tmp, CDROM_TRACK_METADATA_FORMAT, &track_id, track_type, subtype, &frames) != 4) break;
		}
		else {
			//No more tracks
			break;
		}

		bool pregap_valid = true;

		if (pgtype[0] != 'V')
		{
			pregap_valid = false;

		}

		if (cd_toc->last)
		{
			if (!pregap_valid)
			{
				cd_toc->tracks[cd_toc->last - 1].end += pregap;
			}
			cd_toc->end = cd_toc->tracks[cd_toc->last - 1].end;
			cd_toc->tracks[cd_toc->last].start = cd_toc->end;
			if (pregap_valid)
			{
				cd_toc->tracks[cd_toc->last].start += pregap;
			}

			cd_toc->tracks[cd_toc->last].indexes[1] = pregap;
		}
		else {
			if (pregap_valid)
			{
				cd_toc->tracks[cd_toc->last].start = pregap;
			}
			else {
				cd_toc->tracks[cd_toc->last].start = 0;
			}
			cd_toc->tracks[cd_toc->last].indexes[1] = pregap;
		}
		cd_toc->tracks[cd_toc->last].index_num = 2;

		if (!pregap_valid)
		{
			//Pregap sectors are NOT included in the CHD for this track
			pregap = 0;
		}

		if (!strcmp(track_type, "MODE1_RAW"))
		{
			cd_toc->tracks[cd_toc->last].sector_size = 2352;
			cd_toc->tracks[cd_toc->last].type = TT_MODE1;
		}
		else if (!strcmp(track_type, "MODE2_RAW")) {
			cd_toc->tracks[cd_toc->last].sector_size = 2352;
			cd_toc->tracks[cd_toc->last].type = TT_MODE2;
		}
		else if (!strcmp(track_type, "MODE1")) {
			cd_toc->tracks[cd_toc->last].sector_size = 2048;
			cd_toc->tracks[cd_toc->last].type = TT_MODE1;
		}
		else if (!strcmp(track_type, "MODE2")) {
			cd_toc->tracks[cd_toc->last].sector_size = 2336;
			cd_toc->tracks[cd_toc->last].type = TT_MODE2;
		}
		else if (!strcmp(track_type, "AUDIO")) {
			cd_toc->tracks[cd_toc->last].sector_size = 2352;
			cd_toc->tracks[cd_toc->last].type = TT_CDDA;
		}
		else {
			cd_toc->tracks[cd_toc->last].sector_size = 0;
			cd_toc->tracks[cd_toc->last].type = TT_CDDA;
		}

		cd_toc->tracks[cd_toc->last].sbc_type = SUBCODE_NONE;
		if (!strcmp(subtype, "RW")) {
			cd_toc->tracks[cd_toc->last].sbc_type = SUBCODE_RW;
		}
		else if (!strcmp(subtype, "RW_RAW")) {
			cd_toc->tracks[cd_toc->last].sbc_type = SUBCODE_RW_RAW;
		}

		//CHD pads tracks to a multiple of 4 sectors, keep track of the overall sector count and calculate the difference between the cdrom lba and the effective chd lba
		cd_toc->tracks[cd_toc->last].offset = (sector_cnt + pregap - cd_toc->tracks[cd_toc->last].start);
		cd_toc->tracks[cd_toc->last].end = cd_toc->tracks[cd_toc->last].start + frames - pregap;
		cd_toc->end = cd_toc->tracks[cd_toc->last].end + postgap;
		sector_cnt += ((frames + CD_TRACK_PADDING - 1) / CD_TRACK_PADDING) * CD_TRACK_PADDING;
		mister_chd_log("Track %d: Type: %s PreGap: %d PreGapType: %s Frames: %d start: %d end %d\n", cd_toc->last, track_type, pregap, pgtype, frames, cd_toc->tracks[cd_toc->last].start, cd_toc->tracks[cd_toc->last].end);

	}
	return CHDERR_NONE;
}

chd_error mister_chd_read_sector(chd_file *chd_f, int lba, uint32_t d_offset, uint32_t s_offset, int length, uint8_t *destbuf, uint8_t *hunkbuf, int *hunknum)
{

	int tmphnum = 0;
	int hunkofs = 0;
	bool profile = chd_profile_enabled();
	uint64_t chd_start_ns = 0;
	uint64_t chd_read_ns = 0;
	uint64_t prefetch_wait_ns = 0;
	uint64_t memcpy_start_ns = 0;
	uint64_t memcpy_ns = 0;
	bool prefetch_hit = false;
	bool prefetch_wait = false;

	lba_to_hunkinfo(chd_f, lba, &tmphnum, &hunkofs);
	int sector_offset = hunkofs * CD_FRAME_SIZE;


	//mister_chd_log("READ LBA: %d, dest_offset: %d sector offset: %d length %d chd_f %p\n", lba, d_offset, s_offset, length, chd_f);
	bool hunk_hit = tmphnum == *hunknum;
	if (!hunk_hit)
	{
		prefetch_hit = chd_prefetch_try_read(chd_f, tmphnum, hunkbuf, &prefetch_wait_ns, &prefetch_wait);
		if (prefetch_hit)
		{
			hunk_hit = true;
		}
		else
		{
			if (profile) chd_start_ns = chd_profile_now_ns();
			chd_error err = chd_read(chd_f, tmphnum, hunkbuf);
			if (profile) chd_read_ns = chd_profile_now_ns() - chd_start_ns;
			if (err != CHDERR_NONE)
			{
				mister_chd_log("ERROR %s\n", chd_error_string(err));
				return err;
			}
		}
		*hunknum = tmphnum;
	}
	if (profile) memcpy_start_ns = chd_profile_now_ns();
	memcpy(destbuf + d_offset, hunkbuf + sector_offset + s_offset, length);
	if (profile) memcpy_ns = chd_profile_now_ns() - memcpy_start_ns;
	chd_prefetch_schedule(chd_f, tmphnum + 1);
	chd_profile_record(chd_f, lba, tmphnum, hunk_hit, prefetch_hit, prefetch_wait, length, chd_read_ns, prefetch_wait_ns, memcpy_ns);
	return CHDERR_NONE;
}
