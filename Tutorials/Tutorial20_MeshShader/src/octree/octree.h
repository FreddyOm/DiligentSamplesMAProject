#pragma once

#include <DirectXMath.h>
#include <array>
#include <vector>
#include <set>
#include "../DrawTask.h"
#include "aabb.h"

extern std::vector<AABB> OTVoxelBoundBuffer;

bool          IntersectAABBAABB(const AABB& first, const AABB& second);
bool          IntersectAABBPoint(const AABB& first, const DirectX::XMFLOAT3& second);
extern AABB   GetVoxelBounds(size_t index);

template<typename T>
class OctreeNode
{
public:
    AABB                       bounds                = {};
    unsigned int               maxObjectsPerLeaf     = 0;
    bool                       isLeaf                = {};

    AABB   rootBounds = {};
    size_t gridSize = 0;

    std::vector<AABB>& nodeBuffer;
    std::array<OctreeNode*, 8> children              = {};
    std::vector<size_t>        objectIndices         = {};

    OctreeNode(const AABB& bounds, std::vector<AABB>& nodeBuffer, size_t gridSize, AABB rootBounds, unsigned int maxObjectsPerLeaf = 64) :
        bounds(bounds), maxObjectsPerLeaf(maxObjectsPerLeaf), isLeaf(true), nodeBuffer(nodeBuffer), gridSize(gridSize), rootBounds(rootBounds)
    {
        children.fill(nullptr);
        objectIndices.reserve(maxObjectsPerLeaf);

        VERIFY_EXPR(gridSize > 0);
    }

    ~OctreeNode()
    {
        objectIndices.clear();
        
        for(auto & child : children)
        {
            delete child;
        }
    }

    void QueryAllNodes(std::vector<VoxelOC::VoxelBufData>& orderedVoxelDataBuf, std::vector<VoxelOC::OctreeLeafNode>& octreeNodeBuffer)
    {
        // Check for children (Buttom Up)
        for (int i = 0; i < children.size(); ++i)
        {
            if (children[i] != nullptr)
            {                
                children[i]->QueryAllNodes(orderedVoxelDataBuf, octreeNodeBuffer);
            }
        }

        // Create octree node data for gpu
        VoxelOC::OctreeLeafNode ocNode;
        ocNode.VoxelBufStartIndex = static_cast<int>(orderedVoxelDataBuf.size());
        ocNode.VoxelBufIndexCount = static_cast<int>(objectIndices.size());
        ocNode.BasePosAndScale    = bounds.CenterAndScale();

        VERIFY_EXPR(ocNode.VoxelBufIndexCount <= ocNode.BasePosAndScale.w * ocNode.BasePosAndScale.w * ocNode.BasePosAndScale.w);

        if (objectIndices.size() > 0)       // Only insert nodes which actually store voxels. Makes it easier to iterate in depth pre-pass
            octreeNodeBuffer.push_back(std::move(ocNode));
        
        // Add own indices if this node has it's own indices
        for (int index = 0; index < objectIndices.size(); ++index)
        {
            VoxelOC::VoxelBufData voxelData;
            voxelData.BasePosAndScale = OTVoxelBoundBuffer[objectIndices[index]].CenterAndScale();

            VERIFY_EXPR(voxelData.BasePosAndScale.w == 1);

            orderedVoxelDataBuf.push_back(std::move(voxelData));
        }
    }

    void QueryBestOccluders(std::vector<VoxelOC::DepthPrepassDrawTask>& depthPrepassOTNodes)
    {
        // Fill depth prepass best occluders (Top Down)
        for (int i = 0; i < children.size(); ++i)
        {
            if (children[i] != nullptr)
            {
                if (children[i]->IsFull())
                {
                    VoxelOC::DepthPrepassDrawTask drawTask{};
                    drawTask.BasePositionAndScale = children[i]->bounds.CenterAndScale();
                    depthPrepassOTNodes.push_back(std::move(drawTask));
                    continue;
                }
                
                children[i]->QueryBestOccluders(depthPrepassOTNodes);
            }
        }
    }

