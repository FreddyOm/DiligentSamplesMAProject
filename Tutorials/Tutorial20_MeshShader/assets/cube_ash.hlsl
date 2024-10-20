#include "structures.fxh"

#ifndef SHOW_STATISTICS
#define SHOW_STATISTICS 1
#endif

Texture2D<float> HiZPyramid : register(t0);
SamplerState HiZPyramid_sampler;

// Statistics buffer contains the global counter of visible objects
RWByteAddressBuffer Statistics : register(u0);

// Ordered voxel position buffer
StructuredBuffer<VoxelBufData> VoxelPositionBuffer : register(t1);

// Octree nodes
StructuredBuffer<OctreeLeafNode> OctreeNodes : register(t2);


cbuffer cbConstants : register(b0)
{
    Constants g_Constants;
}


// Payload will be used in the mesh shader.
groupshared Payload s_Payload;

bool IsVisible(float4 basePosAndScale)
{
    float4 center = float4(basePosAndScale.xyz, 1.0f);
    float radius = 0.71f * abs(basePosAndScale.z);   // => diagonal (center-max point) = sqrt(2) * half_width / 4.0f | => 1/2 sqrt(2) * half_width
    
    for (int i = 0; i < 6; ++i)
    {
        if (dot(g_Constants.Frustum[i], center) < -radius)
            return false;
    }
    return true;
}

// HiZ occlusion culling
bool IsOccluded(float3 worldPos, float radius)
{
    float4 clipPos = mul(float4(worldPos, 1.0), g_Constants.ViewProjMat);
    float2 screenPos = clipPos.xy / clipPos.w * float2(0.5, -0.5) + 0.5;
    
    float depth = clipPos.z / clipPos.w;
    float minDepth = depth - radius;
    float maxDepth = depth + radius;
    
    uint numLevels = 1; // At least one mip level is assumed
    //HiZPyramid.GetDimensions(0, 0, 0, numLevels);

    for (uint mipLevel = 0; mipLevel < numLevels; ++mipLevel)
    {
        float2 mipSize;
        HiZPyramid.GetDimensions(mipLevel, mipSize.x, mipSize.y, numLevels);
        float2 uv = screenPos * mipSize;
        float hiZDepth = HiZPyramid.SampleLevel(HiZPyramid_sampler, uv, mipLevel).r;

        if (maxDepth < hiZDepth)
            return true; // Culled

        if (minDepth > hiZDepth)
            return false; // Visible
    }

    return false; // Visible if we've gone through all mip levels
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
    
    
        //&& !IsOccluded(VoxelPositionBuffer[node.VoxelBufStartIndex + I].BasePosAndScale.xyz, VoxelPositionBuffer[node.VoxelBufStartIndex + I].BasePosAndScale.w * 1.41)
    // Access node indices for each thread 
    if ((g_Constants.FrustumCulling == 0 || IsVisible(node.BasePosAndScale)) // frustum culling
        && I < node.VoxelBufDataCount && (g_Constants.ShowOnlyBestOccluders == 0 || node.VoxelBufDataCount == 64)) // only draw valid voxels
    {
        VoxelBufData voxel  = VoxelPositionBuffer[node.VoxelBufStartIndex + I];
        float3 pos          = voxel.BasePosAndScale.xyz;
        float scale = voxel.BasePosAndScale.w;
        
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
