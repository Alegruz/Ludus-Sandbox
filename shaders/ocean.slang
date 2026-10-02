// Procedural top-down stylized ocean for the "Drift" game jam theme.
//
// One fullscreen pass. No textures, no water physics, no horizon, no perspective.
// Everything is authored from the single uniform block below. Only f32
// scalar/vector arithmetic, uniform fields, SV_VertexID, SV_Position and a
// location-0 RGBA output are used (the engine's validated shader subset; see
// fullscreen-rendering-handoff.md).
//
// The CPU mirror of this layout is src/ocean/ocean_uniforms.h; the two must
// agree (offsets asserted on the C++ side). set/group 0, binding 0, 128 bytes.

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
};

[[vk::binding(0, 0)]] ConstantBuffer<OceanUniforms> ocean;

[shader("vertex")]
float4 vertexMain(uint id : SV_VertexID) : SV_Position
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

    return float4(color, 1.0);
}
