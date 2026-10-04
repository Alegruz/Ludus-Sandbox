// Painted top-down ocean for Poseidon Saves the Day (the "Drift" jam theme).
//
// One fullscreen pass. Ambient swells over a simulated, advected free surface.
// Everything is authored from the single uniform block below. Only f32
// scalar/vector arithmetic, uniform fields, SV_VulkanVertexID, SV_Position and a
// location-0 RGBA output are used (the engine's validated shader subset; see
// fullscreen-rendering-handoff.md).
//
// The CPU mirror of this layout is src/ocean/ocean_uniforms.h; the two must
// agree (offsets asserted on the C++ side). set/group 0, binding 0, 16128 bytes.

#ifndef LUDUS_GLSL_ES
#define LUDUS_GLSL_ES 0
#endif

struct OceanUniforms
{
    float2 resolution;    // framebuffer pixels
    float2 worldView;     // (viewWidthMeters, viewHeightMeters)

    float waveTime;       // accumulated animation phase (seconds * speed), wrapped
    float2 currentOffset; // accumulated bulk-current displacement (meters)
    float waveScaleMeters; // dominant wavelength in meters

    float waveAnimationSpeed;
    float waveIntensity;
    float foamAmount;
    float currentDirCos;  // unit current direction x

    float currentDirSin;  // unit current direction y
    float pad0;
    float pad1;
    float pad2;

    float4 deepColor;
    float4 midColor;
    float4 shallowColor;
    float4 foamColor;
    float4 boatInfo; // position.xy, heading, radius
    float4 gameInfo; // enabled, active rings, cooldown fraction, contact flash
    float4 ripple0; // origin.xy, radius, fade
    float4 ripple1; // origin.xy, radius, fade
    float4 ripple2; // origin.xy, radius, fade
    float4 ripple3; // origin.xy, radius, fade
    float4 ripple4; // origin.xy, radius, fade
    float4 ripple5; // origin.xy, radius, fade
    float4 ripple6; // origin.xy, radius, fade
    float4 ripple7; // origin.xy, radius, fade
    float4 ripple8; // origin.xy, radius, fade
    float4 ripple9; // origin.xy, radius, fade
    float4 ripple10; // origin.xy, radius, fade
    float4 ripple11; // origin.xy, radius, fade
    float4 ripple12; // origin.xy, radius, fade
    float4 ripple13; // origin.xy, radius, fade
    float4 ripple14; // origin.xy, radius, fade
    float4 ripple15; // origin.xy, radius, fade
    float4 levelBounds; // half width/height, reserved
    float4 dockInfo; // center.xy, radius, arrival speed
    float4 levelInfo; // rock count, phase, dock progress, boundary hazard
    float4 rock0; // center.xy, radius, reserved
    float4 rock1; // center.xy, radius, reserved
    float4 rock2; // center.xy, radius, reserved
    float4 rock3; // center.xy, radius, reserved
    float4 rock4; // center.xy, radius, reserved
    float4 rock5; // center.xy, radius, reserved
    float4 rock6; // center.xy, radius, reserved
    float4 rock7; // center.xy, radius, reserved
    float4 rock8; // center.xy, radius, reserved
    float4 rock9; // center.xy, radius, reserved
    float4 rock10; // center.xy, radius, reserved
    float4 rock11; // center.xy, radius, reserved
    float4 rock12; // center.xy, radius, reserved
    float4 rock13; // center.xy, radius, reserved
    float4 rock14; // center.xy, radius, reserved
    float4 rock15; // center.xy, radius, reserved
    float4 boatMotion; // velocity.xy, speed, reserved
    float4 flowInfo; // snapshot dimensions, signed velocity/height ranges
    float4 flow[192]; // coarse transport: momentum and advected material map
    float4 surface[768]; // full 48 x 64 surface: 16-bit height, 8-bit foam

};

[[vk::binding(0, 0)]] ConstantBuffer<OceanUniforms> ocean;

