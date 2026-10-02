// Focused host-side unit tests for the ocean playground logic.
//
// These cover the three areas the acceptance criteria call out explicitly:
//   * settings validation (bounds, finiteness, JSON round-trip, friendly errors)
//   * coordinate mapping (aspect-correct, resize-stable screen->world)
//   * current/time continuity (continuous integration, resume clamp, freeze)
//
// They are GPU-free and SDK-free: they compile and run on any host with a C++23
// compiler, so appearance is NOT asserted here (that must be verified
// interactively; mocks cannot prove visuals). A tiny assert harness avoids a
// test-framework dependency and keeps the standalone build trivial.

#include "ocean/ocean_clock.h"
#include "ocean/ocean_settings.h"
#include "ocean/ocean_uniforms.h"

#include <cmath>
#include <cstdio>
#include <string>

namespace
{
int gFailures = 0;
int gChecks = 0;

void Check(bool condition, const char* expr, const char* file, int line) noexcept
{
    ++gChecks;
    if (!condition)
    {
        ++gFailures;
        std::printf("FAIL  %s:%d  %s\n", file, line, expr);
    }
}

#define CHECK(cond) Check((cond), #cond, __FILE__, __LINE__)

bool ApproxEq(double a, double b, double eps = 1e-5) noexcept
{
    return std::fabs(a - b) <= eps;
}

using namespace ludus::sandbox::ocean;

// --- Settings validation ----------------------------------------------------

void TestPresetsAreValid()
{
    for (auto p : {Preset::Calm, Preset::Drifting, Preset::Choppy})
    {
        const OceanSettings s = MakePreset(p);
        const ValidationResult v = Validate(s);
        CHECK(v.Ok);
        CHECK(v.Message.empty());
    }
    // Presets are ordered Calm < Drifting < Choppy in energy.
    CHECK(MakePreset(Preset::Calm).WaveIntensity < MakePreset(Preset::Drifting).WaveIntensity);
    CHECK(MakePreset(Preset::Drifting).WaveIntensity < MakePreset(Preset::Choppy).WaveIntensity);
    CHECK(MakePreset(Preset::Calm).CurrentSpeedMetersPerSecond <
          MakePreset(Preset::Choppy).CurrentSpeedMetersPerSecond);
}

void TestClampSanitizesNonFiniteAndOutOfRange()
{
    OceanSettings s = DefaultSettings();
    s.WaveIntensity = std::nanf("");
    s.FoamAmount = 999.0F;
    s.CurrentSpeedMetersPerSecond = -5.0F;
    s.CurrentDirectionDegrees = 450.0F; // wraps to 90
    s.DeepColor.R = INFINITY;
    const OceanSettings c = Clamp(s);
    CHECK(std::isfinite(c.WaveIntensity));
    CHECK(c.WaveIntensity >= kWaveIntensityBounds.Min && c.WaveIntensity <= kWaveIntensityBounds.Max);
    CHECK(ApproxEq(c.FoamAmount, kFoamAmountBounds.Max));
    CHECK(ApproxEq(c.CurrentSpeedMetersPerSecond, kCurrentSpeedBounds.Min));
    CHECK(ApproxEq(c.CurrentDirectionDegrees, 90.0));
    CHECK(std::isfinite(c.DeepColor.R));
    CHECK(Validate(c).Ok);
}

void TestValidateRejectsOutOfRangeWithMessage()
{
    OceanSettings s = DefaultSettings();
    s.WaveScaleMeters = 1000.0F; // above max
    const ValidationResult v = Validate(s);
    CHECK(!v.Ok);
    CHECK(!v.Message.empty());
    CHECK(v.Message.find("waveScale") != std::string::npos);
}

void TestJsonRoundTrip()
{
    const OceanSettings original = MakePreset(Preset::Choppy);
    const std::string json = ToJson(original);
    const ParseResult parsed = FromJson(json);
    CHECK(parsed.Ok);
    CHECK(parsed.Message.empty());
    CHECK(ApproxEq(parsed.Value.CurrentDirectionDegrees, original.CurrentDirectionDegrees, 1e-3));
    CHECK(ApproxEq(parsed.Value.CurrentSpeedMetersPerSecond, original.CurrentSpeedMetersPerSecond, 1e-3));
    CHECK(ApproxEq(parsed.Value.WaveScaleMeters, original.WaveScaleMeters, 1e-3));
    CHECK(ApproxEq(parsed.Value.WaveAnimationSpeed, original.WaveAnimationSpeed, 1e-3));
    CHECK(ApproxEq(parsed.Value.WaveIntensity, original.WaveIntensity, 1e-3));
    CHECK(ApproxEq(parsed.Value.FoamAmount, original.FoamAmount, 1e-3));
    CHECK(ApproxEq(parsed.Value.FoamColor.R, original.FoamColor.R, 1e-3));
    CHECK(ApproxEq(parsed.Value.DeepColor.B, original.DeepColor.B, 1e-3));
}

void TestJsonRejectsInvalidInput()
{
    // Not an object.
    CHECK(!FromJson("[]").Ok);
    // Missing schemaVersion.
    CHECK(!FromJson("{\"currentSpeed\": 1.0}").Ok);
    // Wrong schema version.
    CHECK(!FromJson("{\"schemaVersion\": 99, \"currentSpeed\": 1.0}").Ok);
    // Out-of-range value yields a friendly, field-named message.
    const ParseResult r = FromJson("{\"schemaVersion\": 1, \"waveIntensity\": 5.0}");
    CHECK(!r.Ok);
    CHECK(r.Message.find("waveIntensity") != std::string::npos);
    // NaN / Infinity are not valid JSON numbers -> rejected, not accepted.
    CHECK(!FromJson("{\"schemaVersion\": 1, \"foamAmount\": NaN}").Ok);
    // Unknown field rejected with its name.
    const ParseResult u = FromJson("{\"schemaVersion\": 1, \"bogus\": 1}");
    CHECK(!u.Ok);
    CHECK(u.Message.find("bogus") != std::string::npos);
    // Malformed (trailing garbage).
    CHECK(!FromJson("{\"schemaVersion\": 1, \"foamAmount\": 0.5} trailing").Ok);
}

void TestJsonPartialObjectFillsDefaults()
{
    const ParseResult r = FromJson("{\"schemaVersion\": 1, \"foamAmount\": 0.5}");
    CHECK(r.Ok);
    CHECK(ApproxEq(r.Value.FoamAmount, 0.5, 1e-3));
    // Unspecified fields take defaults.
    CHECK(ApproxEq(r.Value.WaveScaleMeters, DefaultSettings().WaveScaleMeters, 1e-3));
}

// --- Coordinate mapping ------------------------------------------------------

void TestScreenToWorldCenterIsOrigin()
{
    const Vec2 c = ScreenToWorld(960.0F, 540.0F, 1920, 1080);
    CHECK(ApproxEq(c.X, 0.0, 1e-3));
    CHECK(ApproxEq(c.Y, 0.0, 1e-3));
}

void TestScreenToWorldVerticalExtentFixed()
{
    // Top edge maps to +half view height, bottom edge to -half, on any aspect.
    for (auto [w, h] : {std::pair<uint32, uint32>{1920, 1080}, {1080, 1920}, {800, 800}})
    {
        const Vec2 top = ScreenToWorld(static_cast<float>(w) * 0.5F, 0.0F, w, h);
        const Vec2 bottom = ScreenToWorld(static_cast<float>(w) * 0.5F, static_cast<float>(h), w, h);
        CHECK(ApproxEq(top.Y, kWorldViewHeightMeters * 0.5, 1e-2));
        CHECK(ApproxEq(bottom.Y, -kWorldViewHeightMeters * 0.5, 1e-2));
    }
}

void TestScreenToWorldAspectCorrect()
{
    // metersPerPixel must be identical on both axes (no stretching). A pixel
    // step of +1 in X and +1 in Y produces equal world magnitudes.
    const uint32 w = 1600, h = 900;
    const Vec2 a = ScreenToWorld(100.0F, 100.0F, w, h);
    const Vec2 bx = ScreenToWorld(101.0F, 100.0F, w, h);
    const Vec2 by = ScreenToWorld(100.0F, 101.0F, w, h);
    const double dx = std::fabs(bx.X - a.X);
    const double dy = std::fabs(by.Y - a.Y);
    // Equal within float32 rounding: both equal metersPerPixel = viewH/H.
    const double metersPerPixel = static_cast<double>(kWorldViewHeightMeters) / static_cast<double>(h);
    CHECK(ApproxEq(dx, metersPerPixel, 1e-4));
    CHECK(ApproxEq(dy, metersPerPixel, 1e-4));
}

void TestWorldViewWidthScalesWithAspect()
{
    CHECK(ApproxEq(WorldViewWidthMeters(1920, 1080), kWorldViewHeightMeters * (1920.0 / 1080.0), 1e-2));
    CHECK(ApproxEq(WorldViewWidthMeters(1080, 1920), kWorldViewHeightMeters * (1080.0 / 1920.0), 1e-2));
    // Degenerate inputs fall back to the fixed vertical extent.
    CHECK(ApproxEq(WorldViewWidthMeters(0, 0), kWorldViewHeightMeters, 1e-6));
}

void TestWaveScaleStableAcrossResize()
{
    // The world extent a wave occupies must not change when only the pixel
    // resolution changes at a fixed aspect ratio. Build uniforms at two DPRs.
    const OceanSettings s = DefaultSettings();
    const SceneClock clock{};
    const OceanUniforms low = BuildUniforms(s, clock, 1280, 720);
    const OceanUniforms high = BuildUniforms(s, clock, 2560, 1440); // 2x DPR, same aspect
    CHECK(ApproxEq(low.WorldView[0], high.WorldView[0], 1e-3));
    CHECK(ApproxEq(low.WorldView[1], high.WorldView[1], 1e-3));
    CHECK(ApproxEq(low.WaveScaleMeters, high.WaveScaleMeters, 1e-6));
}

// --- Current / time continuity ----------------------------------------------

void TestSanitizeDeltaClampsResume()
{
    CHECK(ApproxEq(SanitizeDelta(-1.0), 0.0));
    CHECK(ApproxEq(SanitizeDelta(0.016), 0.016));
    CHECK(ApproxEq(SanitizeDelta(10.0), kMaxResumeDeltaSeconds)); // long stall clamped
    CHECK(ApproxEq(SanitizeDelta(std::nan("")), 0.0));
    // Non-finite (including +Inf) is treated as "no information" -> freeze (0),
    // the safest choice; it must never produce a huge jump.
    CHECK(ApproxEq(SanitizeDelta(INFINITY), 0.0));
}

void TestCurrentIntegratesContinuously()
{
    OceanSettings s = DefaultSettings();
    s.CurrentDirectionDegrees = 0.0F; // +X
    s.CurrentSpeedMetersPerSecond = 2.0F;
    SceneClock clock{};
    // 100 steps of 0.01s = 1.0s total -> ~2.0 meters east.
    for (int i = 0; i < 100; ++i)
    {
        Advance(clock, s, 0.01, true);
    }
    CHECK(ApproxEq(clock.CurrentOffsetX, 2.0, 1e-3));
    CHECK(ApproxEq(clock.CurrentOffsetY, 0.0, 1e-6));
}

void TestChangingDirectionDoesNotTeleport()
{
    OceanSettings s = DefaultSettings();
    s.CurrentDirectionDegrees = 0.0F;
    s.CurrentSpeedMetersPerSecond = 3.0F;
    SceneClock clock{};
    for (int i = 0; i < 50; ++i)
    {
        Advance(clock, s, 0.01, true); // 0.5s east
    }
    const double xAfterEast = clock.CurrentOffsetX;
    const double yAfterEast = clock.CurrentOffsetY;
    CHECK(xAfterEast > 1.0);
    // Flip to north; the ALREADY-accumulated offset must be preserved exactly.
    s.CurrentDirectionDegrees = 90.0F;
    Advance(clock, s, 0.01, true);
    CHECK(clock.CurrentOffsetX >= xAfterEast - 1e-9);        // X did not jump back
    CHECK(ApproxEq(clock.CurrentOffsetX, xAfterEast, 1e-3)); // no X advance going north
    CHECK(clock.CurrentOffsetY > yAfterEast);                // now moving north
}

void TestFrozenWhileHiddenAndPaused()
{
    OceanSettings s = DefaultSettings();
    s.CurrentSpeedMetersPerSecond = 5.0F;
    SceneClock clock{};
    const double x0 = clock.CurrentOffsetX;
    const double t0 = clock.WaveTime;
    // Hidden -> no advance regardless of delta.
    Advance(clock, s, 0.1, false);
    CHECK(ApproxEq(clock.CurrentOffsetX, x0));
    CHECK(ApproxEq(clock.WaveTime, t0));
    // Paused -> no advance even when visible.
    clock.Paused = true;
    Advance(clock, s, 0.1, true);
    CHECK(ApproxEq(clock.CurrentOffsetX, x0));
    CHECK(ApproxEq(clock.WaveTime, t0));
    // Resume -> advances again.
    clock.Paused = false;
    Advance(clock, s, 0.1, true);
    CHECK(clock.WaveTime > t0);
}

void TestLongSessionPrecisionStaysBounded()
{
    OceanSettings s = DefaultSettings();
    s.CurrentSpeedMetersPerSecond = 4.0F;
    s.WaveAnimationSpeed = 2.0F;
    SceneClock clock{};
    // Simulate ~3 hours at 60fps worth of ticks (clamped delta each).
    for (int i = 0; i < 60 * 60 * 60; ++i)
    {
        Advance(clock, s, 1.0 / 60.0, true);
    }
    // Wrapped accumulators remain finite and inside their wrap windows, so a
    // float32 upload keeps sub-frame precision indefinitely.
    CHECK(std::isfinite(clock.WaveTime));
    CHECK(clock.WaveTime >= 0.0 && clock.WaveTime < kWavePhaseWrap);
    CHECK(std::isfinite(clock.CurrentOffsetX));
    CHECK(clock.CurrentOffsetX >= 0.0 && clock.CurrentOffsetX < kCurrentWrapMeters);
}

void TestResetTime()
{
    OceanSettings s = DefaultSettings();
    SceneClock clock{};
    for (int i = 0; i < 10; ++i)
    {
        Advance(clock, s, 0.05, true);
    }
    clock.Paused = true;
    ResetTime(clock);
    CHECK(ApproxEq(clock.WaveTime, 0.0));
    CHECK(ApproxEq(clock.CurrentOffsetX, 0.0));
    CHECK(ApproxEq(clock.CurrentOffsetY, 0.0));
    CHECK(clock.Paused); // reset keeps pause state
}

} // namespace

int main()
{
    TestPresetsAreValid();
    TestClampSanitizesNonFiniteAndOutOfRange();
    TestValidateRejectsOutOfRangeWithMessage();
    TestJsonRoundTrip();
    TestJsonRejectsInvalidInput();
    TestJsonPartialObjectFillsDefaults();

    TestScreenToWorldCenterIsOrigin();
    TestScreenToWorldVerticalExtentFixed();
    TestScreenToWorldAspectCorrect();
    TestWorldViewWidthScalesWithAspect();
    TestWaveScaleStableAcrossResize();

    TestSanitizeDeltaClampsResume();
    TestCurrentIntegratesContinuously();
    TestChangingDirectionDoesNotTeleport();
    TestFrozenWhileHiddenAndPaused();
    TestLongSessionPrecisionStaysBounded();
    TestResetTime();

    std::printf("\n%d checks, %d failures\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
