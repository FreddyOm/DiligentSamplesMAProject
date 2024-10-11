#pragma once
#include <DebugUtilities.hpp>
#include <string>

typedef unsigned char byte;

struct BinvoxData
{
    int   version = -1;
    int   depth, height, width;
    int   size = 0;
    byte* voxels = 0;
    float tx, ty, tz;
    float scale = 0.0f; 
};

size_t get_index(int x, int y, int z, BinvoxData data);

BinvoxData read_binvox(std::string filespec);
