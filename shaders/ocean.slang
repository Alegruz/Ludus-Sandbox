// Procedural top-down stylized ocean for the "Drift" game jam theme.
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
    float cameraX;
    float cameraY;
    float riverTime;

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
    float4 levelBounds; // half width/height, river speed, rapid boost
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

// The prescribed mean transport matches RiverCurrent; the SDK field adds
// wind, splashes and gesture disturbances without forcing a closed basin open.
float riverSpeedAt(float2 world)
{
    float lane = world.x / ocean.levelBounds.x;
    float speed = ocean.levelBounds.z * (1.0 - 0.35 * lane * lane);
    for (int i = 0; i < 2; ++i)
    {
        float center = ocean.levelBounds.y * (float(i) * 0.5 - 0.25);
        float along = (world.y - center) / 12.0;
        float envelope = max(1.0 - along * along, 0.0);
        speed += ocean.levelBounds.z * ocean.levelBounds.w * envelope * envelope;
    }
    float fade = saturate((length(world - ocean.dockInfo.xy) - ocean.dockInfo.z - 2.0) / 8.0);
    return speed * fade * fade * (3.0 - 2.0 * fade);
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
    world += float2(ocean.cameraX, ocean.cameraY);

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
    float3 view = normalize(float3(0.12, -0.18, 1.0));
    float3 reflected = reflect(-view, normal);
    float facing = max(dot(normal, view), 0.0);
    float fresnel = 0.0204 + 0.9796 * pow(1.0 - facing, 5.0);
    float3 sky = lerp(float3(0.65, 0.82, 0.92), float3(0.13, 0.35, 0.58), saturate(reflected.z));
    float3 light = normalize(float3(-0.27, 0.32, 0.90));
    float diffuse = max(dot(normal, light), 0.0);
    float sun = pow(max(dot(reflected, light), 0.0), 70.0);
    // Depth absorption, surface lighting and Fresnel reflection distinguish
    // troughs from crests using the resolved height and slope.
    float depth = 4.0 + surface.height;
    float3 water = lerp(ocean.shallowColor.rgb, ocean.midColor.rgb, 1.0 - exp(-depth * 0.32));
    water = lerp(water, ocean.deepColor.rgb, 0.12 + 0.16 * (1.0 - diffuse));
    float3 color = water * (0.46 + diffuse * 0.76);
    color = lerp(color, sky, fresnel + 0.07);
    color += float3(1.0, 0.91, 0.70) * sun * 0.65;
    // A broad reflected skylight follows slope, keeping every wave readable in
    // this top-down camera without unrelated large-scale noise color patches.
    float skylight = pow(max(dot(reflected, normalize(float3(0.22, 0.34, 0.91))), 0.0), 18.0);
    color += float3(0.24, 0.40, 0.44) * skylight * 0.45;
    float crest = smoothstep(0.03, 0.32, surface.height);
    color += float3(0.05, 0.13, 0.15) * crest;
    float foam = smoothstep(0.015, 0.45, surface.foam) * ocean.foamAmount *
                 (0.72 + 0.28 * valueNoise(surfaceWorld * 2.4));
    color = lerp(color, ocean.foamColor.rgb, foam * 0.94);

    if (ocean.levelBounds.z > 0.0 && all(abs(world) < ocean.levelBounds.xy))
    {
        float speed = riverSpeedAt(world);
        // Broken longitudinal foam and small upward chevrons make the flow
        // readable even while the camera is following the boat.
        float2 carried = world - float2(0.0, speed * ocean.riverTime);
        float streak = pow(0.5 + 0.5 * sin(carried.x * 1.2 + sin(carried.y * 0.15)), 18.0);
        streak *= smoothstep(0.55, 0.85, valueNoise(carried * float2(0.9, 0.2)));
        float2 mark = float2(fmod(world.x + 1000.0, 10.0) - 5.0,
                            fmod(carried.y + 10000.0, 18.0) - 9.0);
        float arrow = (1.0 - smoothstep(0.08, 0.28, abs(mark.y + abs(mark.x) - 1.5))) *
                      (1.0 - smoothstep(1.3, 1.8, abs(mark.x)));
        color = lerp(color, ocean.foamColor.rgb, (streak * 0.20 + arrow * 0.14) * min(speed, 1.0));
    }

    // Gentle vignette keeps contrast for future boats/debris at the center.
    float2 c = ndc - 0.5;
    float vignette = 1.0 - dot(c, c) * 0.3;
    color *= vignette;

    if (ocean.gameInfo.x > 0.5)
    {
        float2 extent = ocean.levelBounds.xy;
        float outside = step(extent.x, abs(world.x)) + step(extent.y, abs(world.y));
        float land = saturate(outside);
        float grain = valueNoise(world * 0.65);
        float3 bank = lerp(float3(0.10, 0.16, 0.14), float3(0.25, 0.30, 0.22), grain);
        color = lerp(color, ocean.levelBounds.z > 0.0 ? bank : float3(0.025, 0.045, 0.075), land);
        float edge = min(abs(abs(world.x) - extent.x), abs(abs(world.y) - extent.y));
        float inBounds = step(abs(world.x), extent.x + 0.25) * step(abs(world.y), extent.y + 0.25);
        float boundary = (1.0 - smoothstep(0.06, 0.18, edge)) * inBounds;
        float3 boundaryColor = lerp(float3(0.40, 0.75, 0.77), float3(0.95, 0.64, 0.38), ocean.levelInfo.w);
        color = lerp(color, boundaryColor, boundary * 0.7);
        if (ocean.dockInfo.z > 0.0)
        {
            float distance = length(world - ocean.dockInfo.xy);
            float zone = 1.0 - smoothstep(ocean.dockInfo.z - 0.1, ocean.dockInfo.z + 0.1, distance);
            float dockEdge = 1.0 - smoothstep(0.10, 0.25, abs(distance - ocean.dockInfo.z));
            float3 dockColor = lerp(float3(0.28, 0.89, 0.69), float3(0.85, 1.0, 0.72), ocean.levelInfo.z);
            color = lerp(color, dockColor, zone * 0.13 + dockEdge * 0.8);
            // A small anchor cross makes the destination readable before arrival.
            float cross = (1.0 - smoothstep(0.08, 0.16, min(abs(world.x - ocean.dockInfo.x), abs(world.y - ocean.dockInfo.y)))) *
                          (1.0 - smoothstep(0.65, 0.8, distance));
            color = lerp(color, dockColor, cross * 0.85);
        }
        for (int i = 0; i < 16; ++i)
        {
            if (float(i) >= ocean.levelInfo.x) break;
            float4 rock = rockAt(i);
            float2 p = world - rock.xy;
            float distance = length(p);
            float edgeWidth = max(0.04, metersPerPixel);
            float shape = 1.0 - smoothstep(rock.z - edgeWidth * 0.5, rock.z + edgeWidth * 0.5, distance);
            float foamEdge = 1.0 - smoothstep(0.08, 0.32, abs(distance - rock.z - 0.18));
            color = lerp(color, float3(0.67, 0.88, 0.86), foamEdge * 0.5);
            float grain = valueNoise(p * 0.9 + float2(12.3, 5.7));
            float lighting = saturate(0.65 + dot(p / rock.z, float2(-0.15, 0.25)));
            float3 stone = lerp(float3(0.18, 0.23, 0.27), float3(0.54, 0.57, 0.52), lighting + grain * 0.15);
            color = lerp(color, stone, shape);
        }
        float2 relative = world - ocean.boatInfo.xy;
        float ca = cos(ocean.boatInfo.z), sa = sin(ocean.boatInfo.z);
        float2 boat = float2(dot(relative, float2(ca, sa)), dot(relative, float2(-sa, ca)));
        float radius = ocean.boatInfo.w;
        boat.y += surfaceAt(ocean.boatInfo.xy).height * 0.35;
        float hullDistance = length(boat / float2(radius, radius * 0.62));
        float hull = 1.0 - smoothstep(0.94, 1.02, hullDistance);
        float shadow = 1.0 - smoothstep(0.9, 1.2, length((boat + float2(0.15, 0.20)) / float2(radius, radius * 0.62)));
        color = lerp(color, float3(0.02, 0.05, 0.10), shadow * 0.65);
        float deck = 1.0 - smoothstep(0.62, 0.70, hullDistance);
        float3 hullColor = lerp(float3(0.23, 0.11, 0.07), float3(0.88, 0.65, 0.36), deck);
        float cabin = (1.0 - smoothstep(0.22, 0.3, abs(boat.x + 0.25))) *
                      (1.0 - smoothstep(0.22, 0.3, abs(boat.y)));
        hullColor = lerp(hullColor, float3(0.93, 0.90, 0.77), cabin);
        hullColor = lerp(hullColor, float3(1.0, 0.96, 0.70), ocean.gameInfo.w * 0.5);
        if (ocean.levelInfo.y > 0.5 && ocean.levelInfo.y < 1.5)
            hullColor = lerp(hullColor, float3(0.95, 0.25, 0.18), 0.7);
        if (ocean.levelInfo.y > 1.5)
            hullColor = lerp(hullColor, float3(0.38, 0.97, 0.66), 0.55);
        // The circular hull outline is the exact collision envelope; the
        // illustrated boat inside it is decorative and can change heading.
        float hullEdge = (1.0 - smoothstep(0.04, 0.12, abs(length(relative) - radius))) * 0.5;
        color = lerp(color, float3(0.97, 0.85, 0.57), hullEdge);
        color = lerp(color, hullColor, hull);
    }
    return float4(color, 1.0);
}
