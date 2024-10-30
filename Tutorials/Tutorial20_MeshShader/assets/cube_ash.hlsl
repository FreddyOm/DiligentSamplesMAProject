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

// Get themminium Z value, the minimum bound vertex poitions, and the perspectiveDivide for the given Z value
float GetMinBoundVertex(float4 BasePosAndScale, out float4 minXmaxXminYmaxY)
{
    float3 basePos = BasePosAndScale.xyz;
    float scale = BasePosAndScale.w * 0.5;
    
    // Create all corner positions at once using vector operations
    float4x3 corners1 = float4x3(
        basePos + float3(-scale, -scale, -scale),
        basePos + float3(-scale, -scale, scale),
        basePos + float3(-scale, scale, -scale),
        basePos + float3(-scale, scale, scale)
    );
    
    float4x3 corners2 = float4x3(
        basePos + float3(scale, -scale, -scale),
        basePos + float3(scale, -scale, scale),
        basePos + float3(scale, scale, -scale),
        basePos + float3(scale, scale, scale)
    );
    
    // Transform first batch
    float4 clipPos1 = mul(float4(corners1[0].xyz, 1.0), g_Constants.ViewProjMat);
    float4 clipPos2 = mul(float4(corners1[1].xyz, 1.0), g_Constants.ViewProjMat);
    float4 clipPos3 = mul(float4(corners1[2].xyz, 1.0), g_Constants.ViewProjMat);
    float4 clipPos4 = mul(float4(corners1[3].xyz, 1.0), g_Constants.ViewProjMat);
        
    clipPos1 /= clipPos1.w; clipPos1 = saturate(clipPos1);
    clipPos2 /= clipPos2.w; clipPos2 = saturate(clipPos2);
    clipPos3 /= clipPos3.w; clipPos3 = saturate(clipPos3);
    clipPos4 /= clipPos4.w; clipPos4 = saturate(clipPos4);
    
    // Initialize min/max values with first batch
    minXmaxXminYmaxY.x = min(min(clipPos1.x, clipPos2.x), min(clipPos3.x, clipPos4.x));
    minXmaxXminYmaxY.y = max(max(clipPos1.x, clipPos2.x), max(clipPos3.x, clipPos4.x));
    minXmaxXminYmaxY.z = min(min(clipPos1.y, clipPos2.y), min(clipPos3.y, clipPos4.y));
    minXmaxXminYmaxY.w = max(max(clipPos1.y, clipPos2.y), max(clipPos3.y, clipPos4.y));
    
    float minZ = min(min(clipPos1.z, clipPos2.z), min(clipPos3.z, clipPos4.z));
    
    // Transform second batch
    clipPos1 = mul(float4(corners2[0].xyz, 1.0), g_Constants.ViewProjMat);
    clipPos2 = mul(float4(corners2[1].xyz, 1.0), g_Constants.ViewProjMat);
    clipPos3 = mul(float4(corners2[2].xyz, 1.0), g_Constants.ViewProjMat);
    clipPos4 = mul(float4(corners2[3].xyz, 1.0), g_Constants.ViewProjMat);
    
    clipPos1 /= clipPos1.w; clipPos1 = saturate(clipPos1);
    clipPos2 /= clipPos2.w; clipPos2 = saturate(clipPos2);
    clipPos3 /= clipPos3.w; clipPos3 = saturate(clipPos3);
    clipPos4 /= clipPos4.w; clipPos4 = saturate(clipPos4);
    
    // Update min/max values with second batch
    minXmaxXminYmaxY.x = min(min(min(clipPos1.x, clipPos2.x), min(clipPos3.x, clipPos4.x)), minXmaxXminYmaxY.x);
    minXmaxXminYmaxY.y = max(max(max(clipPos1.x, clipPos2.x), max(clipPos3.x, clipPos4.x)), minXmaxXminYmaxY.y);
    minXmaxXminYmaxY.z = min(min(min(clipPos1.y, clipPos2.y), min(clipPos3.y, clipPos4.y)), minXmaxXminYmaxY.z);
    minXmaxXminYmaxY.w = max(max(max(clipPos1.y, clipPos2.y), max(clipPos3.y, clipPos4.y)), minXmaxXminYmaxY.w);
    
    float minZ2 = min(min(min(clipPos1.z, clipPos2.z), min(clipPos3.z, clipPos4.z)), minZ);
    
    return min(minZ, minZ2);
}

