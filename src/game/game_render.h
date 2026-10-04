#pragma once

#include "game/ripple_game.h"
#include "ocean/ocean_uniforms.h"

namespace ludus::sandbox::game
{
[[nodiscard]] Camera
PresentationCamera(uint32 width, uint32 height, const RippleGame&, bool paused, bool gameplay = true) noexcept;
[[nodiscard]] ocean::OceanUniforms BuildUniforms(const ocean::OceanSettings&,
                                                 const ocean::SceneClock&,
                                                 uint32 width,
                                                 uint32 height,
                                                 const RippleGame&,
                                                 bool paused,
                                                 bool gameplay = true) noexcept;
} // namespace ludus::sandbox::game
