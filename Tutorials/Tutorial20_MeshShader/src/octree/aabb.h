#pragma once

struct AABB
{
    DirectX::XMFLOAT3 min = {};
    DirectX::XMFLOAT3 max = {};

    DirectX::XMFLOAT3 Center() const
    {
        return {(min.x + max.x) / 2.f, (min.y + max.y) / 2.f, (min.z + max.z) / 2.f};
    }

    DirectX::XMFLOAT4 CenterAndScale() const
    {
        // Center and scale     x                 y                   z                scale
        return {(min.x + max.x) / 2.f, (min.y + max.y) / 2.f, (min.z + max.z) / 2.f, max.x - min.x};
    }

    // Get bounds of one of eight split AABBs which occupy this AABB
    AABB Octant(unsigned int octant) const
    {
        DirectX::XMFLOAT3 center = Center();
        DirectX::XMFLOAT3 newMin, newMax;
        newMin.x = (octant & 1) ? center.x : min.x;
        newMin.y = (octant & 2) ? center.y : min.y;
        newMin.z = (octant & 4) ? center.z : min.z;
        newMax.x = (octant & 1) ? max.x : center.x;
        newMax.y = (octant & 2) ? max.y : center.y;
        newMax.z = (octant & 4) ? max.z : center.z;
        return {newMin, newMax};
    }
};