// Decode before interpolation: packed channels are exact 24-bit integers.
float3 unpackChannels(float packed)
{
    return float3(fmod(packed, 256.0), fmod(floor(packed / 256.0), 256.0), floor(packed / 65536.0));
}
struct FlowSample
{
    float2 velocity;
    float height;
    float foam;
    float2 displacement;
};
FlowSample flowCell(int2 cell)
{
    cell = clamp(cell, int2(0, 0), int2(15, 23));
    int index = cell.y * 16 + cell.x;
    float4 packed = ocean.flow[index / 2];
    float3 momentum = unpackChannels(packed[(index % 2) * 2]);
    float3 material = unpackChannels(packed[(index % 2) * 2 + 1]);
    FlowSample result;
    result.velocity = (momentum.xy - 128.0) / 127.0 * ocean.flowInfo.z;
    result.height = (momentum.z - 128.0) / 127.0 * ocean.flowInfo.w;
    result.foam = material.x / 255.0;
    result.displacement = (material.yz - 128.0) / 127.0 * 12.0;
    return result;
}
FlowSample fieldAt(float2 world)
{
    FlowSample result;
    result.velocity = float2(0.0, 0.0);
    result.height = 0.0;
    result.foam = 0.0;
    result.displacement = float2(0.0, 0.0);
    if (ocean.flowInfo.z <= 0.0 || any(abs(world) >= ocean.levelBounds.xy)) return result;
    float2 grid = (world / ocean.levelBounds.xy * 0.5 + 0.5) * ocean.flowInfo.xy - 0.5;
    int2 cell = int2(floor(grid));
    float2 f = frac(grid);
    FlowSample a = flowCell(cell), b = flowCell(cell + int2(1, 0));
    FlowSample c = flowCell(cell + int2(0, 1)), d = flowCell(cell + int2(1, 1));
    result.velocity = lerp(lerp(a.velocity, b.velocity, f.x), lerp(c.velocity, d.velocity, f.x), f.y);
    result.height = lerp(lerp(a.height, b.height, f.x), lerp(c.height, d.height, f.x), f.y);
    result.foam = lerp(lerp(a.foam, b.foam, f.x), lerp(c.foam, d.foam, f.x), f.y);
    result.displacement = lerp(lerp(a.displacement, b.displacement, f.x), lerp(c.displacement, d.displacement, f.x), f.y);
    return result;
}

struct SurfaceSample
{
    float height;
    float foam;
    float2 slope;
};
float2 surfaceCell(int2 cell)
{
    cell = clamp(cell, int2(0, 0), int2(47, 63));
    int index = cell.y * 48 + cell.x;
    float packed = ocean.surface[index / 4][index % 4];
    return float2((fmod(packed, 65536.0) - 32768.0) / 32767.0 * 6.0,
                  floor(packed / 65536.0) / 255.0);
}
SurfaceSample surfaceAt(float2 world)
{
    SurfaceSample result;
    result.height = 0.0;
    result.foam = 0.0;
    result.slope = float2(0.0, 0.0);
    if (ocean.flowInfo.z <= 0.0 || any(abs(world) >= ocean.levelBounds.xy)) return result;
    float2 spacing = ocean.levelBounds.xy * 2.0 / float2(48.0, 64.0);
    float2 grid = (world + ocean.levelBounds.xy) / spacing - 0.5;
    int2 center = int2(floor(grid + 0.5));
    float2 f = grid - float2(center);
    // Quadratic B-spline reconstruction gives continuous, analytic normals
    // from nine real height samples. Foam uses the same positive weights.
    float3 wx = float3(0.5 * pow(0.5 - f.x, 2.0), 0.75 - f.x * f.x, 0.5 * pow(0.5 + f.x, 2.0));
    float3 wy = float3(0.5 * pow(0.5 - f.y, 2.0), 0.75 - f.y * f.y, 0.5 * pow(0.5 + f.y, 2.0));
    float3 dx = float3(f.x - 0.5, -2.0 * f.x, f.x + 0.5);
    float3 dy = float3(f.y - 0.5, -2.0 * f.y, f.y + 0.5);
    for (int j = 0; j < 3; ++j)
    {
        for (int i = 0; i < 3; ++i)
        {
            float2 sample = surfaceCell(center + int2(i - 1, j - 1));
            float weight = wx[i] * wy[j];
            result.height += sample.x * weight;
            result.foam += sample.y * weight;
            result.slope += sample.x * float2(dx[i] * wy[j], wx[i] * dy[j]) / spacing;
        }
    }
    return result;
}

