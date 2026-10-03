#pragma once

// Ocean scene controller.
//
// Drives the Ludus public fullscreen RHI lifecycle (rhi.h + render.h) to render
// the procedural ocean. It reuses the engine's documented lifecycle semantics
// (Start/GetStartup/Create*/Pipeline/SetFrameTarget/BeginFrame/Draw/EndFrame/
// Shutdown) WITHOUT touching any private/backend code. The engine is unaware of
// ocean controls: this class pulls settings from a SettingsStore and uploads a
// uniform block it owns.
//
// One main-thread session, matching the RHI's single-session contract (like the
// smoke app's Application).

#include <ludus/foundation/base/core.h>
#include <ludus/foundation/base/pointer.hpp>

#include <ludus/graphics/rhi/render.h>
#include <ludus/graphics/rhi/rhi.h>

#include <ludus/platform/base/window.h>
#include <ludus/platform/browser/window.h>

#include "game/ripple_game.h"

#include "ocean/ocean_clock.h"
#include "ocean/ocean_settings.h"
#include "ocean/settings_store.h"

namespace ludus::sandbox
{
namespace rhi = ludus::graphics::rhi;

enum class SceneState : ludus::foundation::uint8
{
    Stopped,
    Loading, // RHI/resources not yet ready
    Playing, // rendering
    Failed,
    DeviceLost
};

// Describes how to present (used by the DOM/native status display).
class OceanScene final
{
public:
    // The store is borrowed and must outlive the scene; it is the single bridge
    // between UI/config and the GPU-free settings. The scene never mutates it.
    explicit OceanScene(const ocean::SettingsStore& store) noexcept : mStore(&store) {}

    bool Start(rhi::BackendSelection selection = rhi::BackendSelection::Auto) noexcept;
    SceneState Tick() noexcept; // advance + render one frame
    void Shutdown() noexcept;

    [[nodiscard]] SceneState GetState() const noexcept
    {
        return mState;
    }
    [[nodiscard]] rhi::StartupError GetError() const noexcept
    {
        return mError;
    }
    [[nodiscard]] ludus::foundation::uint32 GetFrames() const noexcept
    {
        return mFrames;
    }
    [[nodiscard]] rhi::StartupInfo GetStartupInfo() const noexcept
    {
        return mStartup;
    }
    [[nodiscard]] const ocean::SceneClock& GetClock() const noexcept
    {
        return mClock;
    }

    // Visibility / pause are owned by the driver (web: page visibility; native:
    // config/CLI). Hidden freezes the sim; paused freezes it too but keeps
    // drawing the last frame.
    void SetVisible(bool visible) noexcept
    {
        mVisible = visible;
        CancelGameInput();
    }
    void SetPaused(bool paused) noexcept
    {
        mClock.Paused = paused;
        CancelGameInput();
    }
    [[nodiscard]] bool IsPaused() const noexcept
    {
        return mClock.Paused;
    }
    void ResetSimulation() noexcept
    {
        ocean::ResetTime(mClock);
        mGame.Reset();
        CancelGameInput();
    }

    [[nodiscard]] game::PlacementResult PlaceRipple(foundation::float64 x, foundation::float64 y) noexcept;
    [[nodiscard]] const game::RippleGame& GetGame() const noexcept
    {
        return mGame;
    }
    [[nodiscard]] bool GameEnabled() const noexcept
    {
        return mGameEnabled;
    }
    void SetGameEnabled(bool enabled) noexcept
    {
        mGameEnabled = enabled;
        CancelGameInput();
    }
    void SetFocused(bool focused) noexcept
    {
        mFocused = focused;
        CancelGameInput();
    }
    void CancelGameInput() noexcept
    {
        mGame.CancelInput();
        mSkipDelta = true;
    }

private:
    void Fail(SceneState state, rhi::StartupError error) noexcept;
    [[nodiscard]] bool EnsureResources() noexcept; // create shaders/uniform/pipeline
    [[nodiscard]] rhi::FrameStatus RenderFrame(const platform::browser::WindowState& input) noexcept;

    const ocean::SettingsStore* mStore = nullptr;
    foundation::UniquePtr<platform::Window> mWindow;

    rhi::ShaderHandle mVertex;
    rhi::ShaderHandle mFragment;
    rhi::UniformHandle mUniform;
    rhi::PipelineHandle mPipeline;
    bool mResourcesCreated = false;
    bool mPipelineCreated = false;
    bool mUniformSeeded = false;

    ocean::SceneClock mClock;
    game::RippleGame mGame;
    game::Camera mCamera;
    bool mGameEnabled = true;
    bool mFocused = true;
    bool mSkipDelta = true;
    SceneState mState = SceneState::Stopped;
    rhi::StartupInfo mStartup;
    rhi::StartupError mError = rhi::StartupError::None;
    foundation::uint64 mLastTick = 0;
    foundation::uint32 mFrames = 0;
    bool mVisible = true;
};
} // namespace ludus::sandbox
