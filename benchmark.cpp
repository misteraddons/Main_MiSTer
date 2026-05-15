#ifdef BENCHMARK

#include "benchmark.h"

#include <algorithm>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <vector>

#include "frame_timer.h"
#include "input.h"

static uint64_t now_us()
{
	struct timespec ts = {};
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000000ULL + ts.tv_nsec / 1000ULL;
}

static void print_summary(const char *name, const std::vector<uint64_t> &samples, int iterations)
{
	if (samples.empty())
	{
		printf("BENCH_SUMMARY,%s,count=0\n", name);
		return;
	}

	std::vector<uint64_t> sorted = samples;
	std::sort(sorted.begin(), sorted.end());
	size_t p95_idx = (sorted.size() * 95) / 100;
	if (p95_idx >= sorted.size()) p95_idx = sorted.size() - 1;
	uint64_t median = sorted[sorted.size() / 2];
	uint64_t median_ns = iterations > 0 ? (median * 1000ULL) / (uint64_t)iterations : 0;

	printf("BENCH_SUMMARY,%s,count=%u,iterations=%d,min_us=%llu,median_us=%llu,p95_us=%llu,max_us=%llu,median_ns_per_iter=%llu\n",
		name,
		(unsigned)sorted.size(),
		iterations,
		(unsigned long long)sorted.front(),
		(unsigned long long)median,
		(unsigned long long)sorted[p95_idx],
		(unsigned long long)sorted.back(),
		(unsigned long long)median_ns);
}

static void dummy_frame_callback()
{
}

static uint32_t bench_duplicate_callback_register(int iterations)
{
	frame_timer_benchmark_reset_callbacks();
	add_frame_callback(dummy_frame_callback);

	for (int i = 0; i < iterations; i++)
	{
		add_frame_callback(dummy_frame_callback);
	}

	return (uint32_t)frame_timer_benchmark_callback_count();
}

static void run_timed(const char *name, int samples, int iterations, uint32_t (*fn)(int))
{
	std::vector<uint64_t> values;
	values.reserve(samples);
	uint32_t sink = 0;

	for (int i = 0; i < samples; i++)
	{
		uint64_t start = now_us();
		sink ^= fn(iterations);
		uint64_t elapsed = now_us() - start;
		printf("BENCH_SAMPLE,%s,iter=%d,us=%llu,sink=%u\n",
			name, i, (unsigned long long)elapsed, sink);
		values.push_back(elapsed);
	}

	print_summary(name, values, iterations);
}

int benchmark_input_callbacks(int argc, char *argv[])
{
	setvbuf(stdout, NULL, _IONBF, 0);

	int samples = 7;
	int iterations = 100000;
	int keys_per_player = 64;
	int autofire_stride = 4;

	for (int i = 0; i < argc; i++)
	{
		if (!strcmp(argv[i], "--samples") && i + 1 < argc)
		{
			samples = atoi(argv[++i]);
			if (samples < 1) samples = 1;
		}
		else if (!strcmp(argv[i], "--iterations") && i + 1 < argc)
		{
			iterations = atoi(argv[++i]);
			if (iterations < 1) iterations = 1;
		}
		else if (!strcmp(argv[i], "--keys") && i + 1 < argc)
		{
			keys_per_player = atoi(argv[++i]);
			if (keys_per_player < 1) keys_per_player = 1;
		}
		else if (!strcmp(argv[i], "--autofire-stride") && i + 1 < argc)
		{
			autofire_stride = atoi(argv[++i]);
			if (autofire_stride < 0) autofire_stride = 0;
		}
	}

	printf("BENCH_INFO,name=input_callbacks,samples=%d,iterations=%d,keys_per_player=%d,autofire_stride=%d\n",
		samples, iterations, keys_per_player, autofire_stride);

	input_benchmark_prepare(keys_per_player, autofire_stride);
	run_timed("callback_duplicate_register", samples, iterations, bench_duplicate_callback_register);

	input_benchmark_prepare(keys_per_player, autofire_stride);
	run_timed("input_mask_idle", samples, iterations, input_benchmark_mask_idle);

	input_benchmark_prepare(keys_per_player, autofire_stride);
	run_timed("input_mask_dirty", samples, iterations, input_benchmark_mask_dirty);

	input_benchmark_prepare(keys_per_player, autofire_stride);
	run_timed("input_autofire_frame_cb", samples, iterations, input_benchmark_autofire_frame_cb);

	return 0;
}

#endif
