#include "structures.fxh"

#ifndef SHOW_STATISTICS
#define SHOW_STATISTICS 1
#endif

// Ordered voxel position buffer
StructuredBuffer<VoxelBufData> VoxelPositionBuffer;

// Octree nodes
StructuredBuffer<OctreeLeafNode> OctreeNodes;

cbuffer cbConstants
{
    Constants g_Constants;
}

// Statistics buffer contains the global counter of visible objects
RWByteAddressBuffer Statistics;

// Payload will be used in the mesh shader.
groupshared Payload s_Payload;


bool IsVisible(float4 basePosAndScale)
{
    float4 center = float4(basePosAndScale.xyz, 1.0f);
    float radius = 0.71f * abs(basePosAndScale.z); // => diagonal (center-max point) = sqrt(2) * width / 2.0f | => 1/2 sqrt(2) * width
    
    for (int i = 0; i < 6; ++i)
    {
        if (dot(g_Constants.Frustum[i], center) < -radius)
            return false;
    }
    return true;
}


// The number of cubes that are visible by the camera,
// computed by every thread group
groupshared uint s_TaskCount;
groupshared uint s_OctreeNodeCount;

[numthreads(GROUP_SIZE, 1, 1)]
void main(in uint I  : SV_GroupIndex,
          in uint wg : SV_GroupID)
{
    // Reset the counter from the first thread in the group
    if (I == 0)
    {
        s_TaskCount = 0;
#if SHOW_STATISTICS
        s_OctreeNodeCount = 0;
#endif
    }

    // Flush the cache and synchronize
    GroupMemoryBarrierWithGroupSync();

    // Read the first task arguments in order to get some constant data
    const uint gid = wg * GROUP_SIZE + I;   
    
    // Get the node for this thread group
    OctreeLeafNode node = OctreeNodes[wg];
    
    float meshletColorRndValue = node.RandomValue.x;
    int taskCount = (int) node.RandomValue.y;
    int padding = (int) node.RandomValue.z;
    
    
    // Access node indices for each thread 
    if ((g_Constants.FrustumCulling == 0 || IsVisible(node.BasePosAndScale)) // frustum culling
        && I < node.VoxelBufDataCount)                                       // only draw valid voxels
    {
        VoxelBufData voxel  = VoxelPositionBuffer[node.VoxelBufStartIndex + I];
        float3 pos          = voxel.BasePosAndScale.xyz;
        float scale         = voxel.BasePosAndScale.w;   // Voxel size is stored as width, not as extension in either direction of the axis
    
        // Atomically increase task count
        uint index = 0;
        InterlockedAdd(s_TaskCount, 1, index);

        // Add mesh data to payload
        s_Payload.PosX[index] = pos.x;
        s_Payload.PosY[index] = pos.y;
        s_Payload.PosZ[index] = pos.z;
        s_Payload.Scale[index] = scale;
        s_Payload.MSRand[index] = meshletColorRndValue;
        
#if SHOW_STATISTICS
        
        if (I == 0)
        {
            uint temp = 0;
            InterlockedAdd(s_OctreeNodeCount, 1, temp);
        }
#endif
    }
    
    // All threads must complete their work so that we can read s_TaskCount
    GroupMemoryBarrierWithGroupSync();

    if (I == 0)
    {
        // Update statistics from the first thread
        uint orig_value_task_count;
        Statistics.InterlockedAdd(0, s_TaskCount, orig_value_task_count);
#if SHOW_STATISTICS
        uint orig_value_ocn_count;
        Statistics.InterlockedAdd(4, s_OctreeNodeCount, orig_value_ocn_count);
#endif
    }
    
    // This function must be called exactly once per amplification shader.
    // The DispatchMesh call implies a GroupMemoryBarrierWithGroupSync(), and ends the amplification shader group's execution.
    DispatchMesh(s_TaskCount, 1, 1, s_Payload);
}
