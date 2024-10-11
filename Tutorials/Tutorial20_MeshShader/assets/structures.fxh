#ifndef GROUP_SIZE
#define GROUP_SIZE 64   // 1024
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

    float CoTanHalfFov;         // 4
    float MSDebugViz;           // 4
    float OctreeDebugViz;       // 4
    uint FrustumCulling;        // 4    // @TODO: Change this to single bits in one uint
    
    uint ShowOnlyBestOccluders; // 4

    uint3 Padding;              // 12
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
