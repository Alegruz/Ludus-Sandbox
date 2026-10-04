// Offline CPU preview of the original ambient ocean shader.
//
// This is NOT part of the game. It preserves the initial palette, two-direction
// waves and foam gating, using ocean::BuildUniforms for scene parameters. It
// predates the current lighting, interactive waves/currents and boat wake.
//
// Appearance must still be verified interactively on a GPU; this only lets us
// capture the authored look offline and sanity-check the composition.

#include "ocean/ocean_clock.h"
#include "ocean/ocean_settings.h"
#include "ocean/ocean_uniforms.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

using namespace ludus::sandbox::ocean;

namespace
{
struct V2
{
    float x, y;
};
struct V3
{
    float x, y, z;
};

float fract(float v)
{
    return v - std::floor(v);
}
float clampf(float v, float a, float b)
{
    return v < a ? a : (v > b ? b : v);
}
float mixf(float a, float b, float t)
{
    return a + (b - a) * t;
}
V3 mix3(V3 a, V3 b, float t)
{
    return {mixf(a.x, b.x, t), mixf(a.y, b.y, t), mixf(a.z, b.z, t)};
}
float smoothstepf(float e0, float e1, float x)
{
    float t = clampf((x - e0) / (e1 - e0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

float hash21(V2 p)
{
    p = {fract(p.x * 123.34f), fract(p.y * 456.21f)};
    float d = p.x * (p.x + 45.32f) + p.y * (p.y + 45.32f);
    p.x += d;
    p.y += d;
    return fract(p.x * p.y);
}
float valueNoise(V2 p)
{
    V2 i = {std::floor(p.x), std::floor(p.y)};
    V2 f = {p.x - i.x, p.y - i.y};
    V2 u = {f.x * f.x * (3.0f - 2.0f * f.x), f.y * f.y * (3.0f - 2.0f * f.y)};
    float a = hash21({i.x + 0.0f, i.y + 0.0f});
    float b = hash21({i.x + 1.0f, i.y + 0.0f});
    float c = hash21({i.x + 0.0f, i.y + 1.0f});
    float d = hash21({i.x + 1.0f, i.y + 1.0f});
    return mixf(mixf(a, b, u.x), mixf(c, d, u.x), u.y);
}
float broadField(V2 p)
{
    return valueNoise(p) * 0.65f + valueNoise({p.x * 2.03f + 7.1f, p.y * 2.03f + 7.1f}) * 0.35f;
}
float waveBand(V2 world, V2 dir, float wavelength, float phase)
{
    float k = 6.2831853f / std::max(wavelength, 0.001f);
    return std::sin((world.x * dir.x + world.y * dir.y) * k + phase);
}

V3 shade(const OceanUniforms& u, float px, float py)
{
    V2 res = {std::max(u.Resolution[0], 1.0f), std::max(u.Resolution[1], 1.0f)};
    V2 ndc = {px / res.x, py / res.y};
    float metersPerPixel = u.WorldView[1] / res.y;
    V2 world = {(px - res.x * 0.5f) * metersPerPixel, (res.y * 0.5f - py) * metersPerPixel};
    V2 advected = {world.x - u.CurrentOffset[0], world.y - u.CurrentOffset[1]};

    float patchScale = u.WaveScaleMeters * 2.6f;
    float patches =
        broadField({advected.x / patchScale + u.WaveTime * 0.015f, advected.y / patchScale - u.WaveTime * 0.011f});
    patches = patches * 0.7f + broadField({advected.x / (patchScale * 2.7f) - u.WaveTime * 0.006f,
                                           advected.y / (patchScale * 2.7f) + u.WaveTime * 0.009f}) *
                                   0.3f;

    V2 dirA = {u.CurrentDirCos, u.CurrentDirSin};
    float ca = 0.5736f, sa = 0.8192f;
    V2 dirB = {dirA.x * ca - dirA.y * sa, dirA.x * sa + dirA.y * ca};
    float t = u.WaveTime;
    float wavesA = waveBand(world, dirA, u.WaveScaleMeters, t * 1.7f) * 0.65f +
                   waveBand(world, dirA, u.WaveScaleMeters * 1.9f, t * 1.1f + 1.3f) * 0.35f;
    float wavesB = waveBand(world, dirB, u.WaveScaleMeters * 0.63f, t * 1.3f) * 0.65f +
                   waveBand(world, dirB, u.WaveScaleMeters * 1.37f, t * 0.9f + 2.1f) * 0.35f;
    float waves = wavesA * 0.6f + wavesB * 0.4f;
    float waveShade = waves * (0.2f + 0.45f * u.WaveIntensity);

    float level = clampf(patches + waveShade * 0.5f, 0.0f, 1.0f);
    V3 deep = {u.DeepColor[0], u.DeepColor[1], u.DeepColor[2]};
    V3 mid = {u.MidColor[0], u.MidColor[1], u.MidColor[2]};
    V3 shallow = {u.ShallowColor[0], u.ShallowColor[1], u.ShallowColor[2]};
    float banded = smoothstepf(0.08f, 0.92f, level);
    V3 color = banded < 0.5f ? mix3(deep, mid, smoothstepf(0.0f, 0.5f, banded))
                             : mix3(mid, shallow, smoothstepf(0.5f, 1.0f, banded));
    color.x += waveShade * 0.14f;
    color.y += waveShade * 0.14f;
    color.z += waveShade * 0.14f;

    float crestLine = smoothstepf(0.80f, 0.99f, wavesA * 0.5f + 0.5f);
    float breakUp = broadField(
        {advected.x / (u.WaveScaleMeters * 0.5f) - t * 0.05f, advected.y / (u.WaveScaleMeters * 0.5f) + t * 0.04f});
    float streak = crestLine * smoothstepf(0.45f, 0.75f, breakUp);
    float foamMask = broadField(
        {advected.x / (u.WaveScaleMeters * 2.0f) + t * 0.02f, advected.y / (u.WaveScaleMeters * 2.0f) - t * 0.015f});
    float foamGate = smoothstepf(1.0f - u.FoamAmount * 1.1f, 1.0f, foamMask + u.FoamAmount * 0.35f);
    float foam = clampf(streak * foamGate, 0.0f, 1.0f);
    V3 foamCol = {u.FoamColor[0], u.FoamColor[1], u.FoamColor[2]};
    color = mix3(color, foamCol, foam * 0.9f);

    V2 c = {ndc.x - 0.5f, ndc.y - 0.5f};
    float vignette = 1.0f - (c.x * c.x + c.y * c.y) * 0.3f;
    color.x *= vignette;
    color.y *= vignette;
    color.z *= vignette;
    return color;
}

void writePpm(const std::string& path, int w, int h, const std::vector<std::uint8_t>& rgb)
{
    FILE* f = std::fopen(path.c_str(), "wb");
    std::fprintf(f, "P6\n%d %d\n255\n", w, h);
    std::fwrite(rgb.data(), 1, rgb.size(), f);
    std::fclose(f);
}

void render(const OceanSettings& s, const SceneClock& clk, int w, int h, const std::string& path)
{
    OceanUniforms u = BuildUniforms(s, clk, static_cast<std::uint32_t>(w), static_cast<std::uint32_t>(h));
    std::vector<std::uint8_t> rgb(static_cast<std::size_t>(w) * h * 3);
    for (int y = 0; y < h; ++y)
    {
        for (int x = 0; x < w; ++x)
        {
            V3 c = shade(u, x + 0.5f, y + 0.5f);
            auto to8 = [](float v) { return static_cast<std::uint8_t>(clampf(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
            std::size_t i = (static_cast<std::size_t>(y) * w + x) * 3;
            rgb[i + 0] = to8(c.x);
            rgb[i + 1] = to8(c.y);
            rgb[i + 2] = to8(c.z);
        }
    }
    writePpm(path, w, h, rgb);
    std::printf("wrote %s (%dx%d)\n", path.c_str(), w, h);
}
} // namespace

int main(int argc, char** argv)
{
    const std::string outDir = argc > 1 ? argv[1] : ".";
    // Advance a clock to a representative moment so motion-dependent features
    // (advected patches, foam streaks) are visible.
    auto clockAt = [](const OceanSettings& s, double t) {
        SceneClock c{};
        for (int i = 0; i < static_cast<int>(t / 0.016); ++i)
        {
            Advance(c, s, 0.016, true);
        }
        return c;
    };

    const OceanSettings drifting = MakePreset(Preset::Drifting);
    const OceanSettings calm = MakePreset(Preset::Calm);
    const OceanSettings choppy = MakePreset(Preset::Choppy);

    render(drifting, clockAt(drifting, 6.0), 1920, 1080, outDir + "/ocean-drifting-1080p.ppm");
    render(calm, clockAt(calm, 6.0), 1920, 1080, outDir + "/ocean-calm-1080p.ppm");
    render(choppy, clockAt(choppy, 6.0), 1920, 1080, outDir + "/ocean-choppy-1080p.ppm");
    render(drifting, clockAt(drifting, 6.0), 1080, 1920, outDir + "/ocean-drifting-portrait.ppm");
    render(drifting, clockAt(drifting, 6.0), 1280, 720, outDir + "/ocean-drifting-720p.ppm");
    return 0;
}
