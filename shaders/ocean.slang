// Procedural top-down stylized ocean for the "Drift" game jam theme.
//
// One fullscreen pass. No textures, no water physics, no horizon, no perspective.
// Everything is authored from the single uniform block below. Only f32
// scalar/vector arithmetic, uniform fields, SV_VulkanVertexID, SV_Position and a
// location-0 RGBA output are used (the engine's validated shader subset; see
// fullscreen-rendering-handoff.md).
//
// The CPU mirror of this layout is src/ocean/ocean_uniforms.h; the two must
// agree (offsets asserted on the C++ side). set/group 0, binding 0, 736 bytes.

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

};

[[vk::binding(0, 0)]] ConstantBuffer<OceanUniforms> ocean;

float4 rippleAt(int index)
{
    switch (index)
    {
        case 0: return ocean.ripple0;
        case 1: return ocean.ripple1;
        case 2: return ocean.ripple2;
        case 3: return ocean.ripple3;
        case 4: return ocean.ripple4;
        case 5: return ocean.ripple5;
        case 6: return ocean.ripple6;
        case 7: return ocean.ripple7;
        case 8: return ocean.ripple8;
        case 9: return ocean.ripple9;
        case 10: return ocean.ripple10;
        case 11: return ocean.ripple11;
        case 12: return ocean.ripple12;
        case 13: return ocean.ripple13;
        case 14: return ocean.ripple14;
        default: return ocean.ripple15;
    }
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

// Two octaves only: broad, slowly varying patches (no dense noise).
float broadField(float2 p)
{
    return valueNoise(p) * 0.65 + valueNoise(p * 2.03 + 7.1) * 0.35;
}

// A directional sinusoidal wave band.
float waveBand(float2 world, float2 dir, float wavelength, float phase)
{
    float k = 6.2831853 / max(wavelength, 0.001);
    return sin(dot(world, dir) * k + phase);
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

    // Bulk current advects the whole color field. Sampling the field at
    // (world - currentOffset) makes the patches drift continuously; changing
    // speed/direction never teleports what already accumulated (that lives in
    // currentOffset, integrated on the CPU).
    float2 advected = world - ocean.currentOffset;

    // Broad, slowly advecting color patches (large scale, low frequency). A
    // second, differently-scaled sample breaks any visible lattice regularity.
    float patchScale = ocean.waveScaleMeters * 2.6;
    float patches = broadField(advected / patchScale + float2(ocean.waveTime * 0.015, -ocean.waveTime * 0.011));
    patches = patches * 0.7 +
              broadField(advected / (patchScale * 2.7) + float2(-ocean.waveTime * 0.006, ocean.waveTime * 0.009)) * 0.3;

    // Two overlapping wave patterns moving in DIFFERENT directions, each the sum
    // of two slightly detuned bands so crests read as flowing lines rather than
    // a regular dot grid.
    float2 dirA = float2(ocean.currentDirCos, ocean.currentDirSin);
    float ca = 0.5736; // cos(55deg)
    float sa = 0.8192; // sin(55deg)
    float2 dirB = float2(dirA.x * ca - dirA.y * sa, dirA.x * sa + dirA.y * ca);

    float t = ocean.waveTime;
    float wavesA = waveBand(world, dirA, ocean.waveScaleMeters, t * 1.7) * 0.65 +
                   waveBand(world, dirA, ocean.waveScaleMeters * 1.9, t * 1.1 + 1.3) * 0.35;
    float wavesB = waveBand(world, dirB, ocean.waveScaleMeters * 0.63, t * 1.3) * 0.65 +
                   waveBand(world, dirB, ocean.waveScaleMeters * 1.37, t * 0.9 + 2.1) * 0.35;
    // Combine; keep it graphic (shaped), scaled by intensity.
    float waves = (wavesA * 0.6 + wavesB * 0.4);
    float waveShade = waves * (0.2 + 0.45 * ocean.waveIntensity);

    // Height-ish signal that drives palette banding: patches + a bit of wave.
    float level = clamp(patches + waveShade * 0.5, 0.0, 1.0);

    // Palette: deep navy -> ocean blue -> teal, as broad bands. A touch of
    // contrast curve keeps the illustrated, posterized-but-smooth look.
    float3 deep = ocean.deepColor.rgb;
    float3 mid = ocean.midColor.rgb;
    float3 shallow = ocean.shallowColor.rgb;
    float banded = smoothstep(0.08, 0.92, level);
    float3 color;
    if (banded < 0.5)
    {
        color = lerp(deep, mid, smoothstep(0.0, 0.5, banded));
    }
    else
    {
        color = lerp(mid, shallow, smoothstep(0.5, 1.0, banded));
    }

    // Apply wave shading as light/dark on top of the band color.
    color += waveShade * 0.14;

    // Sparse BROKEN foam STREAKS along the crest lines of wave A, gated by a
    // broad mask so foam appears as scattered dashes rather than dense sparkle
    // or isolated dots. The crest term uses the band value (a line), not the
    // product of two bands (which made a dot grid).
    float crestLine = smoothstep(0.80, 0.99, wavesA * 0.5 + 0.5);
    float breakUp = broadField(advected / (ocean.waveScaleMeters * 0.5) + float2(-t * 0.05, t * 0.04));
    float streak = crestLine * smoothstep(0.45, 0.75, breakUp); // break the line into dashes
    float foamMask = broadField(advected / (ocean.waveScaleMeters * 2.0) + float2(t * 0.02, -t * 0.015));
    float foamGate = smoothstep(1.0 - ocean.foamAmount * 1.1, 1.0, foamMask + ocean.foamAmount * 0.35);
    float foam = clamp(streak * foamGate, 0.0, 1.0);
    color = lerp(color, ocean.foamColor.rgb, foam * 0.9);

    // Gentle vignette keeps contrast for future boats/debris at the center.
    float2 c = ndc - 0.5;
    float vignette = 1.0 - dot(c, c) * 0.3;
    color *= vignette;

    if (ocean.gameInfo.x > 0.5)
    {
        float2 extent = ocean.levelBounds.xy;
        float outside = step(extent.x, abs(world.x)) + step(extent.y, abs(world.y));
        color = lerp(color, float3(0.025, 0.045, 0.075), saturate(outside) * 0.75);
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
            if (float(i) >= ocean.gameInfo.y) break;
            float4 ring = rippleAt(i);
            float distance = length(world - ring.xy);
            float band = abs(distance - ring.z);
            float outline = 1.0 - smoothstep(0.15, 0.35, band);
            float trail = (1.0 - smoothstep(0.35, 1.0, band)) * 0.2;
            color = lerp(color, float3(0.76, 0.96, 0.95), (outline + trail) * ring.w * 0.85);
            float origin = (1.0 - smoothstep(0.15, 0.45, distance)) * ring.w;
            color = lerp(color, float3(0.90, 0.98, 0.91), origin);
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
