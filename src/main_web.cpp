// Web entry point and DOM bridge for the Drift ocean playground.
//
// The browser UI lives in accessible DOM controls (see web/shell.html) laid out
// around and over the canvas. This file is the SMALL EXPLICIT BRIDGE between
// those controls and the application settings: a handful of EMSCRIPTEN_KEEPALIVE
// C entry points the panel's JavaScript calls to push individual values, apply
// presets, import/export JSON, and pause/reset. Engine APIs never see any of
// this; the scene only reads the SettingsStore.
//
// Focus/input ownership: the DOM controls own pointer/keyboard focus while the
// user interacts with them. The canvas captures its own input only when focused.
// Because every bridge call runs on the main thread between animation frames,
// mutating settings never races the renderer.

#include "ocean/ocean_scene.h"
#include "ocean/ocean_settings.h"
#include "ocean/settings_store.h"

#include <ludus/foundation/logging/log.hpp>
#include <ludus/foundation/logging/log_system.hpp>

#include <emscripten.h>

#include <cstdlib>
#include <cstring>
#include <string>

namespace
{
namespace ocean = ludus::sandbox::ocean;

// The store is the single bridge object; the scene borrows it.
ocean::SettingsStore gStore;
ludus::sandbox::OceanScene gScene(gStore);

// Report scene state + frame count + current settings snapshot to the DOM so
// the panel can reflect presets/imports and show loading / error / restart
// state. States mirror SceneState. Error 3 == AdapterUnavailable (no WebGPU).
// clang-format off
EM_JS(void, PresentState, (int state, int error, unsigned frames), {
    const status = document.getElementById('status');
    if (!status) return;
    const names = ['stopped', 'loading', 'playing', 'failed', 'device-lost'];
    const messages = [
        'Stopped. Select Restart to play.',
        'Loading the ocean\u2026',
        'Drifting. Adjust the controls to shape the sea.',
        'The ocean could not start. Try Restart.',
        'Graphics connection lost. Select Restart to try again.'];
    let message = messages[state] || '';
    if (state === 3 && error === 3) {
        message = 'WebGPU is unavailable. Use a browser and GPU that support WebGPU, then Restart.';
    }
    if (status.textContent !== message) status.textContent = message;
    status.dataset.state = names[state] || 'unknown';
    status.dataset.frames = frames;
    status.dataset.error = error;
    if (globalThis.__oceanOnState) globalThis.__oceanOnState(state, error, frames);
});

// Pull the authoritative settings JSON into the DOM when it changes externally
// (preset/reset/import), so sliders reflect the real values.
EM_JS(void, PublishSettings, (const char* json), {
    const text = UTF8ToString(json);
    if (globalThis.__oceanOnSettings) globalThis.__oceanOnSettings(text);
});
// clang-format on

void PublishSettingsSnapshot() noexcept
{
    const std::string json = gStore.ExportJson();
    PublishSettings(json.c_str());
}

void Frame() noexcept
{
    const auto state = gScene.Tick();
    PresentState(static_cast<int>(state), static_cast<int>(gScene.GetError()), gScene.GetFrames());
}
} // namespace

