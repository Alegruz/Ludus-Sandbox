#include "ui/game_hud.h"

int main()
{
    ludus::sandbox::GameHud hud;
    ludus::sandbox::game::RippleGame game;
    if (!game.StartCampaign())
    {
        return 1;
    }
    auto uniforms = ludus::sandbox::ocean::BuildUniforms(ludus::sandbox::ocean::DefaultSettings(), {}, 1280, 720);
    hud.WriteUniforms(uniforms, game, 1);
    if (uniforms.UiInfo[0] < 3 || uniforms.UiInfo[0] > 5)
    {
        return 2;
    }
    const auto first = hud.GetPaintCommands()[0].Bounds;
    uniforms.Resolution[0] = 2560;
    uniforms.Resolution[1] = 1440;
    hud.WriteUniforms(uniforms, game, 2);
    const auto second = hud.GetPaintCommands()[0].Bounds;
    if (first.X != second.X || first.Y != second.Y || uniforms.UiRects[0][0] != first.X * 2)
    {
        return 3;
    }
    uniforms.Resolution[0] = 390;
    uniforms.Resolution[1] = 844;
    hud.WriteUniforms(uniforms, game, 1);
    for (const auto& command : hud.GetPaintCommands())
    {
        if (command.Bounds.X < 0 || command.Bounds.Y < 0 || command.Bounds.X + command.Bounds.Width > 390 ||
            command.Bounds.Y + command.Bounds.Height > 844)
        {
            return 4;
        }
    }
    hud.WriteUniforms(uniforms, game, 0);
    if (uniforms.UiInfo[0] != 0)
    {
        return 5;
    }
    uniforms.Resolution[0] = 40;
    hud.WriteUniforms(uniforms, game, 1);
    if (uniforms.UiInfo[0] != 0)
    {
        return 6;
    }
    return 0;
}