// HiZ occlusion culling in linear ndc space
bool IsVisible(OctreeLeafNode node, uint I, out float HiZDepthVal, out float MinZ, out uint mipCount, out float4 dF41, out float4 dF42)
{    
    if (node.VoxelBufDataCount == 0)    // empty nodes are ignored (can occur due to draw task alignment)
        return false;
    
    float4 worldPosAndScale = VoxelPositionBuffer[node.VoxelBufStartIndex + I].BasePosAndScale;
    
    // Calculate min Z value of the transformed bounding box
    float4 clipPosVertices   = float4(0.f, 0.f, 0.f, 0.f); // also get min and max x- and y-values for screen-space box to check 
    float  perspectiveDivide = 0.f;
    float  minZ              = GetMinBoundVertex(worldPosAndScale, clipPosVertices);
    
    // Keep in mind, that maxY will be minY and the other way around, since clip space Y begins at the lower end of the screen,
    // and screen space begins at the upper end of the screen
    float2 upperLeftBounding    = float2(clipPosVertices.x, clipPosVertices.w);     // minX maxY
    float2 upperRightBounding   = float2(clipPosVertices.y, clipPosVertices.w);     // maxX maxY
    float2 lowerLeftBounding    = float2(clipPosVertices.x, clipPosVertices.z);     // minX minY
    float2 lowerRightBounding   = float2(clipPosVertices.y, clipPosVertices.z);     // maxX minY
    
    // Now I have four ndc positions which are the corner positions of my minZ-rect
    
    // Convert NDC coordinates to UV space [0,1]
    float2 upperLeftBoundingUV  = float2(upperLeftBounding.x  * 0.5 + 0.5, upperLeftBounding.y  * -0.5 + 0.5);
    float2 upperRightBoundingUV = float2(upperRightBounding.x * 0.5 + 0.5, upperRightBounding.y * -0.5 + 0.5);
    float2 lowerLeftBoundingUV  = float2(lowerLeftBounding.x  * 0.5 + 0.5, lowerLeftBounding.y  * -0.5 + 0.5);
    float2 lowerRightBoundingUV = float2(lowerRightBounding.x * 0.5 + 0.5, lowerRightBounding.y * -0.5 + 0.5);
    
    uint numLevels = 1; // At least one mip level is assumed
    uint outVar;
    HiZPyramid.GetDimensions(outVar, outVar, outVar, numLevels);
    mipCount = numLevels;
    
    for (uint mipLevel = numLevels - 5; mipLevel > 0; --mipLevel)
    {
        float hiZDepthUL = 1.0f - HiZPyramid.SampleLevel(HiZPyramid_sampler, upperLeftBoundingUV, mipLevel).r; // compare bounding box
        float hiZDepthUR = 1.0f - HiZPyramid.SampleLevel(HiZPyramid_sampler, upperRightBoundingUV, mipLevel).r;
        float hiZDepthLL = 1.0f - HiZPyramid.SampleLevel(HiZPyramid_sampler, lowerLeftBoundingUV, mipLevel).r;
        float hiZDepthLR = 1.0f - HiZPyramid.SampleLevel(HiZPyramid_sampler, lowerRightBoundingUV, mipLevel).r;
        
        if (max(max(hiZDepthLL, hiZDepthLR), max(hiZDepthUL, hiZDepthUR)) < minZ)    // No bounding box z value was lower than z-pyramids z value -> fully occluded
            return false;       // Return: is not visible
    }

    if (I == 0)
    {
        MinZ = minZ;
        dF41.xy = upperLeftBoundingUV;
        dF41.zw = upperRightBoundingUV;
    }
    
    return true; // All mip levels have been traversed and the bounding box has not been found to be occluded
}

