#include "structures.fxh"

#ifndef SHOW_STATISTICS
#define SHOW_STATISTICS 1
#endif

// Octree nodes
StructuredBuffer<DepthPrepassDrawTask> BestOccluders;

cbuffer cbConstants
{
    Constants g_Constants;
}

// Payload will be used in the mesh shader.
groupshared Payload s_Payload;

bool IsVisible(float4 basePosAndScale)
{
    float4 center = float4(basePosAndScale.xyz, 1.0f);
    float radius = 0.71f * abs(basePosAndScale.z * 0.5); // => diagonal (center-max point) = sqrt(2) * half_width / 2.0f | => 1/2 sqrt(2) * half_width
    
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

[numthreads(GROUP_SIZE, 1, 1)]
void main(in uint I  : SV_GroupIndex,
          in uint wg : SV_GroupID)
{
    // Reset the counter from the first thread in the group
    if (I == 0)
    {
        s_TaskCount = 0;
    }

    // Flush the cache and synchronize
    GroupMemoryBarrierWithGroupSync();

    // Read the first task arguments in order to get some constant data
    const uint gid = wg * GROUP_SIZE + I;
    
    // Get the node for this thread group
    DepthPrepassDrawTask node = BestOccluders[wg];
    
    // Access node indices for each thread 
    if ((g_Constants.FrustumCulling == 0 || IsVisible(node.BasePosAndScale)) // frustum culling
        && gid < node.BestOccluderCount)                                     // only draw valid occluders
    {
        DepthPrepassDrawTask task = BestOccluders[I];
        float3 pos = task.BasePosAndScale.xyz;
        float scale = task.BasePosAndScale.w * 0.5f;
    
        // Atomically increase task count
        uint index = 0;
        InterlockedAdd(s_TaskCount, 1, index);

        // Add mesh data to payload
        s_Payload.PosX[index] = pos.x;
        s_Payload.PosY[index] = pos.y;
        s_Payload.PosZ[index] = pos.z;
        s_Payload.Scale[index] = scale;
        s_Payload.MSRand[index] = 0.0f;
    }
    
    // All threads must complete their work so that we can read s_TaskCount
    GroupMemoryBarrierWithGroupSync();
    
    // This function must be called exactly once per amplification shader.
    // The DispatchMesh call implies a GroupMemoryBarrierWithGroupSync(), and ends the amplification shader group's execution.
    DispatchMesh(s_TaskCount, 1, 1, s_Payload);
}
