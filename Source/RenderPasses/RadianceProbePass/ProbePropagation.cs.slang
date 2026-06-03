/***************************************************************************
 # Copyright (c) 2015-24, NVIDIA CORPORATION. All rights reserved.
 **************************************************************************/

cbuffer CB
{
    uint3 gGridSize;
    float gPropagationStrength;
}

Texture3D<float4> gProbeRadianceIn;
RWTexture3D<float4> gProbeRadianceOut;

float4 sampleProbe(int3 coord)
{
    coord = clamp(coord, int3(0, 0, 0), int3(gGridSize) - int3(1, 1, 1));
    return gProbeRadianceIn.Load(int4(coord, 0));
}

[numthreads(4, 4, 4)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint3 coordU = dispatchThreadId;
    if (any(coordU >= gGridSize))
        return;

    const int3 coord = int3(coordU);

    const float4 center = sampleProbe(coord);
    const float4 xp = sampleProbe(coord + int3(1, 0, 0));
    const float4 xm = sampleProbe(coord + int3(-1, 0, 0));
    const float4 yp = sampleProbe(coord + int3(0, 1, 0));
    const float4 ym = sampleProbe(coord + int3(0, -1, 0));
    const float4 zp = sampleProbe(coord + int3(0, 0, 1));
    const float4 zm = sampleProbe(coord + int3(0, 0, -1));

    const float4 neighborAverage = (xp + xm + yp + ym + zp + zm) / 6.0f;
    const float4 propagated = lerp(center, neighborAverage, saturate(gPropagationStrength));

    gProbeRadianceOut[coordU] = float4(max(propagated.rgb, float3(0.f)), 1.f);
}
