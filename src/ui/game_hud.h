#pragma once

#include <ludus/foundation/base/core.h>

#include <ludus/ui/context.h>

#include "game/ripple_game.h"
#include "ocean/ocean_uniforms.h"

#include <span>

namespace ludus::sandbox
{
// A concrete Ui consumer. One bounded context per scene, no frame allocations.
// Text/action controls stay in the browser's accessible DOM; these two meters
// show gesture readiness (blue) and dock dwell progress (green) on both backends.
class GameHud final
{
public:
    GameHud() noexcept;
    void WriteUniforms(ocean::OceanUniforms& uniforms,
                       const game::RippleGame& game,
                       foundation::float32 pixelScale) noexcept;
    [[nodiscard]] std::span<const ui::PaintCommand> GetPaintCommands() const noexcept
    {
        return mContext.GetPaintCommands();
    }

private:
    ui::Context mContext;
    bool mReady = false;
};
} // namespace ludus::sandbox
