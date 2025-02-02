#include "structures.fxh"

#ifndef SHOW_STATISTICS
#define SHOW_STATISTICS 1
#endif

// Statistics buffer contains the global counter of visible objects
RWByteAddressBuffer Statistics : register(u0);

// Ordered voxel position buffer
StructuredBuffer<VoxelBufData> VoxelPositionBuffer : register(t1);

// Octree nodes
StructuredBuffer<OctreeLeafNode> OctreeNodes : register(t2);

Texture2D<float> HiZPyramid : register(s0);

cbuffer cbConstants : register(b0)
{
    Constants g_Constants;
}

// Payload will be used in the mesh shader.
groupshared Payload s_Payload;

bool GetRenderOption(uint bit)
{
    return (g_Constants.RenderOptions & (1u << bit)) ?  true : false;
}

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
    float halfScale = BasePosAndScale.w * 0.5;
    
    // Create all corner positions at once using vector operations
    float4x3 corners1 = float4x3(
        basePos + float3(-halfScale, -halfScale, -halfScale),
        basePos + float3(-halfScale, -halfScale, halfScale),
        basePos + float3(-halfScale, halfScale, -halfScale),
        basePos + float3(-halfScale, halfScale, halfScale)
    );
    
    float4x3 corners2 = float4x3(
        basePos + float3(halfScale, -halfScale, -halfScale),
        basePos + float3(halfScale, -halfScale, halfScale),
        basePos + float3(halfScale, halfScale, -halfScale),
        basePos + float3(halfScale, halfScale, halfScale)
    );
    
    // Transform first batch
    float4 clipPos1 = mul(float4(corners1[0].xyz, 1.0), g_Constants.ViewProjMat);
    float4 clipPos2 = mul(float4(corners1[1].xyz, 1.0), g_Constants.ViewProjMat);
    float4 clipPos3 = mul(float4(corners1[2].xyz, 1.0), g_Constants.ViewProjMat);
    float4 clipPos4 = mul(float4(corners1[3].xyz, 1.0), g_Constants.ViewProjMat);
        
    clipPos1 /= clipPos1.w; clipPos1 = clamp(clipPos1, -1, 1);
    clipPos2 /= clipPos2.w; clipPos2 = clamp(clipPos2, -1, 1);
    clipPos3 /= clipPos3.w; clipPos3 = clamp(clipPos3, -1, 1);
    clipPos4 /= clipPos4.w; clipPos4 = clamp(clipPos4, -1, 1);
    
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
    
    clipPos1 /= clipPos1.w; clipPos1 = clamp(clipPos1, -1, 1);
    clipPos2 /= clipPos2.w; clipPos2 = clamp(clipPos2, -1, 1);
    clipPos3 /= clipPos3.w; clipPos3 = clamp(clipPos3, -1, 1);
    clipPos4 /= clipPos4.w; clipPos4 = clamp(clipPos4, -1, 1);
    
    // Update min/max values with second batch
    minXmaxXminYmaxY.x = min(min(min(clipPos1.x, clipPos2.x), min(clipPos3.x, clipPos4.x)), minXmaxXminYmaxY.x);
    minXmaxXminYmaxY.y = max(max(max(clipPos1.x, clipPos2.x), max(clipPos3.x, clipPos4.x)), minXmaxXminYmaxY.y);
    minXmaxXminYmaxY.z = min(min(min(clipPos1.y, clipPos2.y), min(clipPos3.y, clipPos4.y)), minXmaxXminYmaxY.z);
    minXmaxXminYmaxY.w = max(max(max(clipPos1.y, clipPos2.y), max(clipPos3.y, clipPos4.y)), minXmaxXminYmaxY.w);
    
    float minZ2 = min(min(clipPos1.z, clipPos2.z), min(clipPos3.z, clipPos4.z));
    
    return saturate(min(minZ, minZ2));
}

