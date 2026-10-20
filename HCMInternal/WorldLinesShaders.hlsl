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
    float4 scene;       // the game's own depth (a copy, t0): x its near plane, y its far plane (0: none), z 1 = reversed
                        // (1 at the near plane, 0 far away), w 1 = in use
    float4 sceneMap;    // x, y: its pixels per target pixel; z: something of the game's nearer than this (world units)
                        // and in front of a line hides it (the first-person gun and arms)
};

Texture2D<float> sceneDepth : register(t0);

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

// the game's depth -> distance along its view (its projection's near and far planes, reversed or not); nothing = far
float sceneDistance(float g)
{
    const float n = scene.x, f = scene.y;
    if (scene.z > 0.5)
    {
        if (g <= 0) return 1e30;
        return f > n ? n * f / (n + g * (f - n)) : n / g;
    }
    if (g >= 1) return 1e30;
    return f > n ? n * f / (f - g * (f - n)) : n / (1 - g);
}

float4 PSLine(PixelIn i) : SV_Target
{
    if (scene.w > 0.5)
    {
        // the game drew something close in front of this piece of line (the first-person gun or arms: the game's
        // picture is under the lines, so they would be drawn over it). The same surface as the line's (the game's
        // picture of the map) is never "in front": it must be nearer by 8 % and 3 cm
        const float d = sceneDistance(sceneDepth.Load(int3(int2(i.pos.xy * sceneMap.xy), 0)));
        const float lineDistance = viewport.w / max(i.pos.z, 1e-12);       // our depth is near / distance
        if (d < sceneMap.z && d < lineDistance * 0.92 - 0.03) discard;
    }
    const float alpha = saturate(viewport.z + 0.5 - abs(i.side));
    return float4(colour.rgb, colour.a * alpha);
}
