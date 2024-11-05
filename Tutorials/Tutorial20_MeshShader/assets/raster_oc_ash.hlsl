#include "structures.fxh"

cbuffer cbConstants
{
	Constants g_Constants;
}

StructuredBuffer<VoxelBufData> VoxelPositionBuffer : register(t0);

// Octree nodes
StructuredBuffer<OctreeLeafNode> OctreeNodes : register(t1);

// Occlusion results
RWByteAddressBuffer VisibilityBuffer : register(u0);

groupshared Payload s_Payload;
groupshared uint s_TaskCount;

[numthreads(GROUP_SIZE, 1, 1)]
void main(in uint I : SV_GroupIndex,
          in uint wg : SV_GroupID)
{
    if (I == 0)
    {
        s_TaskCount = 0;
    }
    
    // Flush the cache and synchronize
    GroupMemoryBarrierWithGroupSync();

    const uint gid = wg * GROUP_SIZE + I;    
    OctreeLeafNode node = OctreeNodes[wg];
    
    VisibilityBuffer.Store(4 * gid, 0); // Reset visibility buffer. @TODO: Maybe Store4 ?
    
    if (node.VoxelBufDataCount > 0 && I < node.VoxelBufDataCount)
    {
        VoxelBufData voxel = VoxelPositionBuffer[node.VoxelBufStartIndex + I];
        float3 pos = voxel.BasePosAndScale.xyz;
        float scale = voxel.BasePosAndScale.w;
        
        uint index = 0;
        InterlockedAdd(s_TaskCount, 1, index);
        
        // Add mesh data to payload
        s_Payload.PosX[index] = pos.x;
        s_Payload.PosY[index] = pos.y;
        s_Payload.PosZ[index] = pos.z;
        s_Payload.Scale[index] = scale;
        s_Payload.MSRand[index] = gid;
    }   
    
    
    GroupMemoryBarrierWithGroupSync();
    
    DispatchMesh(s_TaskCount, 1, 1, s_Payload);
}