// HiZ occlusion culling in linear ndc space
bool IsVisible(OctreeLeafNode node, uint I)
{    
    if (node.VoxelBufDataCount == 0)    // empty nodes are ignored (can occur due to draw task alignment)
        return false;
    
    //                                                      Meshlet                                                         Octree Node
    float4 worldPosAndScale = GetRenderOption(0) ? VoxelPositionBuffer[node.VoxelBufStartIndex + I].BasePosAndScale : node.BasePosAndScale;
    //float4 worldPosAndScale = VoxelPositionBuffer[node.VoxelBufStartIndex + I].BasePosAndScale;
    
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
    float2 upperLeftBoundingUV = float2(upperLeftBounding.x * 0.5 + 0.5, upperLeftBounding.y * -0.5 + 0.5);
    float2 upperRightBoundingUV = float2(upperRightBounding.x * 0.5 + 0.5, upperRightBounding.y * -0.5 + 0.5);
    float2 lowerLeftBoundingUV = float2(lowerLeftBounding.x * 0.5 + 0.5, lowerLeftBounding.y * -0.5 + 0.5);
    float2 lowerRightBoundingUV = float2(lowerRightBounding.x * 0.5 + 0.5, lowerRightBounding.y * -0.5 + 0.5);
    
    
    uint numLevels = 1; // At least one mip level is assumed
    uint width = 0;
    uint height = 0;
    
    uint outVar;
    HiZPyramid.GetDimensions(0, width, height, numLevels);
  
    float2 boxSizeUV = float2(
        max(upperRightBoundingUV.x, lowerRightBoundingUV.x) - min(upperLeftBoundingUV.x, lowerLeftBoundingUV.x),
        max(upperLeftBoundingUV.y, upperRightBoundingUV.y) - min(lowerLeftBoundingUV.y, lowerRightBoundingUV.y)) * 
        float2(width, height);
    
    float boxPixelArea = boxSizeUV.x * boxSizeUV.y;
    float idealMipLevel = log2(sqrt(boxPixelArea));
    
    uint targetMipLevel = uint(min(max(round(idealMipLevel), 0), numLevels - 1));
    
    uint2 texDims;
    HiZPyramid.GetDimensions(targetMipLevel, texDims.x, texDims.y, outVar);
    
    float hiZDepthUL = HiZPyramid.Load(int3(uint2(upperLeftBoundingUV * texDims), targetMipLevel));
    float hiZDepthUR = HiZPyramid.Load(int3(uint2(upperRightBoundingUV * texDims), targetMipLevel));
    float hiZDepthLL = HiZPyramid.Load(int3(uint2(lowerLeftBoundingUV * texDims), targetMipLevel));
    float hiZDepthLR = HiZPyramid.Load(int3(uint2(lowerRightBoundingUV * texDims), targetMipLevel));
    
    float maxHiZDepth = max(max(hiZDepthLL, hiZDepthLR), max(hiZDepthUL, hiZDepthUR));

    // No bounding box z value was lower (closer) than z-pyramids z value -> fully occluded
    // Only if the difference between min/max is more than a given threshold should it be considered occluded! Otherwise z fighting!
    return !(maxHiZDepth < minZ && abs(maxHiZDepth - minZ) > 0.0000001f);
}

// The number of cubes that are visible by the camera,
// computed by every thread group
groupshared uint s_TaskCount;
groupshared uint s_OctreeNodeCount;

[numthreads(GROUP_SIZE, 1, 1)]
void main(in uint I  : SV_GroupIndex,
          in uint wg : SV_GroupID)
{
#if SHOW_STATISTICS
    // Reset the counter from the first thread in the group
    if (I == 0)
    {
        s_TaskCount = 0;
        s_OctreeNodeCount = 0;
    }
#endif

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
    uint cullVoxel = 0;
    cullVoxel += !(node.VoxelBufDataCount > 0);
    cullVoxel += !(I < node.VoxelBufDataCount);
    cullVoxel += (GetRenderOption(2) == true && IsInCameraFrustum(node.BasePosAndScale)) ? 0 : 1;
    cullVoxel += (GetRenderOption(1) == false || IsVisible(node, I)) ? 0 : 1;
    //cullVoxel += node.VoxelBufDataCount == GROUP_
    
    if (cullVoxel == 0) // only draw valid voxels
    {
        VoxelBufData voxel  = VoxelPositionBuffer[node.VoxelBufStartIndex + I];
        float3       pos    = voxel.BasePosAndScale.xyz;
        float        scale  = voxel.BasePosAndScale.w;
        
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
        }
#endif
    }
    
    // All threads must complete their work so that we can read s_TaskCount
    GroupMemoryBarrierWithGroupSync();

    if (node.VoxelBufDataCount > 0 && I == 0)
    {
#if SHOW_STATISTICS
        // Update statistics from the first thread
        uint orig_value_task_count;
        Statistics.InterlockedAdd(0, s_TaskCount, orig_value_task_count);
        
        uint orig_value_ocn_count;
        Statistics.InterlockedAdd(4, s_OctreeNodeCount, orig_value_ocn_count);
#endif
    }
    
    // This function must be called exactly once per amplification shader.
    // The DispatchMesh call implies a GroupMemoryBarrierWithGroupSync(), and ends the amplification shader group's execution.
    DispatchMesh(s_TaskCount, 1, 1, s_Payload);
}
