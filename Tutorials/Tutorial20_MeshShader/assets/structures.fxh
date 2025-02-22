#ifndef GROUP_SIZE
#define GROUP_SIZE 64   // max 1024
#endif

// 32 bytes
struct OctreeLeafNode  // OctreeLeafNode
{
    float4 BasePosAndScale;     // [x, y, z, xyzScale]   To compute node bounding box for frustum culling 
    float4 RandomValue;         // [rand, alignedDrawTaskSize, drawTaskCountPadding, 0]
    
    // Payload
    int VoxelBufStartIndex;
    int VoxelBufDataCount;
    
    int2 Padding;
};

struct DepthPrepassDrawTask
{
    float4 BasePosAndScale; // [x, y, z, scale]
    int BestOccluderCount;
    int3 Padding;
};

struct VoxelBufData
{
    float4 BasePosAndScale; // [ x, y, z, scale ]
};

// 168 bytes
struct Constants
{
    float4x4 ViewMat;           // 4 * 16 = 64
    float4x4 ViewProjMat;       // 4 * 16 = 64
    
    float4 Frustum[6];          // 4 * 6 = 24
    float DepthBias;            // 4
    uint RenderOptions;         // 4    // Bitwise Options: 
                                //         [
                                //              0 = CullMode, 
                                //              1 = UseOcclusionCulling, 
                                //              2 = UseFrustumCulling, 
                                //              3 = ShowOnlyBestOccluders, 
                                //              4 = UseLight, 
                                //              5 = MeshShadingDebugViz, 
                                //              6 = OctreeDebugViz
                                //          ]
};

// Payload size must be less than 16kb.
struct Payload
{
    // Currently, DXC fails to compile the code when
    // the struct declares float3 Pos, so we have to
    // use struct of arrays
    float PosX[GROUP_SIZE];
    float PosY[GROUP_SIZE];
    float PosZ[GROUP_SIZE];
    float Scale[GROUP_SIZE];
    float MSRand[GROUP_SIZE];
};

struct HiZConstants
{
    uint2 InputDimensions;
    uint2 OutputDimensions;
    uint Level;
    uint Padding[3];
};
