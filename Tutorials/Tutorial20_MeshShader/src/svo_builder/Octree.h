#pragma once

#include "../octree/loader/octree_io_node.h"
#include "DataPoint.h"
#include <trimesh/include/TriMesh.h>
#include <vector>

class Octree
{
public:
	// Octree position/size
    trimesh::vec3 min;
    trimesh::vec3 max;
    trimesh::vec3 size;
	size_t gridlength;

	// Octree stats
	size_t n_nodes;
	size_t n_data;

	// The data
	VoxelData* data;

	// The octree nodes
	std::vector<Node> nodes;

	Octree(trimesh::vec3 min, trimesh::vec3 max, size_t gridlength);
	Octree();
	size_t storeNode(Node n);
	const Node* getNode(size_t index) const;
	const Node* getRootNode() const;
	Node* getNode(size_t index);
	Node* getRootNode();
	~Octree(void);
};

inline Octree::Octree(trimesh::vec3 min, trimesh::vec3 max, size_t gridlength)
	: min(min), max(max), size(min + max), gridlength(gridlength){
		nodes.push_back(Node()); // push back NULL node
}

inline Octree::Octree() :
    min(trimesh::vec3(0, 0, 0)), max(trimesh::vec3(1, 1, 1)), size(trimesh::vec3(1, 1, 1)), gridlength(128)
{
}

inline Octree::~Octree(void){
}

inline size_t Octree::storeNode(Node n){
	nodes.push_back(n);
	return nodes.size()-1;
}

inline const Node* Octree::getNode(size_t index) const{
	return &nodes[index];
}

inline const Node* Octree::getRootNode() const{
	return &nodes.back();
}

inline Node* Octree::getNode(size_t index){
	return &nodes[index];
}

inline Node* Octree::getRootNode(){
	return &nodes.back();
}
