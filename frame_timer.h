#ifndef FRAME_TIMER_H
#define FRAME_TIMER_H

#include <stdint.h>

#define MAX_FRAME_CALLBACKS 16

typedef void (*frame_callback_t)(void);

void frame_timer();
void add_frame_callback(frame_callback_t cb);
#ifdef BENCHMARK
void frame_timer_benchmark_reset_callbacks();
int frame_timer_benchmark_callback_count();
#endif

// global
extern uint64_t global_frame_counter; // used by FRAME_TICK()
extern bool fpga_vsync_timer;         // does this core expose the frame counter directly?
                                      // exposed globally in case timerfd isn't accurate enough for some use cases

#endif
