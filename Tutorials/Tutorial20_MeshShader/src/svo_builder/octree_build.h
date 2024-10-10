#pragma once

#include "Octree.h"
#include "../octree/loader/octree_io.h"
#include <string>
#include <fstream>
#include <iostream>

using namespace std;

int readOctree(std::string basefilename, Octree*& octree);
