#include "ocean/ocean_scene.h"

#include "game/game_render.h"
#include "ocean/ocean_uniforms.h"

// Generated at build time by ludus_compile_shader(NAME ocean ...). Provides
// ludus::shaders::ocean::Vertex()/Fragment() ShaderDescription factories
// (SPIR-V on native, WGSL under LUDUS_PLATFORM_WEB).
#include "ocean.h"

#include <ludus/foundation/profiling/clock.hpp>

#include <cmath>
#include <span>

namespace ludus::sandbox
{
using namespace ludus::foundation;

namespace
{
[[nodiscard]] bool Accepted(rhi::ResourceStatus status) noexcept
{
    return status == rhi::ResourceStatus::Ready || status == rhi::ResourceStatus::Pending;
}
} // namespace

void OceanScene::Shutdown() noexcept
{
    // Shutdown aborts any open frame and releases all resources, so individual
    // Destroy calls are unnecessary here (see fullscreen-rendering.md step 6).
    rhi::Shutdown();
    mWindow.Reset();
    mVertex = {};
    mFragment = {};
    mUniform = {};
    mPipeline = {};
    mResourcesCreated = false;
    mPipelineCreated = false;
    mUniformSeeded = false;
    mState = SceneState::Stopped;
    mError = rhi::StartupError::None;
    mLastTick = 0;
    CancelGameInput();
}

void OceanScene::Fail(SceneState state, rhi::StartupError error) noexcept
{
    mStartup = rhi::GetStartup();
    Shutdown();
    mState = state;
    mError = error;
}

bool OceanScene::Start(rhi::BackendSelection selection) noexcept
{
    Shutdown();
    mStartup = {};
    mFrames = 0;

    platform::WindowManager manager;
    if (!manager.Initialize({}) || !manager.CreateWindow(
                                       {
                                           .Name = "Ludus Drift Ocean",
                                           .Width = 1280,
                                           .Height = 720,
                                       },
                                       mWindow))
    {
        Fail(SceneState::Failed, rhi::StartupError::InvalidWindow);
        return false;
    }

    const auto started = rhi::Start({.Name = "Drift Ocean", .Version = 1}, mWindow->GetNativeWindowInfo(), selection);
    if (started != rhi::StartStatus::Ready && started != rhi::StartStatus::Pending)
    {
        Fail(SceneState::Failed, rhi::GetStartup().Error);
        return false;
    }
    mState = SceneState::Loading;
    mLastTick = profiling::NowTicks();
    return true;
}

bool OceanScene::EnsureResources() noexcept
{
    if (!mResourcesCreated)
    {
        if (!Accepted(rhi::CreateShader(ludus::shaders::ocean::Vertex(), mVertex)) ||
            !Accepted(rhi::CreateShader(ludus::shaders::ocean::Fragment(), mFragment)) ||
            !Accepted(rhi::CreateUniform(sizeof(ocean::OceanUniforms), mUniform)))
        {
            return false;
        }
        mResourcesCreated = true;
    }

    // All three dependencies must be Ready before creating the pipeline.
    for (auto status : {rhi::GetStatus(mVertex), rhi::GetStatus(mFragment), rhi::GetStatus(mUniform)})
    {
        if (status == rhi::ResourceStatus::Pending)
        {
            return true; // still loading; try again next tick
        }
        if (status != rhi::ResourceStatus::Ready)
        {
            return false;
        }
    }

    if (!mPipelineCreated)
    {
        if (!Accepted(rhi::CreatePipeline({mVertex, mFragment, mUniform}, mPipeline)))
        {
            return false;
        }
        mPipelineCreated = true;
    }
    return true;
}

rhi::FrameStatus OceanScene::RenderFrame(const platform::browser::WindowState& input) noexcept
{
    // Determine the framebuffer extent we want to render at. On web the browser
    // window state provides the DPR-scaled framebuffer size; on native we fall
    // back to the window's requested size (actual acquired extent comes back
    // from GetFrameInfo and corrects the uniform before the draw).
    uint32 width = mWindow->GetNativeWindowInfo().Width;
    uint32 height = mWindow->GetNativeWindowInfo().Height;
    if (input.FramebufferWidth > 0 && input.FramebufferHeight > 0)
    {
        width = input.FramebufferWidth;
        height = input.FramebufferHeight;
    }

    // A zero-size target (minimized window / hidden canvas) must SKIP without
    // invalidating resources (contract). Retain everything and retry next tick.
    if (width == 0 || height == 0)
    {
        return rhi::FrameStatus::Skipped;
    }

    const ocean::OceanSettings& settings = mStore->Get();

    // Seed the uniform before the first draw (required by the contract). We fill
    // it with the current extent; GetFrameInfo may refine it after BeginFrame.
    const auto buildUniforms = [&](uint32 w, uint32 h) noexcept {
        return mGameEnabled ? game::BuildUniforms(settings, mClock, w, h, mGame, IsPaused() || !mFocused || !mVisible)
                            : ocean::BuildUniforms(settings, mClock, w, h);
    };
    ocean::OceanUniforms uniforms = buildUniforms(width, height);
    const auto uploadBytes = [&uniforms]() noexcept {
        return std::span<const uint8>(reinterpret_cast<const uint8*>(&uniforms), sizeof(uniforms));
    };
    if (!mUniformSeeded)
    {
        if (rhi::UpdateUniform(mUniform, uploadBytes()) != rhi::ResourceStatus::Ready)
        {
            return rhi::FrameStatus::Failed;
        }
        mUniformSeeded = true;
    }

    // clang-format off
    // Ludus requires a separate opening brace for multiline designated initializers.
    const rhi::FrameTarget target =
    {
        .Width = width,
        .Height = height,
        .Red = static_cast<float64>(settings.DeepColor.R),
        .Green = static_cast<float64>(settings.DeepColor.G),
        .Blue = static_cast<float64>(settings.DeepColor.B),
        .Alpha = 1.0,
    };
    // clang-format on
    // SetFrameTarget only reports Ready/Skipped (resource-preserving) here, since
    // we never pass a zero extent and only call it between frames after Ready.
    // Anything other than Ready is treated as a resource-preserving skip, not a
    // teardown; genuine failures surface through BeginFrame/Draw/GetStartup.
    if (rhi::SetFrameTarget(target) != rhi::FrameStatus::Ready)
    {
        return rhi::FrameStatus::Skipped;
    }

    const auto begun = rhi::BeginFrameStatus();
    if (begun == rhi::FrameStatus::Skipped)
    {
        return rhi::FrameStatus::Skipped; // minimized / outdated; retain resources
    }
    if (begun != rhi::FrameStatus::Ready)
    {
        return begun;
    }

    // Correct the resolution/world extent from the ACTUAL acquired frame, so the
    // wave scale stays consistent even if native negotiation changed the extent.
    // GetFrameInfo is valid inside an open frame; guard against a zero extent.
    const rhi::FrameInfo info = rhi::GetFrameInfo();
    const uint32 frameW = info.Width > 0 ? info.Width : width;
    const uint32 frameH = info.Height > 0 ? info.Height : height;
    uniforms = buildUniforms(frameW, frameH);
    mCamera = game::FitCamera(frameW, frameH, mGame.GetLevel().HalfExtent);
    if (rhi::UpdateUniform(mUniform, uploadBytes()) != rhi::ResourceStatus::Ready ||
        rhi::DrawFullscreen(mPipeline) != rhi::ResourceStatus::Ready)
    {
        return rhi::FrameStatus::Failed;
    }
    return rhi::EndFrameStatus();
}

SceneState OceanScene::Tick() noexcept
{
    if (mState != SceneState::Loading && mState != SceneState::Playing)
    {
        return mState;
    }

    if (!mWindow->HandleEvent({}))
    {
        Shutdown();
        return mState;
    }

    // Elapsed time with a monotonic nanosecond clock; delta is sanitized by the
    // scene clock (clamped resume, freeze while hidden).
    const auto now = profiling::NowTicks();
    float64 delta = now >= mLastTick ? static_cast<float64>(now - mLastTick) / 1000000000.0 : 0.0;
    mLastTick = now;
    if (mSkipDelta)
    {
        delta = 0.0;
        mSkipDelta = false;
    }

    const auto startup = rhi::GetStartup();
    mStartup = startup;
    if (startup.State == rhi::StartupState::Failed || startup.State == rhi::StartupState::DeviceLost)
    {
        Fail(startup.State == rhi::StartupState::DeviceLost ? SceneState::DeviceLost : SceneState::Failed,
             startup.Error);
        return mState;
    }
    if (startup.State != rhi::StartupState::Ready)
    {
        return mState; // still Pending (browser startup)
    }

    if (!EnsureResources())
    {
        Fail(SceneState::Failed, rhi::StartupError::RenderingUnavailable);
        return mState;
    }
    if (!mPipelineCreated || rhi::GetStatus(mPipeline) == rhi::ResourceStatus::Pending)
    {
        return mState; // pipeline still building
    }
    if (rhi::GetStatus(mPipeline) != rhi::ResourceStatus::Ready)
    {
        Fail(SceneState::Failed, rhi::StartupError::RenderingUnavailable);
        return mState;
    }

    mState = SceneState::Playing;

    // Gather presentation/visibility state. On web this comes from the browser
    // window snapshot; on native we treat the window as visible.
    platform::browser::WindowState input;
    if (mWindow->GetNativeWindowInfo().System == platform::WindowSystem::WebCanvas)
    {
        (void)mWindow->SetBrowserFramebufferLimit(startup.MaxTextureDimension2D);
        (void)mWindow->GetBrowserState(input);
    }
    else
    {
        input.Visible = true;
    }

    // Drain input events; held state lives in the snapshot. UI ownership is
    // explicit: the scene only reads visibility here and never consumes DOM UI
    // interactions (those are handled by the HTML panel, not the canvas).
    platform::browser::InputEvent event;
    while (mWindow->PollBrowserInput(event))
    {
    }

    // Advance the simulation using only VISIBLE time. SetVisible from the driver
    // can also force-freeze; combine both signals.
    const bool visible = mVisible && input.Visible && mFocused;
    const auto previousTicks = mGame.Ticks();
    mGame.Advance(delta, visible && !IsPaused() && mGameEnabled);
    const float64 visualDelta =
        mGameEnabled ? static_cast<float64>(mGame.Ticks() - previousTicks) * game::kTickSeconds : delta;
    // Gameplay advances the ocean on committed ticks, never ahead of its rings.
    ocean::Advance(mClock, mStore->Get(), visualDelta, visible);

    const rhi::FrameStatus rendered = RenderFrame(input);
    if (rendered == rhi::FrameStatus::Failed || rendered == rhi::FrameStatus::InvalidState)
    {
        const auto failed = rhi::GetStartup();
        Fail(failed.State == rhi::StartupState::DeviceLost ? SceneState::DeviceLost : SceneState::Failed,
             failed.Error == rhi::StartupError::None ? rhi::StartupError::RenderingUnavailable : failed.Error);
        return mState;
    }
    if (rendered == rhi::FrameStatus::Ready)
    {
        ++mFrames;
    }
    return mState;
}

game::PlacementResult OceanScene::PlaceRipple(float64 x, float64 y) noexcept
{
    if (mState != SceneState::Playing || !mGameEnabled || IsPaused() || !mVisible || !mFocused)
    {
        return game::PlacementResult::Inactive;
    }
    if (!std::isfinite(x) || !std::isfinite(y) || x < 0.0 || x > 1.0 || y < 0.0 || y > 1.0)
    {
        return game::PlacementResult::Outside;
    }
    return mGame.Place(game::ScreenToWorld({x, y}, mCamera));
}

game::PlacementResult OceanScene::Stroke(float64 x, float64 y, uint32 phase) noexcept
{
    if (mState != SceneState::Playing || !mGameEnabled || IsPaused() || !mVisible || !mFocused)
    {
        return game::PlacementResult::Inactive;
    }
    if (!std::isfinite(x) || !std::isfinite(y) || x < 0.0 || x > 1.0 || y < 0.0 || y > 1.0 || phase > 2)
    {
        mGame.CancelInput();
        return game::PlacementResult::Outside;
    }
    const auto world = game::ScreenToWorld({x, y}, mCamera);
    if (phase == 0)
    {
        return mGame.BeginStroke(world);
    }
    return phase == 1 ? mGame.MoveStroke(world) : mGame.EndStroke(world);
}

} // namespace ludus::sandbox