// ---------------------------------------------------------------------------
// Bridge entry points (called from web/shell.html). All run on the main thread.
// ---------------------------------------------------------------------------
extern "C" {
EMSCRIPTEN_KEEPALIVE void OceanRestart() noexcept
{
    (void)gScene.Start();
    PublishSettingsSnapshot();
}
EMSCRIPTEN_KEEPALIVE void OceanStop() noexcept
{
    gScene.Shutdown();
}
EMSCRIPTEN_KEEPALIVE void OceanSetPaused(int paused) noexcept
{
    gScene.SetPaused(paused != 0);
}
EMSCRIPTEN_KEEPALIVE int OceanIsPaused() noexcept
{
    return gScene.IsPaused() ? 1 : 0;
}
EMSCRIPTEN_KEEPALIVE void OceanSetVisible(int visible) noexcept
{
    gScene.SetVisible(visible != 0);
}
EMSCRIPTEN_KEEPALIVE void OceanResetSimulation() noexcept
{
    gScene.ResetSimulation();
}

// Scalar controls.
EMSCRIPTEN_KEEPALIVE void OceanSetCurrentDirection(float v) noexcept
{
    gStore.SetCurrentDirectionDegrees(v);
}
EMSCRIPTEN_KEEPALIVE void OceanSetCurrentSpeed(float v) noexcept
{
    gStore.SetCurrentSpeed(v);
}
EMSCRIPTEN_KEEPALIVE void OceanSetWaveScale(float v) noexcept
{
    gStore.SetWaveScale(v);
}
EMSCRIPTEN_KEEPALIVE void OceanSetWaveAnimationSpeed(float v) noexcept
{
    gStore.SetWaveAnimationSpeed(v);
}
EMSCRIPTEN_KEEPALIVE void OceanSetWaveIntensity(float v) noexcept
{
    gStore.SetWaveIntensity(v);
}
EMSCRIPTEN_KEEPALIVE void OceanSetFoamAmount(float v) noexcept
{
    gStore.SetFoamAmount(v);
}

// Palette controls (RGB in 0..1).
EMSCRIPTEN_KEEPALIVE void OceanSetDeepColor(float r, float g, float b) noexcept
{
    gStore.SetDeepColor({.R = r, .G = g, .B = b});
}
EMSCRIPTEN_KEEPALIVE void OceanSetMidColor(float r, float g, float b) noexcept
{
    gStore.SetMidColor({.R = r, .G = g, .B = b});
}
EMSCRIPTEN_KEEPALIVE void OceanSetShallowColor(float r, float g, float b) noexcept
{
    gStore.SetShallowColor({.R = r, .G = g, .B = b});
}
EMSCRIPTEN_KEEPALIVE void OceanSetFoamColor(float r, float g, float b) noexcept
{
    gStore.SetFoamColor({.R = r, .G = g, .B = b});
}

// Presets.
EMSCRIPTEN_KEEPALIVE void OceanApplyPreset(int preset) noexcept
{
    gStore.ApplyPreset(static_cast<ocean::Preset>(preset));
    PublishSettingsSnapshot();
}
EMSCRIPTEN_KEEPALIVE void OceanResetSettings() noexcept
{
    gStore.Reset();
    PublishSettingsSnapshot();
}

// Export current settings as a heap-allocated JSON C string. JavaScript must
// free it with OceanFreeString. Returns null on allocation failure.
EMSCRIPTEN_KEEPALIVE char* OceanExportJson() noexcept
{
    const std::string json = gStore.ExportJson();
    auto* buffer = static_cast<char*>(std::malloc(json.size() + 1));
    if (buffer == nullptr)
    {
        return nullptr;
    }
    std::memcpy(buffer, json.c_str(), json.size() + 1);
    return buffer;
}
EMSCRIPTEN_KEEPALIVE void OceanFreeString(char* ptr) noexcept
{
    std::free(ptr);
}

// Import settings JSON. Returns a heap-allocated status string: empty on
// success, otherwise a friendly error message. Free with OceanFreeString.
EMSCRIPTEN_KEEPALIVE char* OceanImportJson(const char* json) noexcept
{
    const ocean::ParseResult result = gStore.ImportJson(json != nullptr ? json : "");
    if (result.Ok)
    {
        PublishSettingsSnapshot();
    }
    const std::string message = result.Ok ? std::string{} : result.Message;
    auto* buffer = static_cast<char*>(std::malloc(message.size() + 1));
    if (buffer == nullptr)
    {
        return nullptr;
    }
    std::memcpy(buffer, message.c_str(), message.size() + 1);
    return buffer;
}
}

int main()
{
    ludus::foundation::logging::LogConfig config;
    config.EnableConsole = true;
    config.EnableFile = false;
    ludus::foundation::logging::LogSystem::Initialize(config);
    ludus::foundation::logging::SetCurrentThreadName("Main");

    OceanRestart();
    emscripten_set_main_loop(Frame, 0, true);
    return 0;
}
