#include "structures.fxh"

cbuffer cbConstants
{
    Constants g_Constants;
}

// Occlusion results
RWByteAddressBuffer VisibilityBuffer : register(u0);

static const uint constCubeIndices[12 * 3] =
{
    2, 0, 1,
    2, 3, 0,
    4, 6, 5,
    4, 7, 6,
    8, 10, 9,
    8, 11, 10,
    12, 14, 13,
    12, 15, 14,
    16, 18, 17,
    16, 19, 18,
    20, 21, 22,
    20, 22, 23,
};

static const float3 constCubePos[24] =
{
    float3(-1, -1, -1),
    float3(-1, +1, -1),
    float3(+1, +1, -1),
    float3(+1, -1, -1),
    float3(-1, -1, -1),
    float3(-1, -1, +1),
    float3(+1, -1, +1),
    float3(+1, -1, -1),
    float3(+1, -1, -1),
    float3(+1, -1, +1),
    float3(+1, +1, +1),
    float3(+1, +1, -1),
    float3(+1, +1, -1),
    float3(+1, +1, +1),
    float3(-1, +1, +1),
    float3(-1, +1, -1),
    float3(-1, +1, -1),
    float3(-1, +1, +1),
    float3(-1, -1, +1),
    float3(-1, -1, -1),
    float3(-1, -1, +1),
    float3(+1, -1, +1),
    float3(+1, +1, +1),
    float3(-1, +1, +1),
};

struct PSInput
{
    float4 Pos : SV_POSITION;
};

[numthreads(24, 1, 1)]
[outputtopology("triangle")] // output primitive type is triangle list
void main(in uint I : SV_GroupIndex, // thread index used to access mesh shader output (0 .. 23)
          in uint gid : SV_GroupID, // work group index used to access amplification shader output (0 .. s_TaskCount-1)
          in payload Payload payload, // entire amplification shader output can be accessed by the mesh shader
          out indices uint3 tris[12],
          out vertices PSInput verts[24])
{
    
    // The cube contains 24 vertices, 36 indices for 12 triangles.
    // Only the input values from the the first active thread are used.
    SetMeshOutputCounts(24, 12);
    
    // Read the amplification shader output for this group
    float3 pos;
    float scale = payload.Scale[gid];
    float randValue = payload.MSRand[gid];
    pos.x = payload.PosX[gid];
    pos.y = payload.PosY[gid];
    pos.z = payload.PosZ[gid];
    
    // Each thread handles only one vertex
    verts[I].Pos = mul(float4(pos + constCubePos[I].xyz * scale * 0.5, 1.0), g_Constants.ViewProjMat);
    
    // Only the first 12 threads write indices. We must not access the array outside of its bounds.
    if (I < 12)
    {
        tris[I] = float3(constCubeIndices[I * 3 + 0], constCubeIndices[I * 3 + 1], constCubeIndices[I * 3 + 2]);
    }
    
    // If still computed, its visible.
    VisibilityBuffer.Store(4 * gid, 1);

}