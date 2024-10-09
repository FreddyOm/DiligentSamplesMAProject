#pragma once

#include <DirectXMath.h>
#include <BasicMath.hpp>

namespace VoxelOC
{
    // GPU octree data
    struct OctreeLeafNode     // OctreeLeafNode
    {
        DirectX::XMFLOAT4 BasePosAndScale;      // To compute node bounding box for frustum culling
        DirectX::XMFLOAT4 RandomValue;          // Remove later

        // Payload
        int VoxelBufStartIndex;
        int VoxelBufIndexCount;

        int Padding[2];

        bool operator==(OctreeLeafNode& other)
        {
            return BasePosAndScale.x == other.BasePosAndScale.x
                && BasePosAndScale.y == other.BasePosAndScale.y
                && BasePosAndScale.z == other.BasePosAndScale.z;
        }
    };

    // Draw task for depth pre pass
    struct DepthPrepassDrawTask
    {
        DirectX::XMFLOAT4 BasePositionAndScale; // [x, y, z, scale]
        int               BestOccluderCount;
        int               Padding[3];

        DepthPrepassDrawTask() = default;

        DepthPrepassDrawTask(DepthPrepassDrawTask&& other) noexcept
            :
            BasePositionAndScale(other.BasePositionAndScale),
            BestOccluderCount(other.BestOccluderCount)
        { }
    };

    // Global voxel position data
    struct VoxelBufData
    {
        DirectX::XMFLOAT4 BasePosAndScale; // [ x, y, z, scale ]
    };
}

struct Vec4
{
    float x, y, z, w;
    
    Vec4() = default;

    Vec4(Diligent::float4& other)
    {
        x = other.x;
        y = other.y;
        z = other.z;
        w = other.w;
    }

    bool operator<(const Vec4& other) const
    {
        if (x != other.x)
            return x < other.x;

        if (y != other.y)
            return y < other.y;

        if (z != other.z)
            return z < other.z;


        return false;
    }

    bool operator==(const Vec4& other) const
    {
        return x == other.x && y == other.y && z == other.z && w == other.w;
    }
};
