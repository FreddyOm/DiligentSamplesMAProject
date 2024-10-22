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

bool IsInCameraFrustum(float4 basePosAndScale)
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

float LinearToNonLinearDepth(float linearDepth, float near, float far)
{
    float nonLinearDepth = (near * far) / (far - linearDepth * (far - near));
    return nonLinearDepth;
}

float NormalizedNonLinearDepth(float linearDepth, float near, float far)
{
    float nonLinearDepth = LinearToNonLinearDepth(linearDepth, near, far);
    return (nonLinearDepth - near) / (far - near);
}

// HiZ occlusion culling
bool IsVisible(float4 worldPosAndScale)
{
    float4 clipPos = mul(float4(worldPosAndScale.xyz, 1.0), g_Constants.ViewProjMat);
    float2 screenPos = clipPos.xy / clipPos.w * float2(0.5, -0.5) + 0.5;
    
    float linearDepth = clipPos.z / clipPos.w;
    
    // Adjust radius for depth
    float depthScale = abs(g_Constants.ViewProjMat[2][2]);
    float radiusInDepth = worldPosAndScale.w /** depthScale*/ / clipPos.w;
    
    float minDepth = max(0.0, linearDepth - radiusInDepth);
    float maxDepth = min(1.0, linearDepth + radiusInDepth);
    
    uint numLevels = 1; // At least one mip level is assumed
    uint outVar;
    HiZPyramid.GetDimensions(outVar, outVar, outVar, numLevels);
    
    for (uint mipLevel = numLevels - 3; mipLevel > 0; --mipLevel)
    {
        // Store the nearest / farthers 4 bounding vertices from the BB
        float4 boundingBoxVertices = float4(min(1,1), min(1,1), min(1,1), min(1,1)); 
        
        
        float2 mipSize;
        HiZPyramid.GetDimensions(mipLevel, mipSize.x, mipSize.y, numLevels);
        float2 uv = screenPos * mipSize;
        float hiZNonLinearDepth = HiZPyramid.SampleLevel(HiZPyramid_sampler, uv, mipLevel).r;   // compare bounding box

        if (maxDepth < hiZNonLinearDepth)
            return false; // Culled (object is behind the HiZ depth)

        if (minDepth > hiZNonLinearDepth)
            return true; // Visible (object is in front of the HiZ depth)
        
        //if (maxDepth < g_Constants.OCThreshold)
        //    return false; // Culled (object is behind the HiZ depth)

        //if (minDepth > g_Constants.OCThreshold)
        //    return true; // Visible (object is in front of the HiZ depth)
    }

    return true; // Visible if we've gone through all mip levels
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
    if ((g_Constants.FrustumCulling == 0 || IsInCameraFrustum(node.BasePosAndScale)) // frustum culling
        && I < node.VoxelBufDataCount //&& IsVisible(VoxelPositionBuffer[node.VoxelBufStartIndex + I].BasePosAndScale) // occlusion culling
        && (g_Constants.ShowOnlyBestOccluders == 0 || node.VoxelBufDataCount == GROUP_SIZE)) // only draw valid voxels
    {
        VoxelBufData voxel  = VoxelPositionBuffer[node.VoxelBufStartIndex + I];
        float3 pos          = voxel.BasePosAndScale.xyz;
        float scale         = voxel.BasePosAndScale.w;
        
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
