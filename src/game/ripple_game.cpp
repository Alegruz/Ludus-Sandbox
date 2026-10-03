#include "game/ripple_game.h"

#if defined(LUDUS_SANDBOX_WITH_SDK)
#    include <ludus/foundation/math/dynamics.hpp>
#endif

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
    return {viewHeight * aspect, viewHeight};
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
        level.DockDwellTicks == 0 || level.DockDwellTicks > 600)
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
    Reset();
    return true;
}

float64 RippleGame::DockProgress() const noexcept
{
    return static_cast<float64>(mDockTicks) / mLevel.DockDwellTicks;
}

Point ScreenToWorld(Point normalized, Camera camera) noexcept
{
    return {(normalized.X - 0.5) * camera.Width, (0.5 - normalized.Y) * camera.Height};
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

void RippleGame::CancelInput() noexcept
{
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
    const auto settings = mPhysics;
    const auto level = mLevel;
    *this = RippleGame{};
    mLevel = level;
    (void)SetPhysics(settings);
    mBoat.Position = level.Spawn;
    mBoat.PreviousPosition = level.Spawn;
    mBoat.Velocity = level.SpawnVelocity;
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

void RippleGame::Advance(float64 delta, bool running) noexcept
{
    if (!running || mPhase != GamePhase::Playing)
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
    while (mAccumulator + kEpsilon >= kTickSeconds && steps < 4 && mPhase == GamePhase::Playing)
    {
        Tick();
        mAccumulator -= kTickSeconds;
        ++steps;
    }
    if (mPhase != GamePhase::Playing)
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
    const Point current = mPhysics.WaterVelocity;
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
        const Point atHit{mBoat.Position.X + (end.X - mBoat.Position.X) * hit.Time,
                          mBoat.Position.Y + (end.Y - mBoat.Position.Y) * hit.Time};
        const Point direction{atHit.X - ripple.Origin.X, atHit.Y - ripple.Origin.Y};
        const float64 length = Length(direction);
        if (length > kEpsilon)
        {
            const float64 radius = (ripple.PreviousAge + hit.Time * kTickSeconds) * kRippleSpeed;
            const float64 strength = mPhysics.PushSpeed * (1.0 - radius / (kRippleLifetime * kRippleSpeed));
            mBoat.Velocity.X += direction.X / length * strength;
            mBoat.Velocity.Y += direction.Y / length * strength;
            mBoat.ContactFlash = 0.18;
        }
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
    ++mTicks;
}
} // namespace ludus::sandbox::game
