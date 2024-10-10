#pragma once

#include "svo_builder_util.h"
#include "timer.h"

using namespace std;

// global flag: be verbose about what we do?
extern bool verbose;

// Timers (for debugging purposes)
// (This is a bit ugly, but it's a quick and surefire way to measure performance)

// Main program timer
Timer main_timer;

// Timers for partitioning step
Timer part_total_timer;
Timer part_io_in_timer;
Timer part_io_out_timer;
Timer part_algo_timer;

// Timers for voxelizing step
Timer vox_total_timer;
Timer vox_io_in_timer;
Timer vox_algo_timer;

// Timers for SVO building step
Timer svo_total_timer;
Timer svo_io_out_timer;
Timer svo_algo_timer;