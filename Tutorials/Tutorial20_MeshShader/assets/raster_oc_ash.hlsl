#include "structures.fxh"


cbuffer cbConstants
{
	Constants g_Constants;
}

StructuredBuffer<VoxelBufData> VoxelPositionBuffer : register(t0);

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
    
    VisibilityBuffer.Store(gid * 4, 0); // Reset visibility buffer. @TODO: Maybe Store4 ?
    
    
    if (gid < g_Constants.VoxelCount)
    {
        VoxelBufData node = VoxelPositionBuffer[gid];
        float3 pos = node.BasePosAndScale.xyz;
        float scale = node.BasePosAndScale.w;
        
        uint index = 0;
        InterlockedAdd(s_TaskCount, 1, index);
        
        // Add mesh data to payload
        s_Payload.PosX[index] = pos.x;
        s_Payload.PosY[index] = pos.y;
        s_Payload.PosZ[index] = pos.z;
        s_Payload.Scale[index] = scale;
        s_Payload.MSRand[index] = 0.0f;
    }   
    
    
    GroupMemoryBarrierWithGroupSync();
    
    DispatchMesh(s_TaskCount, 1, 1, s_Payload);
}