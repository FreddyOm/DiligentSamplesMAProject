#pragma once

#include <stdint.h>

// This struct defines VoxelData for our voxelizer.
// This is the main memory hogger: the less data you store here, the better.
struct VoxelData{
	uint64_t morton;
	
	VoxelData() : morton(0) {}
	VoxelData(uint64_t morton) : morton(morton) {}

	bool operator >(VoxelData &a){
		return morton > a.morton;
	}

	bool operator <(VoxelData &a){
		return morton < a.morton;
	}
};

const size_t VOXELDATA_SIZE = sizeof(VoxelData);