float4 rockAt(int index)
{
    switch (index)
    {
        case 0: return ocean.rock0;
        case 1: return ocean.rock1;
        case 2: return ocean.rock2;
        case 3: return ocean.rock3;
        case 4: return ocean.rock4;
        case 5: return ocean.rock5;
        case 6: return ocean.rock6;
        case 7: return ocean.rock7;
        case 8: return ocean.rock8;
        case 9: return ocean.rock9;
        case 10: return ocean.rock10;
        case 11: return ocean.rock11;
        case 12: return ocean.rock12;
        case 13: return ocean.rock13;
        case 14: return ocean.rock14;
        default: return ocean.rock15;
    }
}

[shader("vertex")]
float4 vertexMain(uint id : SV_VulkanVertexID) : SV_Position
{
    // Fullscreen triangle from the vertex id (no vertex buffer).
    float2 p = float2((id << 1) & 2, id & 2);
    return float4(p * 2.0 - 1.0, 0.0, 1.0);
}

// --- Procedural helpers (graphic, low-noise) -------------------------------

// Smooth value noise on a lattice; gentle, not sparkly.
float hash21(float2 p)
{
    p = frac(p * float2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return frac(p.x * p.y);
}

float valueNoise(float2 p)
{
    float2 i = floor(p);
    float2 f = frac(p);
    float2 u = f * f * (3.0 - 2.0 * f);
    float a = hash21(i + float2(0.0, 0.0));
    float b = hash21(i + float2(1.0, 0.0));
    float c = hash21(i + float2(0.0, 1.0));
    float d = hash21(i + float2(1.0, 1.0));
    return lerp(lerp(a, b, u.x), lerp(c, d, u.x), u.y);
}

float broadField(float2 p)
{
    return valueNoise(p) * 0.65 + valueNoise(p * 2.03 + 7.1) * 0.35;
}

[shader("fragment")]
float4 fragmentMain(float4 position : SV_Position) : SV_Target0
{
#if LUDUS_GLSL_ES
    position.y = ocean.resolution.y - position.y;
#endif
    // Screen -> world (meters). Top-left origin screen; flip Y so world +Y = up.
    // Aspect-correct: vertical extent fixed, horizontal derived from resolution.
    float2 res = max(ocean.resolution, float2(1.0, 1.0));
    float2 ndc = position.xy / res;                 // 0..1, top-left
    float metersPerPixel = ocean.worldView.y / res.y;
    float2 world;
    world.x = (position.x - res.x * 0.5) * metersPerPixel;
    world.y = (res.y * 0.5 - position.y) * metersPerPixel; // flip Y

    float pixelWidth = max(metersPerPixel, 0.035);
    FlowSample flow = fieldAt(world);
    SurfaceSample surface = surfaceAt(world);
    float2 surfaceWorld = world - flow.displacement;
    float t = ocean.waveTime;

    // Sub-grid gravity ripples contribute only small normal detail. Resolved
    // waves, splash interference and foam are entirely the simulated surface.
    float2 slope = surface.slope;
    for (int i = 0; i < 3; ++i)
    {
        float angle = 0.7 + float(i) * 1.83;
        float2 direction = float2(cos(angle), sin(angle));
        float frequency = (3.1 + float(i) * 1.7) * 10.0 / max(ocean.waveScaleMeters, 1.0);
        float phase = dot(surfaceWorld - ocean.currentOffset, direction) * frequency -
                      sqrt(9.8 * frequency) * t;
        slope += direction * cos(phase) * 0.016 * ocean.waveIntensity;
    }
    float3 normal = normalize(float3(-slope, 1.0));
    float diffuse = max(dot(normal, normalize(float3(-0.27, 0.32, 0.90))), 0.0);
    float2 advected = surfaceWorld - ocean.currentOffset;
    // Material displacement transports the pigment. Height and slope still
    // come from the shared solver, including wind, splashes and interference.
    float patchScale = max(ocean.waveScaleMeters * 1.6, 1.0);
    float2 paint = advected / patchScale;
    float2 brush = float2(valueNoise(paint + float2(4.2, 7.1)), valueNoise(paint + float2(9.3, 2.8))) - 0.5;
    paint += brush * 1.4;
    float patches = broadField(paint) * 0.8 + broadField(paint * 2.3 + float2(3.4, 8.1)) * 0.2;
    float pigment = valueNoise(surfaceWorld * 1.5);
    float wash = patches + (pigment - 0.5) * 0.055;
    float level = saturate(wash + surface.height * 0.14);
    float washEdge = max(0.002, metersPerPixel / patchScale * 0.35);
    float3 deep = ocean.deepColor.rgb;
    float3 color = lerp(deep, ocean.midColor.rgb, smoothstep(0.38 - washEdge, 0.38 + washEdge, level));
    color = lerp(color, ocean.shallowColor.rgb, smoothstep(0.56 - washEdge, 0.56 + washEdge, level));
    // Matte paper lighting preserves the simulated crests without glare.
    float paperGrain = hash21(floor(position.xy));
    color *= (0.82 + diffuse * 0.20) * (0.89 + pigment * 0.16 + paperGrain * 0.06);
    float crest = smoothstep(0.03, 0.32, surface.height);
    color += float3(0.05, 0.13, 0.15) * crest * 0.55;
    float breakUp = broadField(advected / max(ocean.waveScaleMeters * 0.5, 1.0));
    float paintEdge = 1.0 - smoothstep(washEdge, 0.012 + washEdge, abs(wash - 0.56));
    float paintedFoam = paintEdge * smoothstep(0.40, 0.58, breakUp) * ocean.foamAmount * 2.6;
    float foam = smoothstep(0.015, 0.45, surface.foam) * ocean.foamAmount *
                 (0.72 + 0.28 * valueNoise(surfaceWorld * 2.4));
    color = lerp(color, ocean.foamColor.rgb * (0.97 + paperGrain * 0.03), saturate(foam + paintedFoam) * 0.94);

    // Gentle vignette keeps contrast for future boats/debris at the center.
    float2 c = ndc - 0.5;
    float vignette = 1.0 - dot(c, c) * 0.3;
    color *= vignette;

    if (ocean.gameInfo.x > 0.5)
    {
        float2 extent = ocean.levelBounds.xy;
        float outside = step(extent.x, abs(world.x)) + step(extent.y, abs(world.y));
        color = lerp(color, deep * 0.65, saturate(outside) * 0.65);
        float edge = min(abs(abs(world.x) - extent.x), abs(abs(world.y) - extent.y));
        float inBounds = step(abs(world.x), extent.x + 0.25) * step(abs(world.y), extent.y + 0.25);
        float boundary = (1.0 - smoothstep(pixelWidth * 0.5, pixelWidth * 1.5, edge)) * inBounds;
        float rope = 0.65 + 0.35 * sin((world.x + world.y) * 4.0);
        float3 boundaryColor = lerp(float3(0.73, 0.62, 0.39), float3(0.95, 0.56, 0.30), ocean.levelInfo.w);
        color = lerp(color, boundaryColor, boundary * (0.55 + rope * 0.25));
        if (ocean.dockInfo.z > 0.0)
        {
            float2 mooring = world - ocean.dockInfo.xy;
            float distance = length(mooring);
            float zone = 1.0 - smoothstep(ocean.dockInfo.z - 0.1, ocean.dockInfo.z + 0.1, distance);
            float dockEdge = 1.0 - smoothstep(pixelWidth * 0.6, pixelWidth * 1.6, abs(distance - ocean.dockInfo.z));
            float3 dockColor = lerp(float3(0.43, 0.78, 0.62), float3(0.94, 0.93, 0.72), ocean.levelInfo.z);
            color = lerp(color, dockColor, zone * 0.08 + dockEdge * 0.9);
            // A small nautical anchor pairs the safe color with a visible shape.
            float stem = (1.0 - smoothstep(0.10, 0.10 + pixelWidth, abs(mooring.x))) *
                         (1.0 - smoothstep(0.50, 0.50 + pixelWidth, abs(mooring.y)));
            float eye = 1.0 - smoothstep(0.08, 0.08 + pixelWidth, abs(length(mooring - float2(0.0, 0.65)) - 0.22));
            float arms = (1.0 - smoothstep(0.08, 0.08 + pixelWidth, abs(length(mooring) - 0.70))) *
                         (1.0 - smoothstep(-0.12, 0.05, mooring.y));
            float crossbar = (1.0 - smoothstep(0.08, 0.08 + pixelWidth, abs(mooring.y - 0.20))) *
                             (1.0 - smoothstep(0.35, 0.35 + pixelWidth, abs(mooring.x)));
            color = lerp(color, ocean.foamColor.rgb, saturate(stem + eye + arms + crossbar) * 0.9);
        }
        for (int i = 0; i < 16; ++i)
        {
            if (float(i) >= ocean.levelInfo.x) break;
            float4 rock = rockAt(i);
            float2 p = world - rock.xy;
            float distance = length(p);
            float edgeWidth = max(0.04, metersPerPixel);
            float shape = 1.0 - smoothstep(rock.z - edgeWidth * 0.5, rock.z + edgeWidth * 0.5, distance);
            float foamEdge = 1.0 - smoothstep(0.08, 0.32 + pixelWidth, abs(distance - rock.z - 0.18));
            color = lerp(color, ocean.foamColor.rgb, foamEdge * (0.35 + pigment * 0.35));
            float grain = valueNoise(p * 0.9 + float2(12.3, 5.7));
            float2 stoneLocal = p / rock.z;
            float facet = max(dot(stoneLocal, float2(-0.8, 0.6)), dot(stoneLocal, float2(0.35, 0.94)));
            float lighting = smoothstep(-0.5, 0.7, facet + grain * 0.25);
            float3 stone = lerp(float3(0.28, 0.33, 0.32), float3(0.66, 0.63, 0.49), lighting);
            float ink = smoothstep(rock.z - edgeWidth * 2.5, rock.z, distance);
            stone *= 0.94 + grain * 0.12;
            stone = lerp(stone, deep, ink * 0.5);
            color = lerp(color, stone, shape);
        }
        float2 relative = world - ocean.boatInfo.xy;
        float ca = cos(ocean.boatInfo.z), sa = sin(ocean.boatInfo.z);
        float2 boat = float2(dot(relative, float2(ca, sa)), dot(relative, float2(-sa, ca)));
        float radius = ocean.boatInfo.w;
        boat.y += surfaceAt(ocean.boatInfo.xy).height * 0.35;
        float bow = saturate(boat.x / radius);
        float hullDistance = length(boat / float2(radius, radius * (0.62 - bow * 0.15)));
        float hull = 1.0 - smoothstep(0.94, 1.02, hullDistance);
        float shadow = 1.0 - smoothstep(0.9, 1.2, length((boat + float2(0.15, 0.20)) / float2(radius, radius * 0.62)));
        color = lerp(color, float3(0.02, 0.05, 0.10), shadow * 0.65);
        float deck = 1.0 - smoothstep(0.62, 0.70, hullDistance);
        float3 hullColor = lerp(float3(0.79, 0.47, 0.18), float3(0.94, 0.85, 0.65), deck);
        float2 timber = boat / radius;
        float planks = 1.0 - smoothstep(0.025, 0.06, abs(frac(timber.y * 4.0 + 0.5) - 0.5));
        hullColor = lerp(hullColor, float3(0.57, 0.36, 0.17), planks * deck * 0.5);
        float seats = (1.0 - smoothstep(0.045, 0.09, min(abs(timber.x - 0.30), abs(timber.x + 0.35)))) *
                      (1.0 - smoothstep(0.38, 0.46, abs(timber.y)));
        hullColor = lerp(hullColor, float3(0.96, 0.89, 0.72), seats);
        float rail = smoothstep(0.79, 0.86, hullDistance) * (1.0 - smoothstep(0.92, 0.98, hullDistance));
        hullColor = lerp(hullColor, float3(0.98, 0.74, 0.39), rail * 0.8);
        hullColor = lerp(hullColor, float3(1.0, 0.96, 0.70), ocean.gameInfo.w * 0.5);
        if (ocean.levelInfo.y > 0.5 && ocean.levelInfo.y < 1.5)
            hullColor = lerp(hullColor, float3(0.95, 0.25, 0.18), 0.7);
        if (ocean.levelInfo.y > 1.5)
            hullColor = lerp(hullColor, float3(0.58, 0.88, 0.61), 0.20);
        // The circular hull outline is the exact collision envelope; the
        // illustrated boat inside it is decorative and can change heading.
        float hullEdge = (1.0 - smoothstep(0.04, 0.12, abs(length(relative) - radius))) * 0.5;
        color = lerp(color, float3(0.97, 0.85, 0.57), hullEdge);
        color = lerp(color, hullColor, hull);
    }
    return float4(color, 1.0);
}
