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
    CHECK(game.GetBoat().Position.X > 0.2);
    CHECK(Near(game.GetBoat().Position.Y, 0));
    CHECK(game.Contacts() == 1);
    const float64 velocity = game.GetBoat().Velocity.X;
    for (int i = 0; i < 120; ++i)
        game.Tick();
    CHECK(game.Contacts() == 1);
    CHECK(game.GetBoat().Velocity.X < velocity);
    CHECK(game.ActiveRipples() == 0);
    CHECK(game.GetBoat().Velocity.X < 0); // The passing trough reverses the local flow.

    // A finite pressure packet rocks the hull; its return flow can reverse motion.
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
    CHECK(game.GetBoat().Position.X < -0.2);
    game.Reset();
    (void)game.Place({0, -5});
    for (int i = 0; i < 60; ++i)
        game.Tick();
    CHECK(game.GetBoat().Position.Y > 0.2);
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
    CHECK(std::abs(game.GetBoat().Position.X) < 1e-12); // Negligible drag exchanges negligible water momentum.
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

void Circle(RippleGame& game, Point center, float64 sign) noexcept
{
    constexpr float64 radius = 6.0;
    (void)game.BeginStroke({center.X + radius, center.Y});
    for (int i = 1; i <= 48; ++i)
    {
        const float64 angle = sign * static_cast<float64>(i) * 6.283185307179586 / 48.0;
        (void)game.MoveStroke({center.X + std::cos(angle) * radius, center.Y + std::sin(angle) * radius});
    }
    (void)game.EndStroke({center.X + radius, center.Y});
}

