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
inline constexpr usize kSurfaceCapacity = 8;
inline constexpr usize kStrokeCapacity = 64;
inline constexpr float64 kWaveSpeed = 12.0;
inline constexpr float64 kWaveLifetime = 3.0;
inline constexpr float64 kVortexLifetime = 5.0;
inline constexpr usize kRockCapacity = 16;
inline constexpr uint32 kCourseCount = 3;
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
struct Rock final
{
    Point Center;
    float64 Radius = 1.0;
    uint32 Id = 0;
};
// Bounded authored values; no live state or GPU resources. Radius zero disables
// the dock for the open-water practice simulation used by the logic tests.
struct LevelDefinition final
{
    uint32 Id = 1;
    Point HalfExtent{30.0, 40.0};
    Point Spawn;
    Point SpawnVelocity;
    Rock Rocks[kRockCapacity];
    usize RockCount = 0;
    Point DockCenter;
    float64 DockRadius = 0.0;
    float64 DockSpeed = 1.5;
    uint32 DockDwellTicks = 24;
    bool BoundaryHazard = false;
};
[[nodiscard]] LevelDefinition RescueLevel() noexcept;
// Zero-based authored campaign catalog. Invalid lookups leave output unchanged.
[[nodiscard]] bool TryGetCourseLevel(uint32 index, LevelDefinition& output) noexcept;
[[nodiscard]] const char* CourseTitle(uint32 index) noexcept;
[[nodiscard]] const char* CourseInstruction(uint32 index) noexcept;
enum class GamePhase : uint8
{
    Playing,
    Crashed,
    Arrived
};
enum class CrashReason : uint8
{
    None,
    Rock,
    Boundary
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
[[nodiscard]] Camera FitCamera(uint32 width, uint32 height, Point halfExtent = {30.0, 40.0}) noexcept;
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
enum class SurfaceKind : uint8
{
    Wave,
    Vortex
};
struct SurfaceEffect final
{
    Point Origin;
    Point Direction{1.0, 0.0};
    float64 Radius = 8.0;
    float64 Strength = 1.0; // Signed for clockwise/counterclockwise vortices.
    float64 Age = 0.0;
    float64 PreviousAge = 0.0;
    SurfaceKind Kind = SurfaceKind::Wave;
    float64 DecayAge = 0.0; // Vortex energy is refreshed while stirring; phase stays continuous.
    bool Active = false;
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
    [[nodiscard]] bool LoadLevel(const LevelDefinition& level) noexcept;
    [[nodiscard]] bool StartCampaign() noexcept;
    [[nodiscard]] bool NextCourse() noexcept;
    [[nodiscard]] bool CampaignActive() const noexcept
    {
        return mCampaignActive;
    }
    [[nodiscard]] uint32 CourseIndex() const noexcept
    {
        return mCourseIndex;
    }
    [[nodiscard]] bool CampaignComplete() const noexcept
    {
        return mCampaignActive && mCourseIndex == kCourseCount - 1 && mPhase == GamePhase::Arrived;
    }
    [[nodiscard]] const LevelDefinition& GetLevel() const noexcept
    {
        return mLevel;
    }
    [[nodiscard]] GamePhase Phase() const noexcept
    {
        return mPhase;
    }
    [[nodiscard]] CrashReason Crash() const noexcept
    {
        return mCrash;
    }
    [[nodiscard]] float64 DockProgress() const noexcept;
    [[nodiscard]] uint32 TerminalTransitions() const noexcept
    {
        return mTerminalTransitions;
    }
    [[nodiscard]] bool SetPhysics(const PhysicsSettings& settings) noexcept;
    [[nodiscard]] const PhysicsSettings& GetPhysics() const noexcept
    {
        return mPhysics;
    }

    [[nodiscard]] PlacementResult Place(Point world) noexcept;
    [[nodiscard]] PlacementResult BeginStroke(Point world) noexcept;
    [[nodiscard]] PlacementResult MoveStroke(Point world) noexcept;
    [[nodiscard]] PlacementResult EndStroke(Point world) noexcept;
    // Local transport from the same bounded effects sent to the shader.
    [[nodiscard]] Point WaterAt(Point world) const noexcept;
    [[nodiscard]] const SurfaceEffect* GetSurfaceEffects() const noexcept
    {
        return mSurface;
    }
    [[nodiscard]] uint32 ActiveSurfaceEffects(SurfaceKind kind) const noexcept;
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
    [[nodiscard]] bool LoadCourse(uint32 index) noexcept;
    [[nodiscard]] SurfaceEffect* StrokeEffect() noexcept;
    SurfaceEffect mSurface[kSurfaceCapacity];
    Point mStroke[kStrokeCapacity];
    usize mStrokeCount = 0;
    usize mStrokeSlot = kSurfaceCapacity;
    Point mStrokeDirection;
    float64 mStrokeDistance = 0.0;
    float64 mStrokeTurn = 0.0;
    float64 mStrokeAbsoluteTurn = 0.0;
    bool mStroking = false;
    LevelDefinition mLevel;
    bool mCampaignActive = false;
    uint32 mCourseIndex = 0;
    GamePhase mPhase = GamePhase::Playing;
    CrashReason mCrash = CrashReason::None;
    uint32 mDockTicks = 0;
    uint32 mTerminalTransitions = 0;
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
// Closed circle swept by a point; radius already includes the boat hull.
[[nodiscard]] bool CircleContact(Point start, Point end, Point origin, float64 radius, float64& time) noexcept;
} // namespace ludus::sandbox::game
