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

#include <ludus/foundation/base/types.h>
#include <ludus/foundation/logging/log.hpp>
#include <ludus/foundation/logging/log_system.hpp>

#include <emscripten.h>

#include <cstdlib>
#include <cstring>
#include <string>

using ludus::foundation::float32;
using ludus::foundation::float64;
using ludus::foundation::uint32;

namespace
{
namespace ocean = ludus::sandbox::ocean;

// The store is the single bridge object; the scene borrows it.
ocean::SettingsStore gStore;
ludus::sandbox::OceanScene gScene(gStore);

// Report scene state + frame count + current settings snapshot to the DOM so
// the panel can reflect presets/imports and show loading / error / restart
// state. States mirror SceneState. Per-attempt errors remain available for diagnostics.
// clang-format off
EM_JS(void, PresentState, (int state, int error, unsigned frames, int backend, int webgpuError, int webglError), {
    const status = document.getElementById('status');
    if (!status) return;
    const names = ['stopped', 'loading', 'playing', 'failed', 'device-lost'];
    const messages = [
        'Stopped. Select Restart to play.',
        'Loading the ocean\u2026',
        'Drift is ready.',
        'The ocean could not start. Try Restart.',
        'Graphics connection lost. Select Restart to try again.'];
    let message = messages[state] || "";
    if (state === 3 && error === 3) {
        message = 'Graphics are unavailable. Try Restart or enable browser hardware acceleration.';
    }
    if (status.textContent !== message) status.textContent = message;
    status.dataset.state = names[state] || 'unknown';
    status.dataset.frames = frames;
    status.dataset.error = error;
    status.dataset.backend = ['vulkan', 'webgpu', 'webgl2'][backend] || 'unknown';
    status.dataset.webgpuError = webgpuError;
    status.dataset.webglError = webglError;
    document.getElementById('panel').hidden = state !== 2;
    document.getElementById('game-controls').hidden = state !== 2;
    document.getElementById('game-status').hidden = state !== 2;
    document.getElementById('game-instructions').hidden = state !== 2;
    document.getElementById('status-restart').hidden = state === 1 || state === 2;
    if (globalThis.__oceanOnState) globalThis.__oceanOnState(state, error, frames);
});

EM_JS(int, ReadBackendSelection, (), {
    const value = new URLSearchParams(location.search).get('backend');
    return value === 'webgpu' ? 1 : value === 'webgl2' ? 2 : 0;
});

// Pull the authoritative settings JSON into the DOM when it changes externally
// (preset/reset/import), so sliders reflect the real values.
EM_JS(void, PublishSettings, (const char* json), {
    const text = UTF8ToString(json);
    if (globalThis.__oceanOnSettings) globalThis.__oceanOnSettings(text);
});
EM_JS(void, PresentGame, (float64 x, float64 y, uint32 placements, uint32 contacts, uint32 ticks,
                         uint32 rings, uint32 result, float64 cooldown, int paused, int enabled,
                         uint32 phase, uint32 crash, float64 vx, float64 vy, float64 docking), {
    const hud = document.getElementById('game-status');
    if (!hud) return;
    Object.assign(hud.dataset, {x, y, placements, contacts, ticks, rings, result, cooldown,
                               paused: String(!!paused), enabled: String(!!enabled), vx, vy, docking, crash,
                               phase: ['playing', 'crashed', 'arrived'][phase]});
    const feedback = ['Click or tap water to send a ripple.', 'Ripple queued.', 'Ripple sent.',
                      'Ripple recharging...', 'Too many ripples. Wait a moment.',
                      'Tap clear water, away from rocks.', 'Resume to place a ripple.'];
    const message = !enabled ? 'Ocean tuning mode.' : phase === 1 ?
                    (crash === 1 ? 'Crashed into a rock. Retry to rescue the boat.' : 'Reached the water boundary. Retry to rescue the boat.') :
                    phase === 2 ? 'Boat rescued! Retry to sail the course again.' : paused ? 'Paused.' :
                    docking > 0 ? 'Mooring… Keep the boat slow inside the green dock.' :
                    cooldown > 0 ? 'Ripple recharging...' : feedback[result] || feedback[0];
    if (hud.textContent !== message) hud.textContent = message;
    const button = document.getElementById('game-pause');
    button.textContent = paused ? 'Resume' : 'Pause';
    button.disabled = enabled && phase !== 0;
    button.setAttribute('aria-pressed', String(!!paused));
    document.getElementById('mode').setAttribute('aria-pressed', String(!!enabled));
});
// clang-format on

void PublishSettingsSnapshot() noexcept
{
    const std::string json = gStore.ExportJson();
    PublishSettings(json.c_str());
}

void PresentSceneState() noexcept
{
    const auto state = gScene.GetState();
    const auto startup = gScene.GetStartupInfo();
    PresentState(static_cast<int>(state),
                 static_cast<int>(gScene.GetError()),
                 gScene.GetFrames(),
                 static_cast<int>(startup.SelectedBackend),
                 static_cast<int>(startup.WebGpu.Error),
                 static_cast<int>(startup.WebGL2.Error));
}
void Frame() noexcept
{
    (void)gScene.Tick();
    PresentSceneState();
    const auto& game = gScene.GetGame();
    const auto& boat = game.GetBoat();
    PresentGame(boat.Position.X,
                boat.Position.Y,
                static_cast<uint32>(game.Placements()),
                static_cast<uint32>(game.Contacts()),
                static_cast<uint32>(game.Ticks()),
                game.ActiveRipples(),
                static_cast<uint32>(game.LastPlacement()),
                game.CooldownFraction(),
                gScene.IsPaused() ? 1 : 0,
                gScene.GameEnabled() ? 1 : 0,
                static_cast<uint32>(game.Phase()),
                static_cast<uint32>(game.Crash()),
                boat.Velocity.X,
                boat.Velocity.Y,
                game.DockProgress());
}
} // namespace

