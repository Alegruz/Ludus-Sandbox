// Procedural top-down stylized ocean for the "Drift" game jam theme.
//
// One fullscreen pass. Analytic surface waves and bounded game-owned currents.
// Everything is authored from the single uniform block below. Only f32
// scalar/vector arithmetic, uniform fields, SV_VulkanVertexID, SV_Position and a
// location-0 RGBA output are used (the engine's validated shader subset; see
// fullscreen-rendering-handoff.md).
//
// The CPU mirror of this layout is src/ocean/ocean_uniforms.h; the two must
// agree (offsets asserted on the C++ side). set/group 0, binding 0, 1024 bytes.

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
    float4 surfaceInfo; // active effect count
    float4 surface0a; // origin.xy, age, signed strength
    float4 surface0b; // wave direction.xy OR vortex decay age/reserved; radius; kind
    float4 surface1a;
    float4 surface1b;
    float4 surface2a;
    float4 surface2b;
    float4 surface3a;
    float4 surface3b;
    float4 surface4a;
    float4 surface4b;
    float4 surface5a;
    float4 surface5b;
    float4 surface6a;
    float4 surface6b;
    float4 surface7a;
    float4 surface7b;
    float4 boatMotion; // velocity.xy, speed, reserved

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

// Named vec4 fields keep the validated WebGL std140 reflection subset.
float4 surfaceAt(int index, bool shape)
{
    switch (index)
    {
        case 0: return shape ? ocean.surface0b : ocean.surface0a;
        case 1: return shape ? ocean.surface1b : ocean.surface1a;
        case 2: return shape ? ocean.surface2b : ocean.surface2a;
        case 3: return shape ? ocean.surface3b : ocean.surface3a;
        case 4: return shape ? ocean.surface4b : ocean.surface4a;
        case 5: return shape ? ocean.surface5b : ocean.surface5a;
        case 6: return shape ? ocean.surface6b : ocean.surface6a;
        default: return shape ? ocean.surface7b : ocean.surface7a;
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

    float2 surfaceWorld = world;
    float2 interactionSlope = float2(0.0, 0.0);
    float interactionHeight = 0.0;
    float interactionFoam = 0.0;
    float pixelWidth = max(metersPerPixel, 0.035);
    // These definitions share speed, footprint, age and damping with WaterAt.
    // Waves move in the swipe direction; vortices rotate in the drawn direction.
    for (int i = 0; i < 8; ++i)
    {
        if (float(i) >= ocean.surfaceInfo.x) break;
        float4 effect = surfaceAt(i, false);
        float4 shape = surfaceAt(i, true);
        float2 relative = world - effect.xy;
        float radius = max(shape.z, 1.0);
        if (shape.w > 1.5)
        {
            float distance = length(relative);
            float envelope = max(1.0 - dot(relative, relative) / (radius * radius), 0.0);
            float fade = pow(max(1.0 - shape.x / 5.0, 0.0), 2.0);
            float weight = abs(effect.w) * fade * envelope * envelope;
            float angle = -effect.w * effect.z * 1.6 * fade * envelope * envelope;
            float ca = cos(angle), sa = sin(angle);
            surfaceWorld += float2(relative.x * ca - relative.y * sa,
                                   relative.x * sa + relative.y * ca) - relative;
            interactionHeight -= weight * 1.6;
            interactionSlope += relative * (6.4 * abs(effect.w) * fade * envelope / (radius * radius));
            float spiral = sin(atan2(relative.y, relative.x) * 3.0 +
                               log(max(distance / radius, 0.08)) * 10.0 - effect.z * effect.w * 3.0);
            float line = smoothstep(0.75, 0.98, spiral);
            float broken = 0.55 + 0.45 * valueNoise(relative * 0.5);
            interactionFoam += line * weight * broken * smoothstep(0.5, 2.0, distance) * 0.8;
        }
        else
        {
            float along = dot(relative, shape.xy);
            float across = dot(relative, float2(-shape.y, shape.x));
            float front = (along - effect.z * 12.0) / 2.0;
            float envelope = max(1.0 - across * across / (radius * radius), 0.0);
            float fade = pow(max(1.0 - effect.z / 3.0, 0.0), 2.0);
            float weight = effect.w * fade * envelope * envelope;
            float crest = max(1.0 - front * front, 0.0);
            interactionHeight += crest * crest * weight * 1.5;
            if (abs(front) < 1.0)
            {
                interactionSlope -= shape.xy * (3.0 * front * crest * weight);
            }
            // A broad whitecap travels with the physical crest, then leaves
            // smaller broken foam behind it instead of a cursor-shaped mark.
            float whitecap = 1.0 - smoothstep(0.18, 0.45 + pixelWidth, abs(front));
            float trailing = (1.0 - smoothstep(0.0, 4.0, abs(front + 1.5))) *
                             smoothstep(0.1, 0.8, -front) * 0.15;
            float breakup = 0.6 + 0.4 * valueNoise(relative * 0.45);
            interactionFoam += (whitecap + trailing) * weight * breakup;
        }
    }
    for (int i = 0; i < 16; ++i)
    {
        if (float(i) >= ocean.gameInfo.y) break;
        float4 ring = rippleAt(i);
        float2 relative = world - ring.xy;
        float distance = length(relative);
        float band = distance - ring.z;
        float envelope = 1.0 - smoothstep(0.8, 2.4, abs(band));
        float height = cos(band * 2.8) * envelope * ring.w * 0.5;
        interactionHeight += height;
        interactionSlope -= relative / max(distance, 0.1) * sin(band * 2.8) * envelope * ring.w * 1.4;
        interactionFoam += (1.0 - smoothstep(0.08, 0.3 + pixelWidth, abs(band))) * ring.w * 0.55;
    }

    float2 advected = surfaceWorld - ocean.currentOffset;
    float patchScale = ocean.waveScaleMeters * 2.6;
    float patches = broadField(advected / patchScale + float2(ocean.waveTime * 0.015, -ocean.waveTime * 0.011));
    patches = patches * 0.7 +
              broadField(advected / (patchScale * 2.7) + float2(-ocean.waveTime * 0.006, ocean.waveTime * 0.009)) * 0.3;

    float2 dirA = float2(ocean.currentDirCos, ocean.currentDirSin);
    float2 dirB = float2(dirA.x * 0.5736 - dirA.y * 0.8192, dirA.x * 0.8192 + dirA.y * 0.5736);
    float2 dirC = float2(-0.8, 0.6);
    float t = ocean.waveTime;
    float k = 6.2831853 / max(ocean.waveScaleMeters, 0.001);
    // Long swells and shorter crossing waves have distinct phase velocities.
    // Height derivatives supply lighting; a moving pattern is never boat velocity.
    float phaseA = dot(surfaceWorld, dirA) * k - t * 1.7;
    float phaseB = dot(surfaceWorld, dirB) * k * 1.59 - t * 2.1;
    float phaseC = dot(surfaceWorld, dirC) * k * 3.1 - t * 2.8;
    float2 tangentA = float2(-dirA.y, dirA.x);
    float bending = dot(surfaceWorld, tangentA) * k * 0.65 - t * 0.4;
    phaseA += sin(bending) * 0.45;
    float wavesA = sin(phaseA);
    float wavesB = sin(phaseB);
    float waves = wavesA * 0.58 + wavesB * 0.30 + sin(phaseC) * 0.12;
    float waveShade = waves * (0.16 + 0.35 * ocean.waveIntensity);
    float amplitude = 0.3 + 1.4 * ocean.waveIntensity;
    float2 slope = ((dirA + tangentA * cos(bending) * 0.2925) * cos(phaseA) * 0.58 + dirB * cos(phaseB) * 0.477 +
                    dirC * cos(phaseC) * 0.372) * k * amplitude + interactionSlope;
    float3 normal = normalize(float3(-slope, 1.0));
    float diffuse = dot(normal, normalize(float3(-0.4, 0.5, 0.85)));
    float reflection = pow(max(dot(normal, normalize(float3(0.25, 0.35, 1.0))), 0.0), 24.0);
    float level = clamp(patches + waveShade * 0.5 + interactionHeight * 0.08, 0.0, 1.0);

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
    color *= 0.68 + diffuse * 0.4;
    color += waveShade * 0.09;
    color += float3(0.36, 0.64, 0.70) * reflection * (0.06 + 0.12 * ocean.waveIntensity);
    // Thin curved ridges and crossing wind ripples keep the water readable
    // between sparse whitecaps. Their contrast follows the wave setting.
    float crestLight = pow(wavesA * 0.5 + 0.5, 18.0);
    float windRipple = pow(sin(phaseC) * 0.5 + 0.5, 10.0);
    float ridgeBreak = 0.35 + 0.65 * valueNoise(advected / (ocean.waveScaleMeters * 0.35));
    color += float3(0.24, 0.52, 0.56) * (crestLight * 0.26 + windRipple * 0.10) *
             ridgeBreak * ocean.waveIntensity;

    // Sparse BROKEN foam STREAKS along the crest lines of wave A, gated by a
    // broad mask so foam appears as scattered dashes rather than dense sparkle
    // or isolated dots. The crest term uses the band value (a line), not the
    // product of two bands (which made a dot grid).
    float crestLine = smoothstep(0.90 - ocean.waveIntensity * 0.12, 0.99, wavesA * 0.5 + 0.5);
    float breakUp = broadField(advected / (ocean.waveScaleMeters * 0.5) + float2(-t * 0.05, t * 0.04));
    float streak = crestLine * smoothstep(0.45, 0.75, breakUp); // break the line into dashes
    float foamMask = broadField(advected / (ocean.waveScaleMeters * 2.0) + float2(t * 0.02, -t * 0.015));
    float foamGate = smoothstep(1.0 - ocean.foamAmount * 1.1, 1.0, foamMask + ocean.foamAmount * 0.35);
    float foam = clamp(streak * foamGate * ocean.waveIntensity + interactionFoam, 0.0, 1.0);
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
        float speed = ocean.boatMotion.z;
        float behind = -boat.x - radius * 0.7;
        float wakeLength = 3.0 + min(speed, 7.0) * 2.5;
        float wakeEnvelope = smoothstep(0.0, 1.0, behind) * (1.0 - smoothstep(1.0, wakeLength, behind));
        float wakeWidth = radius * 0.45 + max(behind, 0.0) * 0.24;
        float wakeLine = 1.0 - smoothstep(0.08, 0.3 + pixelWidth, abs(abs(boat.y) - wakeWidth));
        float wakeBreak = 0.55 + 0.45 * sin(behind * 2.0 - t * 4.0);
        float wake = wakeLine * wakeEnvelope * wakeBreak * min(speed / 2.5, 0.8);
        color = lerp(color, ocean.foamColor.rgb, wake);
        boat.y += sin(dot(ocean.boatInfo.xy, dirA) * k - t * 1.7) * ocean.waveIntensity * 0.15;
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
