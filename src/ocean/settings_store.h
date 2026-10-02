#pragma once

// The explicit bridge between the UI (DOM panel on web, config/CLI on native)
// and the scene's settings. This is the ONLY surface the application exposes to
// controls; the engine APIs stay unaware of it.
//
// Thread model: single main thread. The web bridge mutates the store from DOM
// event handlers that run on the same thread as the render loop (between
// frames), so no locking is required. Keeping this ownership explicit is what
// stops UI interaction from racing or accidentally corrupting scene state.
//
// The store always holds a sanitized (clamped, finite) settings value, so the
// scene can read it every frame without re-validating.

#include "ocean/ocean_settings.h"

#include <string>
#include <string_view>

namespace ludus::sandbox::ocean
{
class SettingsStore final
{
public:
    SettingsStore() noexcept : mSettings(DefaultSettings()) {}
    explicit SettingsStore(const OceanSettings& initial) noexcept : mSettings(Clamp(initial)) {}

    [[nodiscard]] const OceanSettings& Get() const noexcept
    {
        return mSettings;
    }

    // Replace the whole settings value (sanitized on the way in).
    void Set(const OceanSettings& settings) noexcept
    {
        mSettings = Clamp(settings);
        ++mRevision;
    }

    void ApplyPreset(Preset preset) noexcept
    {
        Set(MakePreset(preset));
    }

    void Reset() noexcept
    {
        Set(DefaultSettings());
    }

    // Individual field setters used by DOM sliders / native CLI. Each sanitizes
    // through Clamp() so an out-of-range widget value can never reach the GPU.
    void SetCurrentDirectionDegrees(float32 v) noexcept
    {
        OceanSettings s = mSettings;
        s.CurrentDirectionDegrees = v;
        Set(s);
    }
    void SetCurrentSpeed(float32 v) noexcept
    {
        OceanSettings s = mSettings;
        s.CurrentSpeedMetersPerSecond = v;
        Set(s);
    }
    void SetWaveScale(float32 v) noexcept
    {
        OceanSettings s = mSettings;
        s.WaveScaleMeters = v;
        Set(s);
    }
    void SetWaveAnimationSpeed(float32 v) noexcept
    {
        OceanSettings s = mSettings;
        s.WaveAnimationSpeed = v;
        Set(s);
    }
    void SetWaveIntensity(float32 v) noexcept
    {
        OceanSettings s = mSettings;
        s.WaveIntensity = v;
        Set(s);
    }
    void SetFoamAmount(float32 v) noexcept
    {
        OceanSettings s = mSettings;
        s.FoamAmount = v;
        Set(s);
    }
    void SetDeepColor(const Color& c) noexcept
    {
        OceanSettings s = mSettings;
        s.DeepColor = c;
        Set(s);
    }
    void SetMidColor(const Color& c) noexcept
    {
        OceanSettings s = mSettings;
        s.MidColor = c;
        Set(s);
    }
    void SetShallowColor(const Color& c) noexcept
    {
        OceanSettings s = mSettings;
        s.ShallowColor = c;
        Set(s);
    }
    void SetFoamColor(const Color& c) noexcept
    {
        OceanSettings s = mSettings;
        s.FoamColor = c;
        Set(s);
    }

    // Export current settings as versioned JSON.
    [[nodiscard]] std::string ExportJson() const noexcept
    {
        return ToJson(mSettings);
    }

    // Import settings from JSON. On success, applies them and returns an Ok
    // result; on failure, leaves the current settings untouched and returns a
    // friendly error message for the UI to show.
    [[nodiscard]] ParseResult ImportJson(std::string_view json) noexcept
    {
        ParseResult r = FromJson(json);
        if (r.Ok)
        {
            Set(r.Value);
        }
        return r;
    }

    // Monotonic revision; UI can detect external changes (e.g. preset applied).
    [[nodiscard]] uint32 Revision() const noexcept
    {
        return mRevision;
    }

private:
    OceanSettings mSettings;
    uint32 mRevision = 0;
};
} // namespace ludus::sandbox::ocean