    void SplitNode()
    {
        if (!isLeaf) return;

        VERIFY_EXPR(bounds.max.x - bounds.min.x >= 4);

        DirectX::XMFLOAT3 center = {
                (bounds.min.x + bounds.max.x) * 0.5f,
                (bounds.min.y + bounds.max.y) * 0.5f,
                (bounds.min.z + bounds.max.z) * 0.5f
            };

        for (int i = 0; i < 8; ++i)
        {
            DirectX::XMFLOAT3 newMin{};
            DirectX::XMFLOAT3 newMax{};

            newMin.x = (i & 1) ? center.x : bounds.min.x;
            newMin.y = (i & 2) ? center.y : bounds.min.y;
            newMin.z = (i & 4) ? center.z : bounds.min.z;
            newMax.x = (i & 1) ? bounds.max.x : center.x;
            newMax.y = (i & 2) ? bounds.max.y : center.y;
            newMax.z = (i & 4) ? bounds.max.z : center.z;

            children[i] = new OctreeNode({newMin, newMax}, nodeBuffer, gridSize, rootBounds, maxObjectsPerLeaf);
        }

        isLeaf = false;
    }

    void InsertObject(size_t objectIndex, const AABB objectBounds)
    {
        OctreeNode*              currentNode = this;
        std::vector<OctreeNode*> path;

        while (true)
        {
            if (!IntersectAABBPoint(bounds, objectBounds.Center()))
            {
                return;
            }

            path.push_back(currentNode);

            if (currentNode->isLeaf)
            {
                if (currentNode->objectIndices.size() < maxObjectsPerLeaf)
                {
                    currentNode->objectIndices.push_back(objectIndex);
                    return;
                }
                else
                {
                    VERIFY_EXPR(currentNode->bounds.CenterAndScale().w >= 4);
                    // Need to split this node
                    currentNode->SplitNode();

                    // Re-distribute existing objects
                    for (size_t index : currentNode->objectIndices)
                    {
                        AABB existingBounds = GetVoxelBounds(index);
                        for (auto& child : currentNode->children)
                        {
                            if (IntersectAABBPoint(child->bounds, existingBounds.Center()))
                            {
                                child->objectIndices.push_back(index);
                            }
                        }
                    }
                    currentNode->objectIndices.clear();
                    // Continue to insert the new object
                }
            }

            // Find the appropriate child
            bool foundChild = false;

            for (auto& child : currentNode->children)
            {
                if (IntersectAABBPoint(child->bounds, objectBounds.Center()))
                {
                    currentNode = child;
                    foundChild  = true;
                    break;                      // Breaking here is fatal when checking for intersection with an AABB!
                }
            }

            if (!foundChild)
            {
                // Object doesn't fit in any child, insert it here
                currentNode->objectIndices.push_back(objectIndex);
                return;
            }
        }
    }

    float GetVoxelSize() const
    {
        VERIFY_EXPR(rootBounds.max.x - rootBounds.max.y < 0.1f && rootBounds.max.y - rootBounds.max.z < 0.1f);
        VERIFY_EXPR(rootBounds.min.x - rootBounds.min.y < 0.1f && rootBounds.min.y - rootBounds.min.z < 0.1f);

        return (rootBounds.max.x - rootBounds.min.x) / (float)(gridSize * 2.0f);
    }

    /*
     
        For MaxObjectsPerLeaf = 4 :
     
        Tight and Full:                 Not Tight but Full:
        --------------                  -----------------------------
        | ----  ---- |                  | ----  ----                |
        | |  |  |  | |                  | |  |  |  |                |
        | ----  ---- |                  | ----  ----                |
        | ----  ---- |                  | ----  ----                |
        | |  |  |  | |                  | |  |  |  |                |
        | ----  ---- |                  | ----  ----                |
        --------------                  |                           |
                                        |                           |
                                        |                           |
                                        |                           |
                                        -----------------------------
    */

    // A tight node is a leaf node which is not bigger than the boundaries of the individual voxels summed! 
    bool IsTight()
    {
        double exp = 1.0 / 3.0;
        double base = maxObjectsPerLeaf;

        float boundDimension = (bounds.max.x - bounds.min.x);
        double overfullVoxelDimension = std::ceil((GetVoxelSize() * 2) * pow(base, exp));

        return boundDimension <= overfullVoxelDimension;
    }

    bool IsFull()
    {
        // Node is either full when voxel count is maximum voxel count and node is tight
        if (objectIndices.size() >= maxObjectsPerLeaf && IsTight())
            return true;

        // Or if all children are full
        for (auto* child : children)
        {
            if (child == nullptr || !child->IsFull())
                return false;
        }
        
        return true;
    }
};
