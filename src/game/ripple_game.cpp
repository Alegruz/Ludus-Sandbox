#include "game/ripple_game.h"

#include <cmath>
#include <initializer_list>
#include <limits>

namespace ludus::sandbox::game
{
namespace
{
constexpr float64 kEpsilon = 1e-9;
constexpr float64 kContactBand = kBoatRadius + kRingHalfWidth;
constexpr float64 kDrag = 0.7;
constexpr float64 kPush = 2.5;
constexpr float64 kMaxSpeed = 7.0;

[[nodiscard]] float64 Length(Point p) noexcept
{
    return std::hypot(p.X, p.Y);
}

[[nodiscard]] bool InWater(Point p) noexcept
{
    return std::isfinite(p.X) && std::isfinite(p.Y) && std::abs(p.X) <= 30.0 && std::abs(p.Y) <= 40.0;
}
} // namespace

Camera FitCamera(uint32 width, uint32 height) noexcept
{
    if (width == 0 || height == 0)
    {
        return {};
    }
    const float64 aspect = static_cast<float64>(width) / height;
    const float64 fitHeight = 70.0 / aspect;
    const float64 viewHeight = fitHeight > 90.0 ? fitHeight : 90.0;
    return {viewHeight * aspect, viewHeight};
}

Point ScreenToWorld(Point normalized, Camera camera) noexcept
{
    return {(normalized.X - 0.5) * camera.Width, (0.5 - normalized.Y) * camera.Height};
}

bool RingContact(Point start, Point end, Point origin, float64 ringStart, float64 ringEnd, float64& time) noexcept
{
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
}

PlacementResult RippleGame::Place(Point world) noexcept
{
    if (!InWater(world))
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
    *this = RippleGame{};
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
    if (!running)
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
    while (mAccumulator + kEpsilon >= kTickSeconds && steps < 4)
    {
        Tick();
        mAccumulator -= kTickSeconds;
        ++steps;
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
    const float64 decay = std::exp(-kDrag * kTickSeconds);
    const float64 distanceScale = (1.0 - decay) / kDrag;
    const Point end{mBoat.Position.X + mBoat.Velocity.X * distanceScale,
                    mBoat.Position.Y + mBoat.Velocity.Y * distanceScale};
    mBoat.Velocity.X *= decay;
    mBoat.Velocity.Y *= decay;
    mBoat.ContactFlash = mBoat.ContactFlash > kTickSeconds ? mBoat.ContactFlash - kTickSeconds : 0.0;

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
        const float64 duration = remaining < kTickSeconds ? remaining : kTickSeconds;
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
            const float64 strength = kPush * (1.0 - radius / (kRippleLifetime * kRippleSpeed));
            mBoat.Velocity.X += direction.X / length * strength;
            mBoat.Velocity.Y += direction.Y / length * strength;
            mBoat.ContactFlash = 0.18;
        }
    }
    mBoat.Position = end;
    const float64 speed = Length(mBoat.Velocity);
    if (speed > kMaxSpeed)
    {
        mBoat.Velocity.X *= kMaxSpeed / speed;
        mBoat.Velocity.Y *= kMaxSpeed / speed;
    }
    if (speed > 0.05)
    {
        mBoat.Heading = std::atan2(mBoat.Velocity.Y, mBoat.Velocity.X);
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
