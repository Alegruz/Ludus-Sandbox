#include "ui/game_hud.h"

#include <ludus/foundation/math/scalar.hpp>

namespace ludus::sandbox
{
namespace
{
using foundation::float32;
using foundation::usize;
using foundation::math::Clamp;
using foundation::math::IsFinite;
using foundation::math::Max;
using foundation::math::Min;

ui::Element MakeBox(ui::WidgetId id, ui::Rect bounds, ui::Color color, usize parent = ui::kRoot) noexcept
{
    ui::Element element;
    element.Id = id;
    element.Parent = parent;
    element.Placement.Width = {bounds.Width, ui::Unit::Pixels};
    element.Placement.Height = {bounds.Height, ui::Unit::Pixels};
    element.Placement.OffsetX = bounds.X;
    element.Placement.OffsetY = bounds.Y;
    element.Fill = color;
    return element;
}
} // namespace

GameHud::GameHud() noexcept : mReady(mContext.TryInitialize(5) == ui::Status::Ok) {}

void GameHud::WriteUniforms(ocean::OceanUniforms& uniforms, const game::RippleGame& game, float32 pixelScale) noexcept
{
    uniforms.UiInfo[0] = 0;
    if (!mReady || !IsFinite(pixelScale) || pixelScale <= 0)
    {
        return;
    }
    const float32 width = uniforms.Resolution[0] / pixelScale;
    const float32 height = uniforms.Resolution[1] / pixelScale;
    // Hide on surfaces too small to leave a useful playfield.
    if (width < 180 || height < 180)
    {
        return;
    }
    // Leave the bottom-center feedback and the phone's central boat lane clear.
    const float32 panelWidth = Min(120.0f, width - 32);
    const float32 trackWidth = Max(0.0f, panelWidth - 24);
    ui::Element elements[] = {
        MakeBox(1, {0, 0, panelWidth, 48}, {0.014f, 0.026f, 0.050f, 0.90f}),
        MakeBox(2, {0, 0, trackWidth, 8}, {0.04f, 0.06f, 0.09f, 1}, 0),
        MakeBox(3, {0, 0, 0, 8}, {0.12f, 0.55f, 0.85f, 1}, 1),
        MakeBox(4, {0, 0, trackWidth, 8}, {0.04f, 0.06f, 0.09f, 1}, 0),
        MakeBox(5, {0, 0, 0, 8}, {0.10f, 0.75f, 0.35f, 1}, 3),
    };
    auto& panel = elements[0].Placement;
    panel.OffsetX = 16;
    panel.OffsetY = -88;
    panel.Vertical = ui::Align::End;
    panel.Padding = 12;
    panel.Gap = 8;
    panel.Children = ui::Flow::Column;
    const auto readiness = static_cast<float32>(1.0 - game.CooldownFraction());
    elements[2].Placement.Width = {Clamp(readiness, 0.0f, 1.0f), ui::Unit::Fraction};
    elements[4].Placement.Width = {Clamp(static_cast<float32>(game.DockProgress()), 0.0f, 1.0f), ui::Unit::Fraction};
    if (mContext.TrySetDocument(elements, {{0, 0, width, height}}) != ui::Status::Ok)
    {
        return;
    }
    const auto commands = mContext.GetPaintCommands();
    if (commands.size() > 6)
    {
        return;
    }
    for (usize index = 0; index < commands.size(); ++index)
    {
        const auto& command = commands[index];
        const auto& bounds = command.Bounds;
        const auto& color = command.Fill;
        auto& rect = uniforms.UiRects[index];
        auto& fill = uniforms.UiColors[index];
        rect[0] = bounds.X * pixelScale;
        rect[1] = bounds.Y * pixelScale;
        rect[2] = bounds.Width * pixelScale;
        rect[3] = bounds.Height * pixelScale;
        fill[0] = color.R;
        fill[1] = color.G;
        fill[2] = color.B;
        fill[3] = color.A;
    }
    uniforms.UiInfo[0] = static_cast<float32>(commands.size());
}
} // namespace ludus::sandbox
