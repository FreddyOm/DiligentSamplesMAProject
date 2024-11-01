#include "structures.fxh"

Texture2D g_Texture;
SamplerState g_Texture_sampler; // By convention, texture samplers must use the '_sampler' suffix

struct PSInput
{
    float4 Pos : SV_POSITION;
    float4 Color : COLOR;
    float2 UV : TEXCOORD;
    float3 Normal : NORMAL;
};

struct PSOutput
{
    float4 Color : SV_TARGET;
};

cbuffer cbConstants
{
    Constants g_Constants;
}

static const float3 g_LightDirection = normalize(float3(-1.0f, -1.0f, -1.0f)); // Static light direction
static const float4 g_LightColor = float4(1.0f, 1.0f, 1.0f, 1.0f); // Light color (white)
static const float g_AmbientIntensity = 0.2f;

bool GetRenderOption(uint bit)
{
    return (g_Constants.RenderOptions & (1u << bit)) ? true : false;
}

void main(in PSInput PSIn,
          out PSOutput PSOut)
{    
    // Normalize the interpolated normal
    float3 normal = normalize(PSIn.Normal);

    // Compute the diffuse lighting (Lambertian)
    float diffuseIntensity = saturate(dot(normal, -g_LightDirection));

    // Combine ambient and diffuse lighting
    float4 diffuseColor = diffuseIntensity * g_LightColor;
    float4 ambientColor = g_AmbientIntensity * g_LightColor;
    float4 finalColor = ambientColor + diffuseColor;

    // Combine final lighting color with texture color or mesh color
    if (length(PSIn.Color.xyz) > 0.0f)
    {
        PSOut.Color = PSIn.Color * (GetRenderOption(4) ? finalColor : 1.0f);
    }
    else
    {
        PSOut.Color = g_Texture.Sample(g_Texture_sampler, PSIn.UV) * (GetRenderOption(4) ? finalColor : 1.0f);
    }
}
