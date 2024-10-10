#pragma once

#include <glm/glm.hpp>
#include <stdint.h>

// This struct defines VoxelData for our voxelizer.
// This is the main memory hogger: the less data you store here, the better.
struct VoxelData{
	uint64_t morton;
	
	glm::vec3 color;
    glm::vec3 normal;

	VoxelData() :
        morton(0), normal(glm::vec3()), color(glm::vec3()) {}
    VoxelData(::uint_fast64_t morton, glm::vec3 normal, glm::vec3 color) :
        morton(morton), normal(normal), color(color) {}

	bool operator >(VoxelData &a){
		return morton > a.morton;
	}

	bool operator <(VoxelData &a){
		return morton < a.morton;
	}
};

const size_t VOXELDATA_SIZE = sizeof(VoxelData);
