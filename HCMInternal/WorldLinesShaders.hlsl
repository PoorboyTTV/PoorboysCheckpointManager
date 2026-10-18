// Poorboys Checkpoint Manager: shaders for the map grid (WorldLinesGpu.cpp). Compiled into WorldLinesShaders.h by
// tools\make_worldlines_shaders.py (fxc, shader model 5.0); the .h is what the build uses.
//
// Projection: the game's camera, reversed depth with no far plane: clip = (x / tan_x, y / tan_y, near, depth), so the
// stored depth is near / distance (1 at the near plane, towards 0 far away) and closer = greater.

cbuffer Frame : register(b0)
{
    float4 rowX;        // clip.x = dot(float4(world, 1), rowX)
    float4 rowY;
    float4 rowZ;
    float4 rowW;
    float4 camPos;      // xyz: the camera
    float4 viewport;    // x, y: pixels; z: half the line width in pixels; w: the near plane
    float4 colour;      // the lines' colour (premultiplied by nothing: alpha blends)
    float4 limits;      // x: lines whose near end is further than this (world units along the view) are not drawn
};

float4 project(float3 p)
{
    float4 h = float4(p, 1);
    return float4(dot(h, rowX), dot(h, rowY), dot(h, rowZ), dot(h, rowW));
}

// depth pass: the collision triangles, no colour
float4 VSDepth(float3 p : POSITION) : SV_Position
{
    return project(p);
}

// lines: pulled a little towards the camera so a line lying on its own polygon wins the depth test
struct LineVertex
{
    float4 clip : TEXCOORD0;
};

LineVertex VSLine(float3 p : POSITION)
{
    float3 d = camPos.xyz - p;
    float len = length(d);
    float pull = min(len * 0.002 + 0.01, len * 0.5);
    LineVertex o;
    o.clip = project(len > 1e-6 ? p + d * (pull / len) : p);
    return o;
}

struct PixelIn
{
    float4 pos : SV_Position;
    noperspective float side : TEXCOORD0;     // pixels from the line's middle (screen-linear: the two ends can be at very different depths)
};

// a line becomes a quad of the wanted width in pixels (clipped at the near plane here; the rest by the hardware)
[maxvertexcount(4)]
void GSLine(line LineVertex v[2], inout TriangleStream<PixelIn> strip)
{
    float4 a = v[0].clip, b = v[1].clip;
    const float nearW = viewport.w;
    if (a.w < nearW && b.w < nearW) return;
    if (min(a.w, b.w) > limits.x) return;
    if (a.w < nearW) a = lerp(a, b, (nearW - a.w) / (b.w - a.w));
    else if (b.w < nearW) b = lerp(b, a, (nearW - b.w) / (a.w - b.w));
    const float2 half = viewport.xy * 0.5;
    const float2 pa = a.xy / a.w * half, pb = b.xy / b.w * half;      // pixels from the screen centre, y up
    float2 dir = pb - pa;
    const float len = length(dir);
    dir = len > 1e-4 ? dir / len : float2(1, 0);
    const float w = viewport.z + 1.0;                                  // one pixel more for the soft edge
    const float2 n = float2(-dir.y, dir.x) * w, e = dir * viewport.z;
    PixelIn o;
    o.pos = float4((pa - e + n) / half * a.w, a.z, a.w); o.side = w;  strip.Append(o);
    o.pos = float4((pa - e - n) / half * a.w, a.z, a.w); o.side = -w; strip.Append(o);
    o.pos = float4((pb + e + n) / half * b.w, b.z, b.w); o.side = w;  strip.Append(o);
    o.pos = float4((pb + e - n) / half * b.w, b.z, b.w); o.side = -w; strip.Append(o);
}

float4 PSLine(PixelIn i) : SV_Target
{
    const float alpha = saturate(viewport.z + 0.5 - abs(i.side));
    return float4(colour.rgb, colour.a * alpha);
}
