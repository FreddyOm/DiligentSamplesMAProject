#pragma once

#include <vector>
#include <stack>
#include <DirectXMath.h>
#include "aabb.h"
#include "../DrawTask.h"
#include "octree.h"
#include "../svo_builder/Octree.h"

#include <mutex>
#include <thread>

struct StackItem
{
    Node* node;
    AABB  bounds;
    StackItem(Node* n, const AABB& b) :
        node(n), bounds(b) {}
};

template <typename T>
class OctreeConverter
{
private:
    Octree*            sourceOctree;
    AABB               rootBounds;
    std::vector<Vec4>& voxelPosBuffer;
    OctreeNode<T>*     destOctreeRoot;
    std::stack<StackItem>* pNodeStack = nullptr;

    void ConvertTreeIterative()
    {
        pNodeStack->push(StackItem(sourceOctree->getRootNode(), rootBounds));

        while (!pNodeStack->empty())
        {
            StackItem current = pNodeStack->top();
            pNodeStack->pop();

            Node* sourceNode = current.node;
            AABB  bounds     = current.bounds;

            if (sourceNode->isNull())
            {
                continue;
            }

            if (sourceNode->isLeaf())
            {
                DirectX::XMFLOAT3 voxelCenter = bounds.Center();
                
                // Insert into voxel position buffer
                voxelPosBuffer.emplace_back(voxelCenter.x, voxelCenter.y, voxelCenter.z, GetVoxelSize());
                
                // Insert into node buffer (used for boundary calculations later on)
                VoxelOC::OctreeLeafNode task{};

                task.BasePosAndScale.x = voxelCenter.x;
                task.BasePosAndScale.y = voxelCenter.y;
                task.BasePosAndScale.z = voxelCenter.z;
                task.BasePosAndScale.w = GetVoxelSize();

                OTVoxelBoundBuffer.push_back(std::move(task));

                // Insert into octree
                size_t index = voxelPosBuffer.size() - 1;
                destOctreeRoot->InsertObject(index, bounds);
            }
            else
            {
                for (int i = 7; i >= 0; --i)
                {
                    if (sourceNode->hasChild(i))
                    {
                        Node* childNode   = &sourceOctree->nodes.at(sourceNode->getChildPos(i));
                        AABB  childBounds = bounds.Octant(i);
                        pNodeStack->push(StackItem(childNode, childBounds));
                    }
                }
            }
        }
    }

public:
    OctreeConverter(Octree* source, const AABB& bounds, std::vector<Vec4>& posBuffer, OctreeNode<T>* destRoot) :
        sourceOctree(source), rootBounds(bounds), voxelPosBuffer(posBuffer), destOctreeRoot(destRoot), pNodeStack(new std::stack<StackItem>()) {}

    ~OctreeConverter()
    {
        delete pNodeStack;
    }

    void Convert()
    {
        ConvertTreeIterative();
    }

    float GetVoxelSize() const
    {
        VERIFY_EXPR(sourceOctree->gridlength > 0);
        VERIFY_EXPR(rootBounds.max.x - rootBounds.max.y < 0.1f && rootBounds.max.y - rootBounds.max.z < 0.1f);
        VERIFY_EXPR(rootBounds.min.x - rootBounds.min.y < 0.1f && rootBounds.min.y - rootBounds.min.z < 0.1f);

        return (rootBounds.max.x - rootBounds.min.x) / (float)(sourceOctree->gridlength * 2.0f) ;
    }
};