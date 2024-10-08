#ifndef GROUP_SIZE
#define GROUP_SIZE 64
#endif

// 32 bytes
struct DrawTask
{
    float4 BasePosAndScale;     // [x, y, z, xyzScale]
    float4 RandomValue;         // [rand, alignedDrawTaskSize, drawTaskCountPadding, 0]
    
};

struct NormalDrawTask   // OctreeLeafNode
{
    float LeafNodeBasePosition; // To compute node bounding box for frustum culling 
    
    // Payload
    int startIndex;
    int dataCount;
};

struct DepthPrepassDrawTask
{
    float4 basePositionAndScale; // [x, y, z, scale]
};

struct VoxelBuffer
{
    float4 BasePositionAndRandVal; // [x, y, z, random value]
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
    
    uint OcclusionCulling;      // 4

    uint3 Padding;              // 12
};

// 32 bytes
struct GPUOctreeNode
{
    float4 minAndIsFull;        // [x, y, z, isFull]
    float4 max;
    int childrenStartIndex;
    int numChildren;
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
