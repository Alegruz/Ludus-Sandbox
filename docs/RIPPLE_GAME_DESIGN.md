# Drift ripple boat game design

Status: Game design, updated October 3, 2026. The ocean, boat, interactive
ripples, swept hazards, docking, and Retry are implemented for one authored course.
Three-level progression remains planned;
see the [implementation record](RIPPLE_GAME_IMPLEMENTATION.md) for current behavior.

The player rescues a boat with a broken motor by placing ripples on the water.
Each expanding ring pushes the boat away from its origin when it reaches the
hull. The player must guide the drifting boat around rocks and into a safe dock.
The same placement action works with a mouse click or a touch on mobile web.

This document defines the first playable game in Ludus-Sandbox. Implementation
ownership and timing are in [the architecture](RIPPLE_GAME_ARCHITECTURE.md);
delivery and acceptance are in [the milestones](RIPPLE_GAME_MILESTONES.md).
Existing ocean visuals and tuning remain documented in [DESIGN.md](DESIGN.md).

## Player experience

The scene is a small, readable body of water seen directly from above. The boat
has no working motor and receives no direct steering input. A bright ring appears
where the player clicks or taps, travels across the water, and nudges the boat.
The boat retains momentum and gradually slows through water drag. The challenge
is anticipating a push rather than repeatedly correcting a directly controlled
character.

Placing a ripple behind the boat accelerates it, placing one beside it changes
its course, and placing one ahead can slow it. The third action is not an
automatic brake: it adds an opposing push and can reverse the boat if overused.
Teach these relationships through an open starting area before adding obstacles.

## First playable rules

| Element | Proposed rule |
| --- | --- |
| Boat | One boat with a circular gameplay hull, position, velocity, and visual heading. No engine thrust or direct rotation input. |
| Placement | One primary click/tap places one ripple at a valid water position on release. A straight drag generates a directional wave, and a circular drag generates a rotating current; see [Ocean interactions](OCEAN_INTERACTIONS.md). |
| Ripple | Expands at a constant speed, fades over a bounded lifetime, and pushes radially outward on its first contact with the boat. |
| Repeated contact | Each ripple can affect the boat once. Remaining inside the ring or re-entering it grants no further impulse. |
| Simultaneous ripples | A small bounded pool permits overlapping rings. A cooldown limits rapid placement. |
| Rocks | Stationary circular hazards. The boat crashes on contact. Ripples pass through rocks in the first version. |
| Water boundary | The playable region is visibly marked. Crossing the hull-safe boundary crashes the boat; scenery outside it is decorative. |
| Dock | A visible safe zone. Win when the entire gameplay hull is inside it and speed stays below the docking limit for a short dwell. |
| Crash | Brief feedback and a clearly available Retry action. Retry restores the authored initial state without reloading the page or device. |
| Success | Freeze gameplay, show arrival feedback, then allow Next level or Retry. |

The cooldown begins only when placement is accepted. Reject placements over UI,
outside water, inside rocks, while paused/loading/finished, or when the ripple
pool is full. Show brief feedback for rejected placement and retain the cooldown.
Spawn/contact at exactly the ripple origin never produces an undefined direction:
accept the ring but apply no push while distance is below a small epsilon.

## Starting tuning values

These are playtest hypotheses, not measurements or locked requirements. Keep them
in Sandbox tuning data and change them together when scale changes.

| Parameter | Starting value |
| --- | --- |
| Playable rectangle | 60 by 80 meters, with a view margin |
| Boat collision radius | 2.0 meters (enlarged during M1 for screen readability) |
| Ripple propagation | 20 meters per second |
| Maximum ring radius | 30 meters; lifetime 1.5 seconds |
| Ring visual/contact half width | 0.35 meters |
| Push | 2.5 meters per second velocity change at close range, tapering smoothly to zero at maximum radius |
| Water drag | Exponential velocity decay with rate 0.7 per second |
| Maximum boat speed | 7 meters per second |
| Placement cooldown | 0.25 seconds |
| Active ripple capacity | 16 |
| Rock capacity per level | 16 |
| Dock | Radius 5 meters; arrival speed at most 1.5 meters per second for 0.4 seconds |

Start with zero physical background current. The playground's moving ocean
patterns are visual and do not automatically apply a force. Add a distinct
gameplay current only after ripple steering feels understandable; show its
direction clearly and keep the physics parameter separate from visual tuning.

## Camera and controls

Use a fixed overhead camera that fits the entire level with a margin. On portrait
and landscape screens, preserve world proportions and reveal extra water as
needed; resizing must never change collision positions or hide the dock. This
game camera extends the playground's existing fixed vertical extent mapping.

The first version accepts a mouse primary button or the primary touch. Ignore
additional simultaneous touches. Browser cancellation, blur, hidden tabs, and
UI focus must not produce a ripple or leave a held input. A tap must generate
one placement, without a second synthetic mouse action. Prevent scrolling on the
play canvas while preserving normal interaction with controls outside it.

Keep the HUD small: goal instruction, ripple readiness, Pause, Retry, and level
progress. Collapse the ocean tuning panel during ordinary play. Offer an optional
placement preview/aim marker for clarity without changing physics. Do not require
hover to understand the game on a touch device.

## Visual and sound direction

Retain the existing navy, blue, and teal ocean palette. Draw rings in pale foam
with a readable leading edge and softer trail. Render the boat, rocks, and dock
as simple procedural shapes in the same fullscreen shader initially. Give their
silhouettes enough contrast to remain readable over both calm and choppy water.
Boat heading follows meaningful movement and retains its last heading at rest;
it does not determine the direction of the ripple push.

The displayed ring position, radius, and fade come from gameplay state. Ocean
waves cannot secretly push the boat, and a decorative wake cannot define a hull.
Use a small boat response and a flash at ring contact so the force is visible.
Wake, bobbing, and tilt are cosmetic polish after the steering loop works.

Short placement, contact, crash, and success sounds are optional polish using
existing engine audio facilities when verified. The game must remain readable
with sound muted. Audio is outside the minimum playable gate.

## Level progression

Build three small authored levels rather than a procedural generator:

1. Open water with an offset dock, teaching a push and an opposing push.
2. One rock between the boat and dock, teaching lateral placement and anticipation.
3. A short staggered route with two or three rocks, combining steering and speed control.

Levels specify stable identifiers, bounds, boat spawn/velocity, rocks, dock, and
a tuning preset. Obstacles never spawn randomly during a run. Retry uses the same
initial conditions so the player can learn from a failed placement.

## Scope and questions for playtesting

The minimum release has one boat, one placement action, three short levels,
arrival/crash states, and a browser package. Enemies, upgrades, moving obstacles,
multiplayer, realistic water simulation, wave reflection, and multiple boats
are deferred. Native hot reload and editor authoring are useful optional tools,
not requirements for a playable browser game.

Test these questions before increasing content scope:

- Can a new player explain why the boat moved after the first few ripples?
- Is the travel delay long enough for anticipation but short enough for control?
- Can the player intentionally slow down and turn around one rock?
- Are taps accurate and all hazards readable on a physical mobile device?
- Does a failure feel attributable to placement rather than an invisible force,
  stretched coordinate system, or unclear collision edge?

Record device, build, tuning values, and observations. Adjust ripple speed,
impulse, drag, and level spacing before adding more mechanics.
