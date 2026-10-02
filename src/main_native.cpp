// Native entry point for the Drift ocean playground.
//
// A native GUI framework is out of scope for this first slice (per the brief),
// so the native build exercises the SAME settings through available input/config
// facilities:
//
//   * An optional path to a versioned-JSON settings file (first CLI argument, or
//     the LUDUS_OCEAN_SETTINGS environment variable). Invalid files produce a
//     friendly message and fall back to the default "Drifting" preset.
//   * An optional preset name as the first argument ("calm"/"drifting"/"choppy").
//   * LUDUS_OCEAN_FRAMES caps the number of rendered frames (useful for CI /
//     offscreen smoke runs); 0 or unset runs until the window closes.
//
// This drives the Ludus Vulkan backend where a display/GPU is available. On a
// headless host (no Wayland/GPU) RHI startup fails cleanly and we report it,
// exactly like the engine's own lifecycle contract.

#include "ocean/ocean_scene.h"
#include "ocean/ocean_settings.h"
#include "ocean/settings_store.h"

#include <ludus/foundation/logging/log.hpp>
#include <ludus/foundation/logging/log_format.hpp>
#include <ludus/foundation/logging/log_system.hpp>

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>

namespace
{
using namespace ludus::foundation::logging;
namespace ocean = ludus::sandbox::ocean;

ocean::OceanSettings LoadInitialSettings(int argc, char** argv) noexcept
{
    // Argument may be a preset name or a path to a JSON settings file.
    std::string arg;
    if (argc > 1)
    {
        arg = argv[1];
    }
    else if (const char* env = std::getenv("LUDUS_OCEAN_SETTINGS"); env != nullptr)
    {
        arg = env;
    }

    if (arg.empty())
    {
        return ocean::DefaultSettings();
    }
    if (arg == "calm")
    {
        return ocean::MakePreset(ocean::Preset::Calm);
    }
    if (arg == "drifting")
    {
        return ocean::MakePreset(ocean::Preset::Drifting);
    }
    if (arg == "choppy")
    {
        return ocean::MakePreset(ocean::Preset::Choppy);
    }

    std::ifstream file(arg);
    if (!file.is_open())
    {
        LUDUS_LOG_WARN(LOG_CORE, "Could not open settings file '{}'; using the Drifting preset.", arg);
        return ocean::DefaultSettings();
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    const std::string json = buffer.str();
    const ocean::ParseResult parsed = ocean::FromJson(json);
    if (!parsed.Ok)
    {
        LUDUS_LOG_WARN(LOG_CORE, "Invalid settings file: {} Using the Drifting preset.", parsed.Message);
        return ocean::DefaultSettings();
    }
    LUDUS_LOG_INFO(LOG_CORE, "Loaded ocean settings from '{}'.", arg);
    return parsed.Value;
}

ludus::foundation::uint32 FrameCap() noexcept
{
    if (const char* env = std::getenv("LUDUS_OCEAN_FRAMES"); env != nullptr)
    {
        const long value = std::strtol(env, nullptr, 10);
        if (value > 0)
        {
            return static_cast<ludus::foundation::uint32>(value);
        }
    }
    return 0;
}
} // namespace

int main(int argc, char** argv)
{
    LogConfig config{};
    config.GlobalLevel = LogLevel::Info;
    config.EnableConsole = true;
    config.EnableFile = false;
    LogSystem::Initialize(config);
    SetCurrentThreadName("Main");

    const ocean::SettingsStore store(LoadInitialSettings(argc, argv));
    const ludus::foundation::uint32 cap = FrameCap();

    ludus::sandbox::OceanScene scene(store);
    if (!scene.Start())
    {
        LUDUS_LOG_ERROR(LOG_CORE,
                        "Ocean scene failed to start (graphics error {}). A Wayland display and a "
                        "Vulkan-capable GPU are required for the native scene.",
                        static_cast<ludus::foundation::uint32>(scene.GetError()));
        LogSystem::Shutdown();
        return 1;
    }

    auto state = scene.GetState();
    while (state == ludus::sandbox::SceneState::Loading || state == ludus::sandbox::SceneState::Playing)
    {
        state = scene.Tick();
        if (cap != 0 && scene.GetFrames() >= cap)
        {
            LUDUS_LOG_INFO(LOG_CORE, "Reached frame cap {}.", cap);
            break;
        }
    }

    const bool failed = state == ludus::sandbox::SceneState::Failed || state == ludus::sandbox::SceneState::DeviceLost;
    if (failed)
    {
        LUDUS_LOG_ERROR(LOG_CORE,
                        "Ocean scene stopped with graphics error {}.",
                        static_cast<ludus::foundation::uint32>(scene.GetError()));
    }
    else
    {
        LUDUS_LOG_INFO(LOG_CORE, "Ocean scene finished after {} frames.", scene.GetFrames());
    }
    scene.Shutdown();
    LogSystem::Shutdown();
    return failed ? 1 : 0;
}
