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
void TestLevelValidation() noexcept
{
    RippleGame game;
    const auto course = RescueLevel();
    CHECK(game.LoadLevel(course));
    CHECK(Near(game.GetBoat().Position.Y, -22.0));
    CHECK(game.GetLevel().RockCount == 1);
    (void)game.Place({0.0, -27.0});
    auto bad = course;
    bad.Spawn = {0.0, 0.0};
    CHECK(!game.LoadLevel(bad));
    CHECK(Near(game.GetBoat().Position.Y, -22.0));
    game.Tick();
    CHECK(game.Placements() == 1); // Rejection preserves queued input too.
    bad = course;
    bad.RockCount = kRockCapacity + 1;
    CHECK(!game.LoadLevel(bad));
    bad = course;
    bad.RockCount = 2;
    bad.Rocks[1] = course.Rocks[0];
    CHECK(!game.LoadLevel(bad));
    bad = course;
    bad.Rocks[0].Radius = -1.0;
    CHECK(!game.LoadLevel(bad));
    bad = course;
    bad.Rocks[0].Center.X = std::numeric_limits<float64>::quiet_NaN();
    CHECK(!game.LoadLevel(bad));
    bad = course;
    bad.DockCenter = {0.0, 0.0};
    CHECK(!game.LoadLevel(bad));
    bad = course;
    bad.DockRadius = kBoatRadius;
    CHECK(!game.LoadLevel(bad));
    bad = course;
    bad.HalfExtent.X = std::numeric_limits<float64>::infinity();
    CHECK(!game.LoadLevel(bad));
    bad = course;
    bad.Spawn = {29.0, 0.0};
    CHECK(!game.LoadLevel(bad));
    bad = course;
    bad.SpawnVelocity = {8.0, 0.0};
    CHECK(!game.LoadLevel(bad));
    bad = course;
    bad.DockDwellTicks = 0;
    CHECK(!game.LoadLevel(bad));
    CHECK(game.GetLevel().Id == course.Id);
    CHECK(game.Placements() == 1);
}

void TestSweptHazardsAndRetry() noexcept
{
    float64 time = -1.0;
    CHECK(CircleContact({-100, 0}, {100, 0}, {0, 0}, 3, time));
    CHECK(Near(time, 0.485)); // Both endpoints miss; the full sweep hits.
    CHECK(CircleContact({-4, 3}, {4, 3}, {0, 0}, 3, time));
    CHECK(Near(time, 0.5));
    CHECK(CircleContact({0, 0}, {0, 0}, {0, 0}, 3, time));
    CHECK(Near(time, 0));
    time = -1.0;
    CHECK(!CircleContact({-4, 4}, {4, 4}, {0, 0}, 3, time));
    CHECK(Near(time, -1));
    CHECK(!CircleContact({0, 0}, {1, 0}, {0, 0}, -1, time));
    CHECK(!CircleContact({std::numeric_limits<float64>::quiet_NaN(), 0}, {1, 0}, {0, 0}, 1, time));
    CHECK(Near(time, -1));

    RippleGame game;
    PhysicsSettings physics;
    physics.DragRate = 0.0;
    physics.MaxBoatSpeed = 100.0;
    CHECK(game.SetPhysics(physics));
    LevelDefinition level;
    level.Spawn = {-3.1, 0.0};
    level.SpawnVelocity = {100.0, 0.0};
    level.RockCount = 1;
    level.Rocks[0] = {.Center = {0.0, 0.0}, .Radius = 1.0, .Id = 1};
    CHECK(game.LoadLevel(level));
    (void)game.Place({-0.4, 0.0}); // This band would touch AFTER the crash.
    game.Advance(0.1, true);
    CHECK(game.Phase() == GamePhase::Crashed);
    CHECK(game.Crash() == CrashReason::Rock);
    CHECK(Near(game.GetBoat().Position.X, -3.0));
    CHECK(game.Contacts() == 0);
    CHECK(game.Ticks() == 1);
    CHECK(game.TerminalTransitions() == 1);
    CHECK(Near(game.Alpha(), 0.0));
    const auto age = game.GetRipples()[0].Age;
    CHECK(Near(age, 0.001)); // Rings freeze at the same collision time.
    CHECK(game.Place({-5, 0}) == PlacementResult::Inactive);
    game.Tick();
    game.Advance(1.0, true);
    CHECK(game.Ticks() == 1 && game.TerminalTransitions() == 1);
    CHECK(Near(game.GetRipples()[0].Age, age));
    using namespace ludus::sandbox::ocean;
    const auto snapshot = BuildUniforms(DefaultSettings(), {}, 960, 540, game, false);
    CHECK(Near(snapshot.BoatInfo[0], -3.0)); // Terminal transforms never interpolate across a rock.
    CHECK(snapshot.LevelInfo[1] == 1.0F);
    CHECK(snapshot.Rocks[0][2] == 1.0F);
    CHECK(snapshot.Rocks[1][2] == 0.0F);

    for (uint32 retry = 0; retry < 30; ++retry)
    {
        game.Reset();
        CHECK(game.Phase() == GamePhase::Playing);
        CHECK(game.Crash() == CrashReason::None);
        CHECK(game.Ticks() == 0 && game.Contacts() == 0 && game.Placements() == 0);
        CHECK(game.ActiveRipples() == 0 && Near(game.CooldownFraction(), 0));
        CHECK(Near(game.GetBoat().Position.X, level.Spawn.X));
        CHECK(Near(game.GetBoat().PreviousPosition.X, level.Spawn.X));
        CHECK(Near(game.GetBoat().Velocity.X, 100.0));
        (void)game.Place(level.Spawn); // Contact at t=0 is eligible before the crash.
        game.Tick();
        CHECK(game.Contacts() == 1 && game.TerminalTransitions() == 1);
        CHECK(game.GetRipples()[0].Id == 1);
    }

    level.RockCount = 0;
    level.BoundaryHazard = true;
    level.Spawn = {27.9, 0.0};
    CHECK(game.LoadLevel(level));
    game.Tick();
    CHECK(game.Phase() == GamePhase::Crashed && game.Crash() == CrashReason::Boundary);
    CHECK(Near(game.GetBoat().Position.X, 28.0));
    CHECK(Near(game.GetBoat().Position.Y, 0.0));
    level.Spawn = {0.0, -37.9};
    level.SpawnVelocity = {0.0, -100.0};
    CHECK(game.LoadLevel(level));
    game.Tick();
    CHECK(game.Crash() == CrashReason::Boundary);
    CHECK(Near(game.GetBoat().Position.Y, -38.0));
}

