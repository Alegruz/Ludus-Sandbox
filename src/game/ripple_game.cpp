#include "game/ripple_game.h"

#if defined(LUDUS_SANDBOX_WITH_SDK)
#    include <ludus/foundation/math/dynamics.hpp>
#endif

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <limits>

namespace ludus::sandbox::game
{
namespace
{
constexpr float64 kEpsilon = 1e-9;
constexpr float64 kContactBand = kBoatRadius + kRingHalfWidth;

[[nodiscard]] float64 Length(Point p) noexcept
{
    return std::hypot(p.X, p.Y);
}

[[nodiscard]] bool InWater(Point p, Point halfExtent) noexcept
{
    return std::isfinite(p.X) && std::isfinite(p.Y) && std::abs(p.X) <= halfExtent.X && std::abs(p.Y) <= halfExtent.Y;
}
[[nodiscard]] bool ClearWater(Point p, const LevelDefinition& level) noexcept
{
    if (!InWater(p, level.HalfExtent))
    {
        return false;
    }
    for (usize i = 0; i < level.RockCount; ++i)
    {
        const auto& rock = level.Rocks[i];
        if (Length({p.X - rock.Center.X, p.Y - rock.Center.Y}) <= rock.Radius)
        {
            return false;
        }
    }
    return true;
}
[[nodiscard]] bool CircleInside(Point center, float64 radius, Point halfExtent) noexcept
{
    return InWater(center, halfExtent) && std::abs(center.X) + radius < halfExtent.X &&
           std::abs(center.Y) + radius < halfExtent.Y;
}
} // namespace

Camera FitCamera(uint32 width, uint32 height, Point halfExtent) noexcept
{
    if (width == 0 || height == 0)
    {
        return {};
    }
    const float64 aspect = static_cast<float64>(width) / height;
    const float64 fitHeight = (halfExtent.X * 2.0 + 10.0) / aspect;
    const float64 levelHeight = halfExtent.Y * 2.0 + 10.0;
    const float64 viewHeight = fitHeight > levelHeight ? fitHeight : levelHeight;
    return {viewHeight * aspect, viewHeight, {}};
}

Camera FollowCamera(uint32 width, uint32 height, const LevelDefinition& level, Point boat) noexcept
{
    if (width == 0 || height == 0 || level.RiverSpeed <= 0.0)
    {
        return FitCamera(width, height, level.HalfExtent);
    }
    const float64 aspect = static_cast<float64>(width) / height;
    const float64 viewHeight = std::max(60.0, (level.HalfExtent.X * 2.0 + 6.0) / aspect);
    if (viewHeight >= level.HalfExtent.Y * 2.0)
    {
        return FitCamera(width, height, level.HalfExtent);
    }
    const float64 limit = level.HalfExtent.Y - viewHeight * 0.5 + 4.0;
    return {viewHeight * aspect, viewHeight, {0.0, std::clamp(boat.Y + viewHeight * 0.20, -limit, limit)}};
}

Point RiverCurrent(const LevelDefinition& level, Point world) noexcept
{
    if (level.RiverSpeed <= 0.0 || !ClearWater(world, level))
    {
        return {};
    }
    const float64 lane = world.X / level.HalfExtent.X;
    float64 speed = level.RiverSpeed * (1.0 - 0.35 * lane * lane);
    for (const float64 center : {-level.HalfExtent.Y * 0.25, level.HalfExtent.Y * 0.25})
    {
        const float64 along = (world.Y - center) / 12.0;
        const float64 envelope = std::max(1.0 - along * along, 0.0);
        speed += level.RiverSpeed * level.RapidsBoost * envelope * envelope;
    }
    if (level.DockRadius > 0.0)
    {
        const float64 distance = Length({world.X - level.DockCenter.X, world.Y - level.DockCenter.Y});
        const float64 fade = std::clamp((distance - level.DockRadius - 2.0) / 8.0, 0.0, 1.0);
        speed *= fade * fade * (3.0 - 2.0 * fade);
    }
    return {0.0, speed};
}

LevelDefinition RescueLevel() noexcept
{
    LevelDefinition level;
    level.Spawn = {0.0, -22.0};
    level.RockCount = 1;
    level.Rocks[0] = {.Center = {0.0, 0.0}, .Radius = 5.0, .Id = 1};
    level.DockCenter = {0.0, 24.0};
    level.DockRadius = 5.0;
    level.BoundaryHazard = true;
    return level;
}

bool TryGetCourseLevel(uint32 index, LevelDefinition& output) noexcept
{
    if (index >= kCourseCount)
    {
        return false;
    }
    LevelDefinition level;
    level.Id = index + 1;
    level.HalfExtent = {22.0 - index * 2.0, 72.0 + index * 16.0};
    level.Spawn = {0.0, -level.HalfExtent.Y + 12.0};
    level.DockCenter = {index == 1 ? -9.0 : 9.0, level.HalfExtent.Y - 13.0};
    level.DockRadius = 6.0;
    level.BoundaryHazard = true;
    level.RiverSpeed = 1.8 + index * 0.45;
    level.RapidsBoost = index * 0.3;
    level.RockCount = 3 + index * 2;
    // Each gate changes the safe lane; the spaces between let players recover.
    level.Rocks[0] = {.Center = {-6.0, -level.HalfExtent.Y + 38.0}, .Radius = 6.0, .Id = 1};
    level.Rocks[1] = {.Center = {6.0, -level.HalfExtent.Y + 68.0}, .Radius = 6.0, .Id = 2};
    level.Rocks[2] = {.Center = {-5.0, -level.HalfExtent.Y + 98.0}, .Radius = 5.0, .Id = 3};
    if (index > 0)
    {
        level.Rocks[3] = {.Center = {6.0, -level.HalfExtent.Y + 128.0}, .Radius = 5.0, .Id = 4};
        level.Rocks[4] = {.Center = {-12.0, -level.HalfExtent.Y + 70.0}, .Radius = 3.0, .Id = 5};
    }
    if (index == 2)
    {
        level.Rocks[5] = {.Center = {-6.0, -level.HalfExtent.Y + 158.0}, .Radius = 5.0, .Id = 6};
        level.Rocks[6] = {.Center = {13.0, -level.HalfExtent.Y + 100.0}, .Radius = 3.0, .Id = 7};
    }
    output = level;
    return true;
}

const char* CourseTitle(uint32 index) noexcept
{
    constexpr const char* titles[kCourseCount] = {"River Mouth", "Rock Gates", "The Rapids"};
    return index < kCourseCount ? titles[index] : "Practice";
}

const char* CourseInstruction(uint32 index) noexcept
{
    constexpr const char* instructions[kCourseCount] = {
        "The river carries you upward. Steer around alternating rocks, then enter the calm green dock.",
        "Pick the open lane before each rock gate. Drag across the flow to steer; arrive slowly.",
        "Faster water leaves less time to react. Watch ahead, weave through the rapids, and reach the calm dock.",
    };
    return index < kCourseCount ? instructions[index] : "Guide the boat with the water.";
}

bool RippleGame::LoadCourse(uint32 index) noexcept
{
    LevelDefinition level;
    if (!TryGetCourseLevel(index, level))
    {
        return false;
    }
    if (!LoadLevel(level))
    {
        return false;
    }
    (void)SetPhysics({});
    mCampaignActive = true;
    mCourseIndex = index;
    return true;
}

bool RippleGame::StartCampaign() noexcept
{
    return LoadCourse(0);
}

bool RippleGame::NextCourse() noexcept
{
    return mCampaignActive && mPhase == GamePhase::Arrived && mCourseIndex + 1 < kCourseCount &&
           LoadCourse(mCourseIndex + 1);
}

bool CircleContact(Point start, Point end, Point origin, float64 radius, float64& time) noexcept
{
    if (!std::isfinite(start.X) || !std::isfinite(start.Y) || !std::isfinite(end.X) || !std::isfinite(end.Y) ||
        !std::isfinite(origin.X) || !std::isfinite(origin.Y) || !std::isfinite(radius) || radius < 0.0)
    {
        return false;
    }
#if defined(LUDUS_SANDBOX_WITH_SDK)
    using namespace ludus::foundation::math;
    RadialSweepContact contact;
    if (!IsSuccess(TrySweepRadialBand({start.X, start.Y, 0.0},
                                      {end.X, end.Y, 0.0},
                                      {origin.X, origin.Y, 0.0},
                                      0.0,
                                      0.0,
                                      radius,
                                      contact)) ||
        !contact.Hit)
    {
        return false;
    }
    time = contact.Fraction;
    return true;
#else
    const Point p{start.X - origin.X, start.Y - origin.Y};
    const Point d{end.X - start.X, end.Y - start.Y};
    const float64 c = p.X * p.X + p.Y * p.Y - radius * radius;
    if (c <= 0.0)
    {
        time = 0.0;
        return true;
    }
    const float64 a = d.X * d.X + d.Y * d.Y;
    const float64 b = 2.0 * (p.X * d.X + p.Y * d.Y);
    const float64 discriminant = b * b - 4.0 * a * c;
    if (a == 0.0 || b >= 0.0 || !std::isfinite(discriminant) || discriminant < 0.0)
    {
        return false;
    }
    // Stable entering root for an initially separated, approaching hull.
    const float64 t = 2.0 * c / (-b + std::sqrt(discriminant));
    if (t < 0.0 || t > 1.0)
    {
        return false;
    }
    time = t;
    return true;
#endif
}

bool RippleGame::LoadLevel(const LevelDefinition& level) noexcept
{
    if (level.Id == 0 || !std::isfinite(level.HalfExtent.X) || !std::isfinite(level.HalfExtent.Y) ||
        level.HalfExtent.X <= kBoatRadius || level.HalfExtent.Y <= kBoatRadius || level.HalfExtent.X > 1000.0 ||
        level.HalfExtent.Y > 1000.0 || !CircleInside(level.Spawn, kBoatRadius, level.HalfExtent) ||
        !std::isfinite(level.SpawnVelocity.X) || !std::isfinite(level.SpawnVelocity.Y) ||
        Length(level.SpawnVelocity) > mPhysics.MaxBoatSpeed || level.RockCount > kRockCapacity ||
        !std::isfinite(level.DockCenter.X) || !std::isfinite(level.DockCenter.Y) || !std::isfinite(level.DockRadius) ||
        level.DockRadius < 0.0 || !std::isfinite(level.DockSpeed) || level.DockSpeed <= 0.0 ||
        level.DockDwellTicks == 0 || level.DockDwellTicks > 600 || !std::isfinite(level.RiverSpeed) ||
        level.RiverSpeed < 0.0 || level.RiverSpeed > 5.0 || !std::isfinite(level.RapidsBoost) ||
        level.RapidsBoost < 0.0 || level.RapidsBoost > 1.0)
    {
        return false;
    }
    if (level.DockRadius > 0.0 &&
        (level.DockRadius <= kBoatRadius || !CircleInside(level.DockCenter, level.DockRadius, level.HalfExtent)))
    {
        return false;
    }
    for (usize i = 0; i < level.RockCount; ++i)
    {
        const auto& rock = level.Rocks[i];
        if (rock.Id == 0 || !std::isfinite(rock.Radius) || rock.Radius <= 0.0 ||
            !CircleInside(rock.Center, rock.Radius, level.HalfExtent) ||
            Length({level.Spawn.X - rock.Center.X, level.Spawn.Y - rock.Center.Y}) <= kBoatRadius + rock.Radius ||
            (level.DockRadius > 0.0 && Length({level.DockCenter.X - rock.Center.X,
                                               level.DockCenter.Y - rock.Center.Y}) <= level.DockRadius + rock.Radius))
        {
            return false;
        }
        for (usize j = 0; j < i; ++j)
        {
            if (rock.Id == level.Rocks[j].Id)
            {
                return false;
            }
        }
    }
    mLevel = level;
    mCampaignActive = false;
    mCourseIndex = 0;
    Reset();
    return true;
}

float64 RippleGame::DockProgress() const noexcept
{
    return static_cast<float64>(mDockTicks) / mLevel.DockDwellTicks;
}

Point ScreenToWorld(Point normalized, Camera camera) noexcept
{
    return {camera.Center.X + (normalized.X - 0.5) * camera.Width,
            camera.Center.Y + (0.5 - normalized.Y) * camera.Height};
}

bool RingContact(Point start, Point end, Point origin, float64 ringStart, float64 ringEnd, float64& time) noexcept
{
#if defined(LUDUS_SANDBOX_WITH_SDK)
    using namespace ludus::foundation::math;
    RadialSweepContact contact;
    const auto status = TrySweepRadialBand({start.X, start.Y, 0.0},
                                           {end.X, end.Y, 0.0},
                                           {origin.X, origin.Y, 0.0},
                                           ringStart,
                                           ringEnd,
                                           kContactBand,
                                           contact);
    if (!IsSuccess(status) || !contact.Hit)
    {
        return false;
    }
    time = contact.Fraction;
    return true;
#else
    // Independent bounded-world reference for SDK-free fast checks. Production
    // and native SDK tests always use FoundationMath above.
    const Point relative{start.X - origin.X, start.Y - origin.Y};
    const Point travel{end.X - start.X, end.Y - start.Y};
    const float64 growth = ringEnd - ringStart;
    float64 first = 2.0;
    const auto candidate = [&](float64 t) noexcept {
        if (t < -kEpsilon || t > 1.0 + kEpsilon)
        {
            return;
        }
        t = t < 0.0 ? 0.0 : (t > 1.0 ? 1.0 : t);
        const float64 distance = Length({relative.X + travel.X * t, relative.Y + travel.Y * t});
        const float64 radius = ringStart + growth * t;
        if (std::abs(distance - radius) <= kContactBand + 1e-7 && t < first)
        {
            first = t;
        }
    };
    candidate(0.0);
    // Solve both boundaries of the swept ring band, not only the endpoints.
    for (const float64 offset : {kContactBand, -kContactBand})
    {
        const float64 radius = ringStart + offset;
        const float64 a = travel.X * travel.X + travel.Y * travel.Y - growth * growth;
        const float64 b = 2.0 * (relative.X * travel.X + relative.Y * travel.Y - radius * growth);
        const float64 c = relative.X * relative.X + relative.Y * relative.Y - radius * radius;
        if (std::abs(a) < kEpsilon)
        {
            if (std::abs(b) > kEpsilon)
            {
                candidate(-c / b);
            }
            continue;
        }
        const float64 discriminant = b * b - 4.0 * a * c;
        if (discriminant < -kEpsilon)
        {
            continue;
        }
        const float64 root = std::sqrt(discriminant < 0.0 ? 0.0 : discriminant);
        candidate((-b - root) / (2.0 * a));
        candidate((-b + root) / (2.0 * a));
    }
    if (first > 1.0)
    {
        return false;
    }
    time = first;
    return true;
#endif
}

RippleGame::RippleGame() noexcept
{
    (void)SetPhysics({});
    (void)mWater.Reset(mLevel);
}

bool RippleGame::SetPhysics(const PhysicsSettings& settings) noexcept
{
    if (!std::isfinite(settings.DragRate) || settings.DragRate < 0.0 || settings.DragRate > 100.0 ||
        !std::isfinite(settings.PushSpeed) || settings.PushSpeed < 0.0 || settings.PushSpeed > 100.0 ||
        !std::isfinite(settings.MaxBoatSpeed) || settings.MaxBoatSpeed <= 0.0 || settings.MaxBoatSpeed > 100.0 ||
        !std::isfinite(settings.WaterVelocity.X) || !std::isfinite(settings.WaterVelocity.Y) ||
        Length(settings.WaterVelocity) > settings.MaxBoatSpeed || Length(mLevel.SpawnVelocity) > settings.MaxBoatSpeed)
    {
        return false;
    }
    float64 velocityScale = 1.0;
    float64 distanceScale = kTickSeconds;
#if defined(LUDUS_SANDBOX_WITH_SDK)
    using namespace ludus::foundation::math;
    LinearDragStep step;
    if (!IsSuccess(TryComputeLinearDragStep(settings.DragRate, kTickSeconds, step)))
    {
        return false;
    }
    velocityScale = step.VelocityScale;
    distanceScale = step.DistanceScale;
#else
    if (settings.DragRate > 0.0)
    {
        velocityScale = std::exp(-settings.DragRate * kTickSeconds);
        distanceScale = -std::expm1(-settings.DragRate * kTickSeconds) / settings.DragRate;
    }
#endif
    mPhysics = settings;
    mVelocityScale = velocityScale;
    mDistanceScale = distanceScale;
    return true;
}

PlacementResult RippleGame::Place(Point world) noexcept
{
    if (mPhase != GamePhase::Playing)
    {
        mLastPlacement = PlacementResult::Inactive;
    }
    else if (!ClearWater(world, mLevel))
    {
        mLastPlacement = PlacementResult::Outside;
    }
    else if (mInputCount == kInputCapacity)
    {
        mLastPlacement = PlacementResult::Capacity;
    }
    else
    {
        mInput[mInputCount++] = world;
        mLastPlacement = PlacementResult::Queued;
    }
    return mLastPlacement;
}

PlacementResult RippleGame::BeginStroke(Point world) noexcept
{
    if (mPhase != GamePhase::Playing)
    {
        mLastPlacement = PlacementResult::Inactive;
        return mLastPlacement;
    }
    if (!ClearWater(world, mLevel))
    {
        mLastPlacement = PlacementResult::Outside;
        return mLastPlacement;
    }
    if (!mWater.Ready())
    {
        mLastPlacement = PlacementResult::Capacity;
        return mLastPlacement;
    }
    mStroking = true;
    mStrokeStart = world;
    mStrokePrevious = world;
    mStrokeDistance = 0.0;
    mStrokeFlowing = false;
    return PlacementResult::Queued;
}

PlacementResult RippleGame::MoveStroke(Point world) noexcept
{
    if (!mStroking || mPhase != GamePhase::Playing)
    {
        return PlacementResult::Inactive;
    }
    bool clear = ClearWater(world, mLevel);
    for (usize i = 0; i < mLevel.RockCount && clear; ++i)
    {
        float64 time = 0.0;
        clear = !CircleContact(mStrokePrevious, world, mLevel.Rocks[i].Center, mLevel.Rocks[i].Radius, time);
    }
    if (!clear)
    {
        mStroking = false;
        mLastPlacement = PlacementResult::Outside;
        return mLastPlacement;
    }
    const float64 distance = Length({world.X - mStrokePrevious.X, world.Y - mStrokePrevious.Y});
    if (distance < 0.4)
    {
        return mStrokeFlowing ? PlacementResult::Placed : PlacementResult::Queued;
    }
    mStrokeDistance += distance;
    if (mStrokeDistance >= 2.5)
    {
        const Point start = mStrokeFlowing ? mStrokePrevious : mStrokeStart;
        if (!mWater.Stroke(start, world))
        {
            mLastPlacement = PlacementResult::Outside;
            mStroking = false;
            return mLastPlacement;
        }
        mStrokeFlowing = true;
        mLastPlacement = PlacementResult::Placed;
    }
    mStrokePrevious = world;
    return mStrokeFlowing ? PlacementResult::Placed : PlacementResult::Queued;
}

PlacementResult RippleGame::EndStroke(Point world) noexcept
{
    const auto result = MoveStroke(world);
    if (!mStroking)
    {
        return result;
    }
    mStroking = false;
    return mStrokeFlowing ? result : Place(mStrokeStart);
}

Point RippleGame::WaterAt(Point world) const noexcept
{
    const auto water = mWater.Sample(world);
    const auto river = RiverCurrent(mLevel, world);
    return {mPhysics.WaterVelocity.X + water.VelocityX + river.X, mPhysics.WaterVelocity.Y + water.VelocityY + river.Y};
}

void RippleGame::CancelInput() noexcept
{
    mStroking = false;
    mInputCount = 0;
    mAccumulator = 0.0;
    mBoat.PreviousPosition = mBoat.Position;
    for (auto& ripple : mRipples)
    {
        ripple.PreviousAge = ripple.Age;
    }
}

void RippleGame::Reset() noexcept
{
    (void)mWater.Reset(mLevel);
    mStroking = false;
    mStrokeFlowing = false;
    mStrokeDistance = 0.0;
    mPhase = GamePhase::Playing;
    mCrash = CrashReason::None;
    mDockTicks = 0;
    mTerminalTransitions = 0;
    mBoat = {};
    for (auto& ripple : mRipples)
    {
        ripple = {};
    }
    mInputCount = 0;
    mCooldown = 0;
    mNextId = 1;
    mTicks = 0;
    mPlacements = 0;
    mContacts = 0;
    mDiscardedTicks = 0;
    mAccumulator = 0.0;
    mLastPlacement = PlacementResult::Ready;
    mBoat.Position = mLevel.Spawn;
    mBoat.PreviousPosition = mLevel.Spawn;
    mBoat.Velocity = mLevel.SpawnVelocity;
    if (Length(mBoat.Velocity) > 0.05)
    {
        mBoat.Heading = std::atan2(mBoat.Velocity.Y, mBoat.Velocity.X);
    }
}

uint32 RippleGame::ActiveRipples() const noexcept
{
    uint32 count = 0;
    for (const auto& ripple : mRipples)
    {
        count += ripple.Active ? 1U : 0U;
    }
    return count;
}

void RippleGame::Advance(float64 delta, bool running, bool gameplay) noexcept
{
    if (!running || (gameplay && mPhase != GamePhase::Playing))
    {
        CancelInput();
        return;
    }
    if (!std::isfinite(delta) || delta <= 0.0)
    {
        return;
    }
    mAccumulator += delta > 0.1 ? 0.1 : delta;
    uint32 steps = 0;
    while (mAccumulator + kEpsilon >= kTickSeconds && steps < 4 && (!gameplay || mPhase == GamePhase::Playing))
    {
        if (gameplay)
        {
            Tick();
        }
        else
        {
            mWater.Advance(kTickSeconds);
            ++mTicks;
        }
        mAccumulator -= kTickSeconds;
        ++steps;
    }
    if (gameplay && mPhase != GamePhase::Playing)
    {
        CancelInput();
        return;
    }
    if (mAccumulator < 0.0)
    {
        mAccumulator = 0.0;
    }
    if (mAccumulator >= kTickSeconds)
    {
        mDiscardedTicks += static_cast<uint64>(mAccumulator / kTickSeconds);
        mAccumulator = std::fmod(mAccumulator, kTickSeconds);
    }
}

void RippleGame::Tick() noexcept
{
    if (mPhase != GamePhase::Playing)
    {
        return;
    }
    if (mCooldown > 0)
    {
        --mCooldown;
        if (mCooldown == 0 &&
            (mLastPlacement == PlacementResult::Placed || mLastPlacement == PlacementResult::Cooldown))
        {
            mLastPlacement = PlacementResult::Ready;
        }
    }
    for (usize i = 0; i < mInputCount; ++i)
    {
        if (mCooldown > 0)
        {
            mLastPlacement = PlacementResult::Cooldown;
            continue;
        }
        Ripple* slot = nullptr;
        for (auto& ripple : mRipples)
        {
            if (!ripple.Active)
            {
                slot = &ripple;
                break;
            }
        }
        if (slot == nullptr || mNextId == std::numeric_limits<uint64>::max())
        {
            mLastPlacement = PlacementResult::Capacity;
            continue;
        }
        if (!mWater.Splash(mInput[i], mPhysics.PushSpeed))
        {
            mLastPlacement = PlacementResult::Capacity;
            continue;
        }
        *slot = {};
        slot->Origin = mInput[i];
        slot->Id = mNextId++;
        slot->Active = true;
        mCooldown = 15;
        ++mPlacements;
        mLastPlacement = PlacementResult::Placed;
    }
    mInputCount = 0;

    mBoat.PreviousPosition = mBoat.Position;
    const Point current = WaterAt(mBoat.Position);
    const Point relativeVelocity{mBoat.Velocity.X - current.X, mBoat.Velocity.Y - current.Y};
    const Point end{mBoat.Position.X + current.X * kTickSeconds + relativeVelocity.X * mDistanceScale,
                    mBoat.Position.Y + current.Y * kTickSeconds + relativeVelocity.Y * mDistanceScale};
    mBoat.Velocity.X = current.X + relativeVelocity.X * mVelocityScale;
    mBoat.Velocity.Y = current.Y + relativeVelocity.Y * mVelocityScale;
    mBoat.ContactFlash = mBoat.ContactFlash > kTickSeconds ? mBoat.ContactFlash - kTickSeconds : 0.0;

    // Cut the boat/ripple interval at the first crash. Later contacts cannot
    // push the hull through a hazard, and docking is evaluated only if alive.
    float64 travelFraction = 1.0;
    CrashReason crash = CrashReason::None;
    const auto recordCrash = [&](float64 time, CrashReason reason) noexcept {
        if (time >= 0.0 && time <= travelFraction)
        {
            travelFraction = time;
            crash = reason;
        }
    };
    if (mLevel.BoundaryHazard)
    {
        struct AxisSweep final
        {
            float64 Start;
            float64 Finish;
            float64 HalfExtent;
        };
        const auto boundary = [&](const AxisSweep& axis) noexcept {
            const float64 start = axis.Start;
            const float64 finish = axis.Finish;
            const float64 limit = axis.HalfExtent - kBoatRadius;
            if (finish >= limit && finish != start)
            {
                recordCrash((limit - start) / (finish - start), CrashReason::Boundary);
            }
            if (finish <= -limit && finish != start)
            {
                recordCrash((-limit - start) / (finish - start), CrashReason::Boundary);
            }
        };
        boundary({mBoat.Position.X, end.X, mLevel.HalfExtent.X});
        boundary({mBoat.Position.Y, end.Y, mLevel.HalfExtent.Y});
    }
    for (usize i = 0; i < mLevel.RockCount; ++i)
    {
        const auto& rock = mLevel.Rocks[i];
        float64 time = 0.0;
        if (CircleContact(mBoat.Position, end, rock.Center, rock.Radius + kBoatRadius, time))
        {
            recordCrash(time, CrashReason::Rock);
        }
    }

    struct Contact final
    {
        float64 Time;
        usize Index;
    };
    Contact contacts[kRippleCapacity]{};
    usize contactCount = 0;
    for (usize i = 0; i < kRippleCapacity; ++i)
    {
        auto& ripple = mRipples[i];
        if (!ripple.Active)
        {
            continue;
        }
        ripple.PreviousAge = ripple.Age;
        const float64 remaining = kRippleLifetime - ripple.Age;
        const float64 interval = kTickSeconds * travelFraction;
        const float64 duration = remaining < interval ? remaining : interval;
        const float64 fraction = duration / kTickSeconds;
        const Point clippedEnd{mBoat.Position.X + (end.X - mBoat.Position.X) * fraction,
                               mBoat.Position.Y + (end.Y - mBoat.Position.Y) * fraction};
        float64 contactTime = 0.0;
        if (!ripple.BoatAffected && RingContact(mBoat.Position,
                                                clippedEnd,
                                                ripple.Origin,
                                                ripple.Age * kRippleSpeed,
                                                (ripple.Age + duration) * kRippleSpeed,
                                                contactTime))
        {
            const Contact value{contactTime * fraction, i};
            usize insert = contactCount;
            while (insert > 0 &&
                   (contacts[insert - 1].Time > value.Time ||
                    (contacts[insert - 1].Time == value.Time && mRipples[contacts[insert - 1].Index].Id > ripple.Id)))
            {
                contacts[insert] = contacts[insert - 1];
                --insert;
            }
            contacts[insert] = value;
            ++contactCount;
        }
        ripple.Age += duration;
    }
    for (usize i = 0; i < contactCount; ++i)
    {
        const auto& hit = contacts[i];
        auto& ripple = mRipples[hit.Index];
        ripple.BoatAffected = true;
        ++mContacts;
        // Legacy front counters remain diagnostic only. The surface solver
        // supplies all momentum and visible waves, including their return flow.
    }
    mBoat.Position = {mBoat.Position.X + (end.X - mBoat.Position.X) * travelFraction,
                      mBoat.Position.Y + (end.Y - mBoat.Position.Y) * travelFraction};
    const float64 speed = Length(mBoat.Velocity);
    if (speed > mPhysics.MaxBoatSpeed)
    {
        mBoat.Velocity.X *= mPhysics.MaxBoatSpeed / speed;
        mBoat.Velocity.Y *= mPhysics.MaxBoatSpeed / speed;
    }
    if (speed > 0.05)
    {
        mBoat.Heading = std::atan2(mBoat.Velocity.Y, mBoat.Velocity.X);
    }
    if (crash != CrashReason::None)
    {
        mCrash = crash;
        mPhase = GamePhase::Crashed;
        mDockTicks = 0;
        mBoat.Velocity = {};
        mBoat.ContactFlash = 0.18;
        ++mTerminalTransitions;
    }
    else if (mLevel.DockRadius > 0.0)
    {
        const bool contained = Length({mBoat.Position.X - mLevel.DockCenter.X,
                                       mBoat.Position.Y - mLevel.DockCenter.Y}) <= mLevel.DockRadius - kBoatRadius;
        if (contained && Length(mBoat.Velocity) <= mLevel.DockSpeed)
        {
            ++mDockTicks;
            if (mDockTicks >= mLevel.DockDwellTicks)
            {
                mPhase = GamePhase::Arrived;
                ++mTerminalTransitions;
            }
        }
        else
        {
            mDockTicks = 0;
        }
    }
    for (auto& ripple : mRipples)
    {
        if (ripple.Age >= kRippleLifetime - kEpsilon)
        {
            ripple.Active = false;
        }
    }
    mWater.Advance(kTickSeconds * travelFraction);
    ++mTicks;
}
} // namespace ludus::sandbox::game