// ---------------------------------------------------------------------------
// Bridge entry points (called from web/shell.html). All run on the main thread.
// ---------------------------------------------------------------------------
extern "C" {
EMSCRIPTEN_KEEPALIVE void OceanRestart() noexcept
{
    (void)gScene.Start(static_cast<ludus::graphics::rhi::BackendSelection>(ReadBackendSelection()));
    PublishSettingsSnapshot();
    PresentSceneState();
}
EMSCRIPTEN_KEEPALIVE void OceanStop() noexcept
{
    gScene.Shutdown();
    PresentSceneState();
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

EMSCRIPTEN_KEEPALIVE int DriftPlace(float64 x, float64 y) noexcept
{
    return static_cast<int>(gScene.PlaceRipple(x, y));
}
EMSCRIPTEN_KEEPALIVE void DriftSetEnabled(int enabled) noexcept
{
    gScene.SetGameEnabled(enabled != 0);
}
EMSCRIPTEN_KEEPALIVE void DriftSetFocused(int focused) noexcept
{
    gScene.SetFocused(focused != 0);
}
EMSCRIPTEN_KEEPALIVE void DriftCancelInput() noexcept
{
    gScene.CancelGameInput();
}

// Scalar controls.
EMSCRIPTEN_KEEPALIVE void OceanSetCurrentDirection(float32 v) noexcept
{
    gStore.SetCurrentDirectionDegrees(v);
}
EMSCRIPTEN_KEEPALIVE void OceanSetCurrentSpeed(float32 v) noexcept
{
    gStore.SetCurrentSpeed(v);
}
EMSCRIPTEN_KEEPALIVE void OceanSetWaveScale(float32 v) noexcept
{
    gStore.SetWaveScale(v);
}
EMSCRIPTEN_KEEPALIVE void OceanSetWaveAnimationSpeed(float32 v) noexcept
{
    gStore.SetWaveAnimationSpeed(v);
}
EMSCRIPTEN_KEEPALIVE void OceanSetWaveIntensity(float32 v) noexcept
{
    gStore.SetWaveIntensity(v);
}
EMSCRIPTEN_KEEPALIVE void OceanSetFoamAmount(float32 v) noexcept
{
    gStore.SetFoamAmount(v);
}

// Palette controls (RGB in 0..1).
EMSCRIPTEN_KEEPALIVE void OceanSetDeepColor(float32 r, float32 g, float32 b) noexcept
{
    gStore.SetDeepColor({.R = r, .G = g, .B = b});
}
EMSCRIPTEN_KEEPALIVE void OceanSetMidColor(float32 r, float32 g, float32 b) noexcept
{
    gStore.SetMidColor({.R = r, .G = g, .B = b});
}
EMSCRIPTEN_KEEPALIVE void OceanSetShallowColor(float32 r, float32 g, float32 b) noexcept
{
    gStore.SetShallowColor({.R = r, .G = g, .B = b});
}
EMSCRIPTEN_KEEPALIVE void OceanSetFoamColor(float32 r, float32 g, float32 b) noexcept
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