float64 Circulation(const WaterField& field, Point center, float64 radius) noexcept
{
    float64 result = 0.0;
    for (usize i = 0; i < 64; ++i)
    {
        const float64 angle = (static_cast<float64>(i) + 0.5) * 6.283185307179586 / 64.0;
        const auto sample = field.Sample({center.X + radius * std::cos(angle), center.Y + radius * std::sin(angle)});
        result += (-sample.VelocityX * std::sin(angle) + sample.VelocityY * std::cos(angle)) * radius *
                  6.283185307179586 / 64.0;
    }
    return result;
}
void TestSurfaceGestures() noexcept
{
    RippleGame game;
    CHECK(game.GetWater().Ready() && !game.GetWater().Active());
    CHECK(game.BeginStroke({-5, 0}) == PlacementResult::Queued);
    for (usize i = 0; i < 20; ++i)
    {
        (void)game.MoveStroke({-5.0 + (i % 2 == 0 ? 0.1 : -0.1), 0});
    }
    CHECK(game.EndStroke({-5, 0}) == PlacementResult::Queued);
    game.Tick();
    CHECK(game.Placements() == 1 && game.GetWater().Diagnostics().Energy > 0);
    game.Reset();
    (void)game.BeginStroke({-5, 0});
    game.CancelInput();
    CHECK(game.EndStroke({-5, 0}) == PlacementResult::Inactive);
    game.Tick();
    CHECK(game.Placements() == 0);
    CHECK(game.BeginStroke({31, 0}) == PlacementResult::Outside);
    CHECK(game.BeginStroke({std::numeric_limits<float64>::quiet_NaN(), 0}) == PlacementResult::Outside);
    (void)game.BeginStroke({-10, 0});
    CHECK(game.EndStroke({-2, 0}) == PlacementResult::Placed);
    CHECK(game.Placements() == 0);
    CHECK(game.GetWater().Active() && game.GetWater().Diagnostics().Energy > 10.0);
    CHECK(game.GetWater().Diagnostics().MaxDivergence > 0.1);
    CHECK(Near(game.GetWater().Diagnostics().MaxHeight, 0)); // Only momentum was injected.
    game.Tick();
    CHECK(game.GetWater().Diagnostics().MaxHeight > 0.01); // Continuity creates the surface wave.
    for (usize i = 0; i < 119; ++i)
    {
        game.Tick();
    }
    CHECK(game.GetBoat().Position.X > 0.1);
    CHECK(std::abs(game.GetBoat().Position.Y) < 0.01);
    CHECK(game.Contacts() == 0); // Transport comes from the velocity field.
    CHECK(std::abs(game.GetWater().Sample({-5, 0}).DisplacementX) > 0.05);
    // Height has reached previously undisturbed water after release.
    CHECK(std::abs(game.GetWater().Sample({7, 0}).Height) > 0.001);
    game.Reset();
    (void)game.BeginStroke({10, 0});
    (void)game.EndStroke({2, 0});
    for (usize i = 0; i < 120; ++i)
    {
        game.Tick();
    }
    CHECK(game.GetBoat().Position.X < -0.1);

    for (const float64 sign : {1.0, -1.0})
    {
        game.Reset();
        Circle(game, {-6, 0}, sign);
        const float64 initial = Circulation(game.GetWater(), {-6, 0}, 6.0);
        CHECK(initial * sign > 20.0);
        CHECK(game.WaterAt({0, 0}).Y * sign > 1.0);
        for (usize i = 0; i < 60; ++i)
        {
            game.Tick();
        }
        CHECK(game.GetBoat().Position.Y * sign > 0.1);
        CHECK(Circulation(game.GetWater(), {-6, 0}, 6.0) * sign > 10.0);
        CHECK(game.GetWater().Sample({0, 0}).DisplacementY * sign > 0.05);
        const auto frozen = game.GetWater().Diagnostics();
        game.Advance(10.0, false);
        CHECK(Near(game.GetWater().Diagnostics().Energy, frozen.Energy));
        for (usize i = 0; i < 600; ++i)
        {
            game.Tick();
        }
        CHECK(game.GetWater().Diagnostics().Energy < frozen.Energy * 0.5);
    }
    // Opposite forcing combines in the existing field, with no emitter slots.
    WaterField field;
    CHECK(field.Reset({}));
    CHECK(field.Stroke({-10, 0}, {10, 0}));
    const auto energy = field.Diagnostics().Energy;
    CHECK(field.Stroke({10, 0}, {-10, 0}));
    CHECK(field.Diagnostics().Energy < energy * 1e-8);
    // Resampling the same path must not amplify force with pointer frequency.
    WaterField sparse, dense;
    CHECK(sparse.Reset({}));
    CHECK(dense.Reset({}));
    CHECK(sparse.Stroke({-10, 0}, {10, 0}));
    for (usize i = 0; i < 100; ++i)
    {
        CHECK(dense.Stroke({-10.0 + i * 0.2, 0}, {-10.0 + (i + 1) * 0.2, 0}));
    }
    CHECK(std::abs(sparse.Diagnostics().Energy / dense.Diagnostics().Energy - 1.0) < 0.005);
    CHECK(!field.Stroke({std::numeric_limits<float64>::infinity(), 0}, {0, 0}));
    CHECK(!field.Stroke({0, 0}, {0, 0}));

    // The free surface uses paired face fluxes: an initially flat closed
    // domain keeps zero integrated height while a released wave evolves.
    float64 mass = 0.0;
    for (usize i = 0; i < 120; ++i)
    {
        sparse.Advance(kTickSeconds);
    }
    for (usize y = 0; y < 64; ++y)
    {
        for (usize x = 0; x < 48; ++x)
        {
            mass += sparse.Sample({(x + 0.5) * 1.25 - 30.0, (y + 0.5) * 1.25 - 40.0}).Height;
        }
    }
    CHECK(std::abs(mass) < 1e-7);
    const auto previous = sparse.Diagnostics();
    LevelDefinition invalid;
    invalid.HalfExtent.X = std::numeric_limits<float64>::quiet_NaN();
    CHECK(!sparse.Reset(invalid));
    CHECK(Near(sparse.Diagnostics().Energy, previous.Energy));
    invalid = {};
    invalid.RockCount = kRockCapacity + 1;
    CHECK(!sparse.Reset(invalid));

    // Packing stays exactly representable and samples the solver, including zero.
    game.Reset();
    const auto calm = BuildUniforms(ludus::sandbox::ocean::DefaultSettings(), {}, 960, 540, game, true);
    CHECK(sizeof(calm) == 16128 && calm.FlowInfo[0] == 16 && calm.FlowInfo[1] == 24 && calm.FlowInfo[2] == 0);
    CHECK(calm.Flow[0][0] == 8421504.0F); // three signed zero channels (128).
    Circle(game, {-6, 0}, 1.0);
    const auto snapshot = BuildUniforms(ludus::sandbox::ocean::DefaultSettings(), {}, 960, 540, game, true);
    CHECK(snapshot.FlowInfo[2] == 10);
    bool changed = false;
    for (usize i = 0; i < 192; ++i)
    {
        for (usize j = 0; j < 4; ++j)
        {
            CHECK(snapshot.Flow[i][j] >= 0 && snapshot.Flow[i][j] < 16777216.0F);
            CHECK(std::floor(snapshot.Flow[i][j]) == snapshot.Flow[i][j]);
            changed |= snapshot.Flow[i][j] != calm.Flow[i][j];
        }
    }
    CHECK(changed);
}
void TestPhysicalWaves() noexcept
{
    WaterField left, right, together, opposed;
    CHECK(left.Reset({}) && right.Reset({}) && together.Reset({}) && opposed.Reset({}));
    CHECK(left.Splash({-8, 0}, 0.01));
    CHECK(right.Splash({8, 0}, 0.01));
    CHECK(together.Splash({-8, 0}, 0.01) && together.Splash({8, 0}, 0.01));
    CHECK(opposed.Splash({-8, 0}, 0.01) && opposed.Splash({8, 0}, -0.01));
    CHECK(Near(left.Diagnostics().Energy, 0)); // No instantaneous impulse.
    for (usize i = 0; i < 100; ++i)
    {
        left.Advance(kTickSeconds);
        right.Advance(kTickSeconds);
        together.Advance(kTickSeconds);
        opposed.Advance(kTickSeconds);
    }
    float64 error = 0.0, signal = 0.0, mass = 0.0;
    for (usize y = 0; y < kWaterHeight; ++y)
    {
        for (usize x = 0; x < kWaterWidth; ++x)
        {
            const float64 a = left.SurfaceCell(x, y).Height + right.SurfaceCell(x, y).Height;
            const float64 b = together.SurfaceCell(x, y).Height;
            error += (a - b) * (a - b);
            signal += a * a;
            mass += b;
        }
    }
    CHECK(signal > 1e-5 && error < signal * 0.01); // Small waves superpose, including spatial interference.
    CHECK(std::abs(mass) < 1e-8);                  // Pressure adds energy, not water volume.
    const float64 constructive = together.Sample({0, 0}).Height;
    CHECK(std::abs(constructive) > 1e-4);
    CHECK(std::abs(opposed.Sample({0, 0}).Height) < std::abs(constructive) * 0.05);
    CHECK(left.Diagnostics().MaxHeight > 0.001); // Packet persists after forcing ends.
    CHECK(!left.Splash({31, 0}, 1.0));
    CHECK(!left.Splash({0, 0}, std::numeric_limits<float64>::infinity()));

    WaterField sea;
    CHECK(sea.SetSeaState(0.7));
    CHECK(sea.Reset({}));
    const auto first = sea.Diagnostics();
    CHECK(first.Energy > 10.0 && first.MaxHeight > 0.1);
    CHECK(std::abs(sea.Sample({15, 20}).Height) > 0.005);
    CHECK(std::abs(sea.Sample({-15, -20}).Height) > 0.005);
    for (usize i = 0; i < 900; ++i)
        sea.Advance(kTickSeconds);
    CHECK(sea.Diagnostics().Energy > 1.0 && sea.Diagnostics().Energy < first.Energy * 5.0);
    CHECK(!Near(sea.Sample({15, 20}).Height, 0));
    CHECK(!sea.SetSeaState(-1) && Near(sea.SeaState(), 0.7));
    CHECK(!sea.SetSeaState(std::numeric_limits<float64>::quiet_NaN()));
    CHECK(sea.SetSeaState(0));
    CHECK(sea.Active() && sea.Diagnostics().Energy > 0); // Existing waves survive wind changes.

    RippleGame game;
    CHECK(game.SetSeaState(0.7));
    game.Reset();
    const auto boat = game.GetBoat().Position;
    const float64 before = game.GetWater().Sample({15, 20}).Height;
    for (usize i = 0; i < 60; ++i)
        game.Advance(kTickSeconds, true, false);
    CHECK(Near(game.GetBoat().Position.X, boat.X) && Near(game.GetBoat().Position.Y, boat.Y));
    CHECK(!Near(game.GetWater().Sample({15, 20}).Height, before)); // Ocean mode keeps simulating.
    const auto snapshot = BuildUniforms(ludus::sandbox::ocean::DefaultSettings(), {}, 960, 540, game, true);
    const auto cell = game.GetWater().SurfaceCell(47, 63);
    const auto packed = static_cast<uint32>(snapshot.Surface[767][3]);
    CHECK(std::abs((static_cast<float64>(packed % 65536U) - 32768.0) / 32767.0 * 6.0 - cell.Height) < 0.0001);
}
void TestSurfaceLevelRules() noexcept
{
    RippleGame game;
    CHECK(game.LoadLevel(RescueLevel()));
    CHECK(game.BeginStroke({0, 0}) == PlacementResult::Outside);
    CHECK(game.BeginStroke({-10, 0}) == PlacementResult::Queued);
    CHECK(game.EndStroke({0, 0}) == PlacementResult::Outside);
    CHECK(Near(game.GetWater().Diagnostics().Energy, 0));
    // Even down/up events with valid endpoints cannot tunnel through a rock.
    (void)game.BeginStroke({-10, 0});
    CHECK(game.EndStroke({10, 0}) == PlacementResult::Outside);
    CHECK(Near(game.GetWater().Diagnostics().Energy, 0));
    (void)game.BeginStroke({-10, -22});
    CHECK(game.EndStroke({-2, -22}) == PlacementResult::Placed);
    for (usize i = 0; i < 180; ++i)
    {
        game.Tick();
    }
    CHECK(Near(game.WaterAt({0, 0}).X, 0));
    CHECK(Near(game.WaterAt({30, 0}).X, 0));
    CHECK(Near(game.WaterAt({0, 40}).Y, 0));
    game.Reset();
    CHECK(Near(game.GetWater().Diagnostics().Energy, 0));

    PhysicsSettings physics;
    physics.DragRate = 0.0;
    physics.MaxBoatSpeed = 100.0;
    CHECK(game.SetPhysics(physics));
    LevelDefinition level;
    level.Spawn = {-3.1, 0};
    level.SpawnVelocity = {100, 0};
    level.RockCount = 1;
    level.Rocks[0] = {.Center = {0, 0}, .Radius = 1, .Id = 1};
    CHECK(game.LoadLevel(level));
    (void)game.BeginStroke({-10, -10});
    (void)game.EndStroke({-2, -10});
    game.Tick();
    CHECK(game.Phase() == GamePhase::Crashed);
    const auto frozen = game.GetWater().Diagnostics();
    CHECK(frozen.MaxHeight > 0 && frozen.MaxHeight < 0.1); // Solver stepped only to the collision.
    CHECK(game.BeginStroke({-10, -10}) == PlacementResult::Inactive);
    game.Advance(1.0, true);
    CHECK(Near(game.GetWater().Diagnostics().Energy, frozen.Energy));
    game.Reset();
    CHECK(Near(game.GetWater().Diagnostics().Energy, 0));

    // Small accepted domains need CFL substeps. Repeated strong overlapping
    // strokes and reflected waves stay finite on this worst-resolution grid.
    WaterField water;
    level = {};
    level.HalfExtent = {2.1, 2.1};
    CHECK(water.Reset(level));
    for (usize i = 0; i < 120; ++i)
    {
        CHECK(water.Stroke({-1, 0}, {1, 0}));
        water.Advance(kTickSeconds);
    }
    const auto diagnostics = water.Diagnostics();
    CHECK(std::isfinite(diagnostics.Energy) && std::isfinite(diagnostics.MaxCurl));
    // Depth remains positive even under extreme repeated forcing in a small basin.
    for (usize y = 0; y < kWaterHeight; ++y)
        for (usize x = 0; x < kWaterWidth; ++x)
            CHECK(water.SurfaceCell(x, y).Height > -4.0);
    water.Advance(std::numeric_limits<float64>::quiet_NaN());
    CHECK(Near(water.Diagnostics().Energy, diagnostics.Energy));
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

void TestRockOrigins() noexcept
{
    RippleGame game;
    auto level = RescueLevel();
    CHECK(game.LoadLevel(level));
    CHECK(game.Place({0.0, 0.0}) == PlacementResult::Outside);
    CHECK(game.Place({5.0, 0.0}) == PlacementResult::Outside);
    CHECK(game.Place({4.99, 0.0}) == PlacementResult::Outside);
    game.Tick();
    CHECK(game.Placements() == 0 && game.ActiveRipples() == 0);
    CHECK(Near(game.CooldownFraction(), 0.0));
    CHECK(game.Place({5.01, 0.0}) == PlacementResult::Queued);
    CHECK(game.Place({0.0, 0.0}) == PlacementResult::Outside);
    game.Tick();
    CHECK(game.Placements() == 1 && game.ActiveRipples() == 1);
    CHECK(game.CooldownFraction() > 0.0); // Rejection preserved valid queued input.
    CHECK(Near(game.GetRipples()[0].Origin.X, 5.01));

    level.RockCount = 2;
    level.Rocks[1] = {.Center = {12.0, 0.0}, .Radius = 1.0, .Id = 2};
    CHECK(game.LoadLevel(level));
    CHECK(game.Place({12.0, 0.0}) == PlacementResult::Outside);
    CHECK(game.Place({11.0, 0.0}) == PlacementResult::Outside);
    game.Tick();
    CHECK(game.Placements() == 0 && game.ActiveRipples() == 0);
    CHECK(Near(game.CooldownFraction(), 0.0));
    CHECK(game.Place({10.99, 0.0}) == PlacementResult::Queued);
    game.Tick();
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
    (void)game.Place({1.1, 0.0}); // This band would touch AFTER the crash.
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

// Earn arrivals through the same placements available to players. This checks
// that authored routes can be sailed and braked with the actual fixed-tick rules.
bool Sail(RippleGame& game, const Point* waypoints, usize count) noexcept
{
    usize waypoint = 0;
    for (uint32 tick = 0; tick < 18000 && game.Phase() == GamePhase::Playing; ++tick)
    {
        const auto& boat = game.GetBoat();
        const Point delta{waypoints[waypoint].X - boat.Position.X, waypoints[waypoint].Y - boat.Position.Y};
        const float64 distance = std::hypot(delta.X, delta.Y);
        if (distance < 2.5 && waypoint + 1 < count)
        {
            ++waypoint;
            continue;
        }
        const float64 speed = distance * 0.6 < 3.2 ? distance * 0.6 : 3.2;
        const Point error{(distance > 0.01 ? delta.X / distance * speed : 0.0) - boat.Velocity.X,
                          (distance > 0.01 ? delta.Y / distance * speed : 0.0) - boat.Velocity.Y};
        const float64 magnitude = std::hypot(error.X, error.Y);
        if (game.CooldownFraction() == 0.0 && magnitude > 1.1)
        {
            (void)game.Place(
                {boat.Position.X - error.X / magnitude * 3.0, boat.Position.Y - error.Y / magnitude * 3.0});
        }
        game.Tick();
    }
    return game.Phase() == GamePhase::Arrived;
}

void TestCampaign() noexcept
{
    LevelDefinition output = RescueLevel();
    CHECK(!TryGetCourseLevel(kCourseCount, output));
    CHECK(output.RockCount == 1 && Near(output.DockCenter.Y, 24.0));
    RippleGame game;
    for (uint32 course = 0; course < kCourseCount; ++course)
    {
        CHECK(TryGetCourseLevel(course, output));
        CHECK(game.LoadLevel(output));
        CHECK(!game.CampaignActive());
        CHECK(output.Id == course + 1 && output.BoundaryHazard);
    }
    CHECK(game.StartCampaign());
    CHECK(game.CampaignActive() && game.CourseIndex() == 0 && !game.CampaignComplete());
    CHECK(game.Place({-3.0, -22.0}) == PlacementResult::Queued);
    CHECK(!game.NextCourse());
    game.Tick();
    CHECK(game.Placements() == 1); // Failed transition preserves queued input.
    game.Reset();
    CHECK(game.CampaignActive() && game.CourseIndex() == 0 && game.ActiveRipples() == 0);
    auto invalid = game.GetLevel();
    invalid.Id = 0;
    CHECK(!game.LoadLevel(invalid));
    CHECK(game.CampaignActive() && game.CourseIndex() == 0);

    const Point first[] = {{8.0, 10.0}};
    CHECK(Sail(game, first, 1));
    CHECK(!game.CampaignComplete() && game.TerminalTransitions() == 1);
    game.Reset(); // Retry after arrival stays on the same course.
    CHECK(game.Phase() == GamePhase::Playing && game.CourseIndex() == 0);
    CHECK(Sail(game, first, 1));
    PhysicsSettings physics;
    physics.PushSpeed = 4.0;
    CHECK(game.SetPhysics(physics));
    CHECK(game.NextCourse());
    CHECK(game.CourseIndex() == 1 && Near(game.GetPhysics().PushSpeed, 2.5));
    CHECK(game.Ticks() == 0 && game.Placements() == 0 && game.Contacts() == 0);
    CHECK(game.ActiveRipples() == 0 && Near(game.Alpha(), 0.0) && Near(game.DockProgress(), 0.0));
    const Point straight[] = {{0.0, 24.0}};
    CHECK(!Sail(game, straight, 1));
    CHECK(game.Phase() == GamePhase::Crashed && !game.NextCourse());
    game.Reset();
    CHECK(game.CourseIndex() == 1 && Near(game.GetBoat().Position.Y, -22.0));
    const Point second[] = {{12.0, -22.0}, {12.0, 24.0}, {0.0, 24.0}};
    CHECK(Sail(game, second, 3));
    CHECK(game.NextCourse());
    CHECK(game.CourseIndex() == 2 && game.GetLevel().RockCount == 3);
    CHECK(Near(game.GetBoat().Position.Y, -28.0));
    const Point channel[] = {
        {12.0, -28.0},
        {12.0, -12.0},
        {0.0, -2.0},
        {-12.0, 7.0},
        {-12.0, 12.0},
        {12.0, 18.0},
        {10.0, 29.0},
    };
    CHECK(Sail(game, channel, 7));
    CHECK(game.CampaignComplete() && game.TerminalTransitions() == 1);
    const auto ticks = game.Ticks();
    CHECK(!game.NextCourse());
    game.Advance(1.0, true);
    CHECK(game.CampaignComplete() && game.Ticks() == ticks);
    using namespace ludus::sandbox::ocean;
    const auto finalSnapshot = BuildUniforms(DefaultSettings(), {}, 960, 540, game, false);
    CHECK(finalSnapshot.LevelInfo[0] == 3.0F && finalSnapshot.LevelInfo[1] == 2.0F &&
          finalSnapshot.Rocks[2][2] == 4.0F);
    game.Reset();
    CHECK(game.CourseIndex() == 2 && !game.CampaignComplete());
    CHECK(game.BeginStroke({15.0, -20.0}) == PlacementResult::Queued);
    (void)game.MoveStroke({20.0, -20.0});
    CHECK(game.GetWater().Diagnostics().Energy > 0.0);
    game.Advance(kTickSeconds * 0.5, true);
    CHECK(game.StartCampaign());
    CHECK(game.CourseIndex() == 0 && Near(game.GetWater().Diagnostics().Energy, 0));
    CHECK(game.ActiveRipples() == 0);
    CHECK(Near(game.Alpha(), 0.0) && Near(game.GetBoat().Velocity.X, 0.0));
    game.Tick();
    CHECK(game.Placements() == 0); // A stroke from the previous run cannot leak.
    const auto replaySnapshot = BuildUniforms(DefaultSettings(), {}, 960, 540, game, false);
    CHECK(replaySnapshot.LevelInfo[1] == 0.0F && replaySnapshot.Rocks[2][2] == 0.0F);
    CHECK(game.LoadLevel(RescueLevel()));
    CHECK(!game.CampaignActive() && !game.NextCourse());
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
    TestSurfaceGestures();
    TestSurfaceLevelRules();
    TestPhysicalWaves();
    TestLevelValidation();
    TestRockOrigins();
    TestSweptHazardsAndRetry();
    TestDocking();
    TestCampaign();
    std::printf("Ripple game: %d checks, %d failures\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
