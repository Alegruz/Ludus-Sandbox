#include "game/game_render.h"
#include "game/ripple_game.h"
#include "ocean/ocean_uniforms.h"

#include <cmath>
#include <cstdio>
#include <limits>

namespace
{
using namespace ludus::sandbox::game;
int gFailures = 0;
int gChecks = 0;
void Check(bool ok, const char* name, int line) noexcept
{
    ++gChecks;
    if (!ok)
    {
        ++gFailures;
        std::printf("FAIL %d: %s\n", line, name);
    }
}
#define CHECK(value) Check((value), #value, __LINE__)
bool Near(float64 a, float64 b) noexcept
{
    return std::abs(a - b) < 1e-6;
}

void TestContact() noexcept
{
    float64 t = -1.0;
    CHECK(RingContact({0, 0}, {0, 0}, {-10, 0}, 0, 30, t));
    CHECK(t > 0.2 && t < 0.4); // Entire band can pass the hull in one tick.
    CHECK(!RingContact({0, 0}, {0, 0}, {-40, 0}, 0, 30, t));
    CHECK(RingContact({-10, 0}, {10, 0}, {0, 0}, 5, 5, t)); // Moving hull passes stationary ring.
    CHECK(t > 0.1 && t < 0.3);
    CHECK(RingContact({0, 0}, {0, 0}, {0, 0}, 0, 0.3, t));
    CHECK(Near(t, 0));
    CHECK(RingContact({-3, kBoatRadius + kRingHalfWidth},
                      {3, kBoatRadius + kRingHalfWidth},
                      {0, 0},
                      0,
                      0,
                      t)); // Tangency.
    CHECK(Near(t, 0.5));
}

void TestPushAndDrag() noexcept
{
    RippleGame game;
    CHECK(game.Place({-5, 0}) == PlacementResult::Queued);
    for (int i = 0; i < 60; ++i)
        game.Tick();
    CHECK(game.GetBoat().Position.X > 0.5);
    CHECK(Near(game.GetBoat().Position.Y, 0));
    CHECK(game.Contacts() == 1);
    const float64 velocity = game.GetBoat().Velocity.X;
    for (int i = 0; i < 120; ++i)
        game.Tick();
    CHECK(game.Contacts() == 1);
    CHECK(game.GetBoat().Velocity.X < velocity);
    CHECK(game.ActiveRipples() == 0);

    // A ring placed ahead opposes existing motion; a ring beside it turns.
    const float64 forwardVelocity = game.GetBoat().Velocity.X;
    (void)game.Place({game.GetBoat().Position.X + 5, 0});
    for (int i = 0; i < 30; ++i)
        game.Tick();
    CHECK(game.Contacts() == 2);
    CHECK(game.GetBoat().Velocity.X < forwardVelocity);
    (void)game.Place({game.GetBoat().Position.X, game.GetBoat().Position.Y - 5});
    for (int i = 0; i < 30; ++i)
        game.Tick();
    CHECK(game.Contacts() == 3);
    CHECK(game.GetBoat().Velocity.Y > 0);

    game.Reset();
    (void)game.Place({5, 0});
    for (int i = 0; i < 60; ++i)
        game.Tick();
    CHECK(game.GetBoat().Position.X < -0.5);
    game.Reset();
    (void)game.Place({0, -5});
    for (int i = 0; i < 60; ++i)
        game.Tick();
    CHECK(game.GetBoat().Position.Y > 0.5);
    game.Reset();
    (void)game.Place({0, 0});
    for (int i = 0; i < 100; ++i)
        game.Tick();
    CHECK(game.Contacts() == 1);
    CHECK(Near(game.GetBoat().Position.X, 0));
    CHECK(Near(game.GetBoat().Velocity.X, 0));
}

void TestInputAndClock() noexcept
{
    RippleGame game;
    CHECK(game.Place({31, 0}) == PlacementResult::Outside);
    CHECK(game.Place({std::numeric_limits<float64>::infinity(), 0}) == PlacementResult::Outside);
    (void)game.Place({-5, 0});
    game.Advance(kTickSeconds * 0.5, true);
    CHECK(game.Placements() == 0);
    game.Advance(kTickSeconds * 0.5, true);
    CHECK(game.Placements() == 1);
    game.Advance(0.1, true);
    CHECK(game.Ticks() == 5);
    CHECK(game.DiscardedTicks() == 2); // At most four catch-up ticks.
    CHECK(game.Placements() == 1);
    (void)game.Place({-5, 0});
    game.Tick();
    CHECK(game.LastPlacement() == PlacementResult::Cooldown);
    CHECK(game.Placements() == 1);
    game.Advance(999, false);
    const auto ticks = game.Ticks();
    game.Advance(std::numeric_limits<float64>::quiet_NaN(), true);
    CHECK(game.Ticks() == ticks);
    game.Reset();
    for (usize i = 0; i < kInputCapacity; ++i)
        CHECK(game.Place({1, 1}) == PlacementResult::Queued);
    CHECK(game.Place({1, 1}) == PlacementResult::Capacity);
    game.CancelInput();
    game.Tick();
    CHECK(game.Placements() == 0);
    CHECK(game.ActiveRipples() == 0);
    CHECK(Near(game.Alpha(), 0));
}

void TestPresentationRates() noexcept
{
    RippleGame sixty;
    RippleGame thirty;
    (void)sixty.Place({-5, 0});
    (void)thirty.Place({-5, 0});
    for (int i = 0; i < 60; ++i)
        sixty.Advance(kTickSeconds, true);
    for (int i = 0; i < 30; ++i)
        thirty.Advance(kTickSeconds * 2, true);
    CHECK(sixty.Ticks() == thirty.Ticks());
    CHECK(Near(sixty.GetBoat().Position.X, thirty.GetBoat().Position.X));
    CHECK(Near(sixty.GetBoat().Velocity.X, thirty.GetBoat().Velocity.X));
    CHECK(sixty.Contacts() == thirty.Contacts());
}

void TestPhysicalCurrentAndTuning() noexcept
{
    RippleGame game;
    PhysicsSettings settings;
    settings.WaterVelocity = {2.0, -1.0};
    CHECK(game.SetPhysics(settings));
    for (int i = 0; i < 60; ++i)
    {
        game.Tick();
    }
    const float64 blend = 1.0 - std::exp(-settings.DragRate);
    CHECK(Near(game.GetBoat().Velocity.X, 2.0 * blend));
    CHECK(Near(game.GetBoat().Velocity.Y, -blend));
    CHECK(Near(game.GetBoat().Position.X, 2.0 * (1.0 - blend / settings.DragRate)));
    CHECK(Near(game.GetBoat().Position.Y, -(1.0 - blend / settings.DragRate)));
    const auto before = game.GetBoat().Position;
    settings.DragRate = -1.0;
    CHECK(!game.SetPhysics(settings));
    CHECK(Near(game.GetPhysics().DragRate, 0.7));
    CHECK(Near(game.GetBoat().Position.X, before.X));
    settings.DragRate = std::numeric_limits<float64>::quiet_NaN();
    CHECK(!game.SetPhysics(settings));
    settings.DragRate = 0.0;
    settings.WaterVelocity.X = std::numeric_limits<float64>::infinity();
    CHECK(!game.SetPhysics(settings));
    settings.WaterVelocity = {8.0, 0.0};
    CHECK(!game.SetPhysics(settings));
    settings.WaterVelocity = {2.0, -1.0};
    settings.MaxBoatSpeed = 0.0;
    CHECK(!game.SetPhysics(settings));
    settings.MaxBoatSpeed = 7.0;
    settings.PushSpeed = -1.0;
    CHECK(!game.SetPhysics(settings));
    settings.PushSpeed = std::numeric_limits<float64>::infinity();
    CHECK(!game.SetPhysics(settings));
    settings.PushSpeed = 2.5;
    CHECK(game.SetPhysics(settings));
    const auto velocity = game.GetBoat().Velocity;
    game.Tick();
    CHECK(Near(game.GetBoat().Velocity.X, velocity.X));
    CHECK(Near(game.GetBoat().Position.X, before.X + velocity.X * kTickSeconds));
    game.Reset();
    CHECK(Near(game.GetPhysics().WaterVelocity.X, 2.0));
    CHECK(Near(game.GetPhysics().DragRate, 0.0));
    CHECK(Near(game.GetBoat().Position.X, 0.0));
    game.Tick();
    CHECK(Near(game.GetBoat().Velocity.X, 0.0)); // Zero drag means no water coupling.

    settings.DragRate = 1e-20;
    CHECK(game.SetPhysics(settings));
    (void)game.Place({-5, 0});
    for (int i = 0; i < 60; ++i)
    {
        game.Tick();
    }
    CHECK(game.GetBoat().Position.X > 0.5); // Small drag must not erase motion.
    settings.PushSpeed = 0.0;
    settings.DragRate = 0.7;
    CHECK(game.SetPhysics(settings));
    game.Reset();
    (void)game.Place({-5, 0});
    for (int i = 0; i < 60; ++i)
    {
        game.Tick();
    }
    CHECK(game.Contacts() == 1);
    CHECK(Near(game.GetBoat().Velocity.X, 2.0 * blend));
}

void TestCameraAndSnapshot() noexcept
{
    const auto wide = FitCamera(1920, 1080);
    const auto tall = FitCamera(390, 844);
    CHECK(wide.Height >= 90 && wide.Width >= 70);
    CHECK(tall.Height >= 90 && tall.Width >= 70);
    CHECK(Near(wide.Width / wide.Height, 1920.0 / 1080));
    CHECK(Near(tall.Width / tall.Height, 390.0 / 844));
    CHECK(Near(ScreenToWorld({0.5, 0.5}, tall).X, 0));
    CHECK(ScreenToWorld({0.5, 0}, tall).Y > 0);
    RippleGame game;
    (void)game.Place({-5, 0});
    for (int i = 0; i < 30; ++i)
        game.Tick();
    using namespace ludus::sandbox::ocean;
    const auto snapshot = ludus::sandbox::game::BuildUniforms(DefaultSettings(), {}, 1920, 1080, game, true);
    CHECK(Near(snapshot.BoatInfo[0], game.GetBoat().Position.X));
    CHECK(snapshot.GameInfo[0] == 1 && snapshot.GameInfo[1] == 1);
    CHECK(Near(snapshot.Ripples[0][2], game.GetRipples()[0].Age * kRippleSpeed));
    CHECK(snapshot.Ripples[1][0] == 0 && snapshot.Ripples[1][3] == 0);
    game.Reset();
    CHECK(game.Ticks() == 0 && game.Placements() == 0 && game.Contacts() == 0);
    CHECK(Near(game.GetBoat().Position.X, 0));
}
} // namespace

int main()
{
    TestContact();
    TestPushAndDrag();
    TestInputAndClock();
    TestPresentationRates();
    TestCameraAndSnapshot();
    TestPhysicalCurrentAndTuning();
    std::printf("Ripple game: %d checks, %d failures\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