void TestDocking() noexcept
{
    RippleGame game;
    PhysicsSettings physics;
    physics.DragRate = 0.0;
    CHECK(game.SetPhysics(physics));
    LevelDefinition level;
    level.DockRadius = 5.0;
    level.SpawnVelocity = {1.5, 0.0};
    CHECK(game.LoadLevel(level));
    for (uint32 i = 0; i < 23; ++i)
        game.Tick();
    CHECK(game.Phase() == GamePhase::Playing);
    CHECK(Near(game.DockProgress(), 23.0 / 24.0));
    game.Advance(0.1, false);
    CHECK(Near(game.DockProgress(), 23.0 / 24.0));
    game.Tick();
    CHECK(game.Phase() == GamePhase::Arrived);
    CHECK(Near(game.DockProgress(), 1.0));
    CHECK(game.TerminalTransitions() == 1);
    CHECK(game.Place({1.0, 0.0}) == PlacementResult::Inactive);
    game.Advance(1.0, true);
    game.Tick();
    CHECK(game.Ticks() == 24 && game.TerminalTransitions() == 1);
    game.Reset();
    CHECK(Near(game.DockProgress(), 0));
    CHECK(game.Phase() == GamePhase::Playing);

    level.SpawnVelocity = {2.0, 0.0};
    CHECK(game.LoadLevel(level));
    for (uint32 i = 0; i < 24; ++i)
        game.Tick();
    CHECK(game.Phase() == GamePhase::Playing && Near(game.DockProgress(), 0));
    level.Spawn = {2.9, 0.0};
    level.SpawnVelocity = {1.5, 0.0};
    CHECK(game.LoadLevel(level));
    game.Tick();
    CHECK(game.DockProgress() > 0);
    for (uint32 i = 0; i < 5; ++i)
        game.Tick();
    CHECK(Near(game.DockProgress(), 0)); // The WHOLE hull must remain inside.

    level.Spawn = {};
    level.SpawnVelocity = {};
    CHECK(game.LoadLevel(level));
    for (uint32 i = 0; i < 10; ++i)
        game.Tick();
    physics.DragRate = 100.0;
    physics.WaterVelocity = {7.0, 0.0};
    CHECK(game.SetPhysics(physics));
    game.Tick();
    CHECK(Near(game.DockProgress(), 0)); // Excess speed resets accumulated dwell.

    physics.DragRate = 0.0;
    physics.WaterVelocity = {};
    CHECK(game.SetPhysics(physics));
    level.DockCenter = {24.9, 0.0};
    level.Spawn = {27.7, 0.0};
    level.SpawnVelocity = {1.5, 0.0};
    level.BoundaryHazard = true;
    CHECK(game.LoadLevel(level));
    for (uint32 i = 0; i < 20; ++i)
        game.Tick();
    CHECK(game.Phase() == GamePhase::Crashed); // Brief dock overlap cannot defeat a crash.
    CHECK(Near(game.DockProgress(), 0));
    CHECK(game.TerminalTransitions() == 1);
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
    TestLevelValidation();
    TestSweptHazardsAndRetry();
    TestDocking();
    std::printf("Ripple game: %d checks, %d failures\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
