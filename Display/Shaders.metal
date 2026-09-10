#include <metal_stdlib>
using namespace metal;
struct Raster { float4 position [[position]]; float2 uv; };
vertex Raster framebufferVertex(uint id [[vertex_id]], constant float2 &scale [[buffer(0)]]) {
    const float2 positions[] = {float2(-1,-1), float2(1,-1), float2(-1,1), float2(1,1)};
    const float2 uv[] = {float2(0,1), float2(1,1), float2(0,0), float2(1,0)};
    return {float4(positions[id] * scale, 0, 1), uv[id]};
}
fragment float4 framebufferFragment(Raster in [[stage_in]], texture2d<float> image [[texture(0)]]) {
    constexpr sampler sampleFilter(coord::normalized, address::clamp_to_edge, filter::linear);
    return image.sample(sampleFilter, in.uv);
}
