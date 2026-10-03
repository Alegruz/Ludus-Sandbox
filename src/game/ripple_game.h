#pragma once

#include "ocean/numeric.h"

namespace ludus::sandbox::game
{
using ocean::numeric::float32;
using ocean::numeric::float64;
using ocean::numeric::uint32;
using ocean::numeric::uint64;
using ocean::numeric::uint8;
using ocean::numeric::usize;

inline constexpr usize kRippleCapacity = 16;
inline constexpr usize kInputCapacity = 8;
inline constexpr float64 kTickSeconds = 1.0 / 60.0;
inline constexpr float64 kRippleSpeed = 20.0;
inline constexpr float64 kRippleLifetime = 1.5;
inline constexpr float64 kBoatRadius = 2.0;
inline constexpr float64 kRingHalfWidth = 0.35;

struct Point final
{
    float64 X = 0.0;
    float64 Y = 0.0;
};
// Physical tuning is independent of cosmetic ocean animation. Changes take
// effect on the next tick; validation is transactional. Velocities are m/s.
struct PhysicsSettings final
{
    Point WaterVelocity;
    float64 DragRate = 0.7;     // Per second.
    float64 PushSpeed = 2.5;    // Delta velocity at the ripple center.
    float64 MaxBoatSpeed = 7.0; // World-relative gameplay limit.
};

struct Camera final
{
    float64 Width = 70.0;
    float64 Height = 90.0;
};
[[nodiscard]] Camera FitCamera(uint32 width, uint32 height) noexcept;
[[nodiscard]] Point ScreenToWorld(Point normalized, Camera camera) noexcept;

struct Boat final
{
    Point Position;
    Point PreviousPosition;
    Point Velocity;
    float64 Heading = 0.0;
    float64 ContactFlash = 0.0;
};
struct Ripple final
{
    Point Origin;
    float64 Age = 0.0;
    float64 PreviousAge = 0.0;
    uint64 Id = 0;
    bool Active = false;
    bool BoatAffected = false;
};
enum class PlacementResult : uint8
{
    Ready,
    Queued,
    Placed,
    Cooldown,
    Capacity,
    Outside,
    Inactive
};

// Pure fixed-tick state. No GPU resources or frame callbacks are retained.
class RippleGame final
{
public:
    RippleGame() noexcept;
    [[nodiscard]] bool SetPhysics(const PhysicsSettings& settings) noexcept;
    [[nodiscard]] const PhysicsSettings& GetPhysics() const noexcept
    {
        return mPhysics;
    }

    [[nodiscard]] PlacementResult Place(Point world) noexcept;
    void Advance(float64 delta, bool running) noexcept;
    void CancelInput() noexcept;
    void Reset() noexcept;
    void Tick() noexcept;

    [[nodiscard]] const Boat& GetBoat() const noexcept
    {
        return mBoat;
    }
    [[nodiscard]] const Ripple* GetRipples() const noexcept
    {
        return mRipples;
    }
    [[nodiscard]] uint32 ActiveRipples() const noexcept;
    [[nodiscard]] uint64 Ticks() const noexcept
    {
        return mTicks;
    }
    [[nodiscard]] uint64 Placements() const noexcept
    {
        return mPlacements;
    }
    [[nodiscard]] uint64 DiscardedTicks() const noexcept
    {
        return mDiscardedTicks;
    }
    [[nodiscard]] uint64 Contacts() const noexcept
    {
        return mContacts;
    }
    [[nodiscard]] float64 Alpha() const noexcept
    {
        return mAccumulator / kTickSeconds;
    }
    [[nodiscard]] float64 CooldownFraction() const noexcept
    {
        return static_cast<float64>(mCooldown) / 15.0;
    }
    [[nodiscard]] PlacementResult LastPlacement() const noexcept
    {
        return mLastPlacement;
    }

private:
    PhysicsSettings mPhysics;
    float64 mVelocityScale = 1.0;
    float64 mDistanceScale = kTickSeconds;
    Boat mBoat;
    Ripple mRipples[kRippleCapacity];
    Point mInput[kInputCapacity];
    usize mInputCount = 0;
    uint32 mCooldown = 0;
    uint64 mNextId = 1;
    uint64 mTicks = 0;
    uint64 mPlacements = 0;
    uint64 mContacts = 0;
    uint64 mDiscardedTicks = 0;
    float64 mAccumulator = 0.0;
    PlacementResult mLastPlacement = PlacementResult::Ready;
};

// Earliest contact of a moving hull and expanding ring, normalized to [0,1].
// ringStart/ringEnd and boat trajectory cover the SAME time interval.
[[nodiscard]] bool
RingContact(Point start, Point end, Point origin, float64 ringStart, float64 ringEnd, float64& time) noexcept;
} // namespace ludus::sandbox::game