// The number of cubes that are visible by the camera,
// computed by every thread group
groupshared uint s_TaskCount;
groupshared uint s_OctreeNodeCount;
groupshared float s_HZD;
groupshared float s_MinZ;
groupshared float4 s_DebugFloat4_1;
groupshared float4 s_DebugFloat4_2;
groupshared uint s_MipCount;

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
        s_HZD = 0.f;
        s_MinZ = 0.f;
        s_DebugFloat4_1 = float4(-1, -1, -1, -1);
        s_DebugFloat4_2 = float4(-1, -1, -1, -1);
        s_MipCount = 0;
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
    
    float HZD            = -1;
    float MinZ           = -1;
    uint MipCount        = -1;
    float4 DebugFloat4_1 = float4(-1, -1, -1, -1);
    float4 DebugFloat4_2 = float4(-1, -1, -1, -1);
    
    uint cullVoxel = 0;
    cullVoxel += node.VoxelBufDataCount > 0 ? 0 : 1;
    cullVoxel += I < node.VoxelBufDataCount ? 0 : 1;
    cullVoxel += (g_Constants.FrustumCulling == 0 || IsInCameraFrustum(node.BasePosAndScale)) ? 0 : 1;
    cullVoxel += IsVisible(node, I, HZD, MinZ, MipCount, DebugFloat4_1, DebugFloat4_2) ? 0 : 1;
    
    if (cullVoxel == 0) // only draw valid voxels
    {
        VoxelBufData voxel  = VoxelPositionBuffer[node.VoxelBufStartIndex + I];
        float3       pos    = voxel.BasePosAndScale.xyz;
        float        scale  = voxel.BasePosAndScale.w;
        
        DebugFloat4_2.xyz = pos;
        DebugFloat4_2.w = scale;
        
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
        
        if (node.VoxelBufDataCount > 0 && I == 0)
        {
            GroupMemoryBarrier();
            uint temp;
            InterlockedAdd(s_OctreeNodeCount, 1, temp);
            
            InterlockedExchange(s_HZD, HZD, temp);
            InterlockedExchange(s_MinZ, MinZ, temp);
            InterlockedExchange(s_MipCount, MipCount, temp);
            
            InterlockedExchange(s_DebugFloat4_1.x, DebugFloat4_1.x, temp);
            InterlockedExchange(s_DebugFloat4_1.y, DebugFloat4_1.y, temp);
            InterlockedExchange(s_DebugFloat4_1.z, DebugFloat4_1.z, temp);
            InterlockedExchange(s_DebugFloat4_1.w, DebugFloat4_1.w, temp);
            
            InterlockedExchange(s_DebugFloat4_2.x, DebugFloat4_2.x, temp);
            InterlockedExchange(s_DebugFloat4_2.y, DebugFloat4_2.y, temp);
            InterlockedExchange(s_DebugFloat4_2.z, DebugFloat4_2.z, temp);
            InterlockedExchange(s_DebugFloat4_2.w, DebugFloat4_2.w, temp);
        }
#endif
    }
    
    // All threads must complete their work so that we can read s_TaskCount
    GroupMemoryBarrierWithGroupSync();

    if (node.VoxelBufDataCount > 0 && I == 0)
    {
        // Update statistics from the first thread
        uint orig_value_task_count;
        Statistics.InterlockedAdd(0, s_TaskCount, orig_value_task_count);
        
#if SHOW_STATISTICS
  
        uint orig_value_ocn_count;
        Statistics.InterlockedAdd(4, s_OctreeNodeCount, orig_value_ocn_count);
        
        Statistics.Store(8, s_HZD);
        Statistics.Store(12, s_MinZ);
        
        Statistics.Store(16, s_DebugFloat4_1.x);
        Statistics.Store(20, s_DebugFloat4_1.y);
        Statistics.Store(24, s_DebugFloat4_1.z);
        Statistics.Store(28, s_DebugFloat4_1.w);
        
        Statistics.Store(32, s_DebugFloat4_2.x);
        Statistics.Store(36, s_DebugFloat4_2.y);
        Statistics.Store(40, s_DebugFloat4_2.z);
        Statistics.Store(44, s_DebugFloat4_2.w);
        
        Statistics.Store(48, s_MipCount);
#endif
    }
    
    // This function must be called exactly once per amplification shader.
    // The DispatchMesh call implies a GroupMemoryBarrierWithGroupSync(), and ends the amplification shader group's execution.
    DispatchMesh(s_TaskCount, 1, 1, s_Payload);
}
