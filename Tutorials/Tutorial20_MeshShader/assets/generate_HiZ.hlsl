Texture2D<float> InputTexture : register(t0);
RWTexture2D<float> OutputTexture : register(u0);

cbuffer Constants : register(b0)
{
    uint2 InputDimensions;
    uint2 OutputDimensions;
    uint Level;
};

#define THREAD_GROUP_SIZE 32

[numthreads(THREAD_GROUP_SIZE, THREAD_GROUP_SIZE, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    // Early exit if we're outside the output dimensions
    if (any(DTid.xy >= OutputDimensions))
        return;

    // Calculate the corresponding position in the input texture
    uint2 InputPos = DTid.xy * 2;

    // Sample four texels from the input texture
    float z1 = InputTexture.Load(int3(InputPos, 0));
    float z2 = InputTexture.Load(int3(min(InputPos + uint2(1, 0), InputDimensions - 1), 0));
    float z3 = InputTexture.Load(int3(min(InputPos + uint2(0, 1), InputDimensions - 1), 0));
    float z4 = InputTexture.Load(int3(min(InputPos + uint2(1, 1), InputDimensions - 1), 0));

    // Find the maximum Z value (assuming reverse Z -> nearest z value)
    float maxZ = max(max(z1, z2), max(z3, z4));

    // Write the result
    OutputTexture[DTid.xy] = maxZ;
}