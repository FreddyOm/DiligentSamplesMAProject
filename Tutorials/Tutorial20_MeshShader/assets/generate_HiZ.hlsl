Texture2D<float> InverseDepthBuffer : register(t0);
RWTexture2D<float> HiZBuffer : register(u0);

[numthreads(8, 8, 1)]
void CS_GenerateHiZ(uint3 DispatchThreadID : SV_DispatchThreadID)
{
    uint2 texCoord = DispatchThreadID.xy * 2; // We're reducing resolution by half
    
    float depth1 = InverseDepthBuffer[texCoord + uint2(0, 0)];
    float depth2 = InverseDepthBuffer[texCoord + uint2(1, 0)];
    float depth3 = InverseDepthBuffer[texCoord + uint2(0, 1)];
    float depth4 = InverseDepthBuffer[texCoord + uint2(1, 1)];
    
    // Store the maximum (nearest) depth value for inverse depth buffer
    HiZBuffer[DispatchThreadID.xy] = max(max(depth1, depth2), max(depth3, depth4));
}