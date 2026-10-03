# Drift ripple game architecture

Status: Game contracts, updated October 3, 2026. M0/M1 is implemented in
`src/game/ripple_game.*` and `game_render.*`, with scene ownership in the existing
ocean controller. Collision, docking and level modules below remain proposed.
See the [implementation record](RIPPLE_GAME_IMPLEMENTATION.md) for actual types,
SDK versions and acceptance evidence.

The game owns its input commands, fixed-step simulation, levels, and render
snapshot. Ludus supplies public platform and graphics services. Implement the
first playable scene by extending the existing ocean source and single Slang
pass. The CPU is authoritative for ripple contact and boat collision.

## Baseline and dependency boundary

Sandbox currently consumes an installed SDK with `find_package(Ludus CONFIG
REQUIRED)`, pins engine revision `70f9debf16a7ec53e33e0f9b87a4106c33896c7a`
in `config/ludus-version.txt`, and owns `src/ocean`, `shaders/ocean.slang`, and
`web/shell.html`. See [BUILD.md](BUILD.md) and [VALIDATION.md](VALIDATION.md).
The implementation record distinguishes local SDK overrides and software-GPU
verification from the fresh bootstrap pin and physical-device acceptance.

The inspected engine public `render.h` provides shaders, one uniform binding,
one surface pipeline, and one fullscreen triangle draw per frame. Uniforms are
16 through 4096 bytes in multiples of 16. There is no public sprite/texture draw
API in this slice. The public fullscreen guide and shader handoff are the engine
contracts; an older installed SDK must not be assumed to expose newer APIs.

| Ludus | Ludus-Sandbox |
| --- | --- |
| Window/canvas lifecycle and reusable pointer event capture | Mapping pointer input to a ripple placement action |
| Shader compilation helper and backend artifact validation | Slang ocean, rings, boat, rock, and dock formulas |
| Resource readiness, upload, draw, resize, device loss, cleanup | Scene state, camera fit, palettes, settings, and render extraction |
| Optional versioned GameHost services once verified | Game state/checkpoint schemas and live property definitions |
| General reusable engine types | Boat motion, ripple force, collision, win/crash rules, and level content |

Do not include engine private headers, expose native/WebGPU handles, or add boat
and ocean vocabulary to engine APIs. New C++ follows the repository's numeric
alias, explicit-error, no-exception and formatting conventions. Preserve the
existing standalone test path; do not replace the game's test shim as a side edit.

## Game modules and ownership

Suggested source additions, adapted to surrounding conventions during implementation:

| Module | Responsibility |
| --- | --- |
| `src/game/game_session.*` | State machine, level selection, clock, input queue, restart, and snapshot publication |
| `src/game/game_simulation.*` | Boat/ripple records, force/contact rules, drag, motion, collision, and docking |
| `src/game/game_level.*` | Bounded level definitions, validation, staging, and stable authored IDs |
| `src/game/game_input.*` | Mouse/touch events to ordered game commands; UI consumption and cancellation |
| `src/game/game_render.*` | Read-only snapshot extraction and padded uniform packing |
| Existing `src/ocean/ocean_scene.*` | Public RHI lifecycle/resource adapter, extended for the game pass |
| Existing `shaders/ocean.slang` | Procedural water and game shape composition |

`GameSession` owns the world and preallocated bounded records. The renderer reads
an owned snapshot and never queries/mutates simulation during submission.
One boat and bounded arrays are sufficient initially. Do not make the jam loop
depend on the proposed generic ECS/component-pool/world module or an allocator
that is not yet shipped. Reuse its ownership principles without duplicating
generic engine infrastructure.

## State and level loading

Gameplay states are Loading, Playing, Paused, Crashed, and Arrived. Graphics
availability is a separate condition. Loading rejects placements; Playing accepts
them; Paused freezes ticks and discards new gameplay placements; Crashed and
Arrived freeze the world until Retry/Next. Graphics loss pauses gameplay and
requires explicit resource restart. Preserve the world for a graphics-only
restart; a failed restoration may offer Retry, but must not silently reset it.

A level definition contains schema/version, level ID, world bounds, boat spawn
and initial velocity, static rocks, dock region, and named gameplay tuning.
Validate finite values, positive dimensions/radii, capacity limits, unique IDs,
spawn clearance, dock clearance, and containment before replacing the active
level. On failed import retain the previous level and report the error. A few
compiled game-owned definitions are adequate before adding file authoring.

Live velocity, ripple ages/contact flags, clocks, and GPU handles never belong
in authored level data. Retry creates a clean world from the definition, clears
pending placements, resets cooldown/ripple IDs, and marks transforms as a
discontinuity for interpolation. It does not recreate the device or recompile
the shader.

## Input and camera mapping

Use world meters with positive X right and positive Y up. Retain the playground's
CPU/GPU mapping convention, adding a level center and fitted view extent. For
level width W, height H, margin M, and aspect A, choose vertical view extent
`max(H + 2*M, (W + 2*M)/A)` and horizontal extent `vertical*A`. Gameplay camera
fit is distinct from the playground's fixed 100-meter vertical view.

Convert pointer coordinates relative to the canvas CSS rectangle, then to the
actual framebuffer/viewport and world coordinates. Use the same committed camera
snapshot as the displayed frame; do not multiply by devicePixelRatio twice.
Reject zero extents and positions outside the playable water. Resize changes the
view, not the world. Keep visual hull radius equal to the documented collision
radius, apart from an explicitly cosmetic outline.

Consume a primary pointer-down edge once. Ignore secondary buttons/touches and
synthetic duplicate mouse input. UI owns events over its controls. Pause,
visibility loss, cancellation, and level replacement clear queued placements.
Use platform events if they preserve required edges/touch identity; otherwise
propose a generic engine input fix or a small Sandbox DOM action bridge. Do not
infer mobile support from mouse emulation or the presence of PointerX/Y fields.

Retain accepted input edges across presentation frames with zero simulation ticks.
An ordered bounded queue assigns a placement to the next eligible tick; catch-up
ticks consume it once. Capacity/cooldown rejection gives feedback and does not
overwrite an earlier command. No pointer state survives a reset accidentally.

## Simulation timing and update order

Use a 60 Hz fixed gameplay step as a proposed baseline. Clamp frame delta to
100 ms, run at most four catch-up ticks per frame, and discard excess debt with
a diagnostic counter. Pause/hidden/resume resets debt and cancels queued input.
Wall time must not expire cooldowns or advance boat/ripple state while paused.
The existing variable-delta ocean clock is a presentation facility, not the
authority for game collision. Advance the shared visual game time from committed
ticks so decorative rings cannot run ahead of physical rings.

Each tick has an explicit order:

1. Consume tick input and accept/reject placements using current gameplay state.
2. Determine boat motion under existing velocity/drag for the tick. Advance ring
   radii and evaluate swept boat/ring contact over that interval.
3. Resolve the boat's earliest swept collision with a rock or boundary. Only
   ripple contacts up to that time are eligible. A crash is terminal for the tick.
4. If still playing, commit the boat position and apply eligible contact impulses
   to its velocity in contact-time then ripple-creation order; clamp final speed.
   These impulses affect displacement beginning with the next fixed tick.
5. Evaluate docking against the committed hull and final speed. Reset dwell on
   an invalid tick; emit Arrived once when its threshold is met.
6. Expire completed ripples after contact evaluation, commit state, and extract
   the render snapshot. Terminal crash takes priority over arrival.

The one-tick force delay is an explicit initial integration choice, bounded to
about 16.7 ms. If testing shows it matters, replace it with integration split at
contact times and update tests/specification together. Do not let draw frequency
change this order or integrate boat motion independently in the shader.

## Ripple and collision contracts

A ripple has a monotonic session ID, world origin, age, propagation speed,
maximum radius, fade envelope, and a BoatAffected flag. Its radius is speed times
age, capped at maximum radius. The starting impulse attenuation is
`max(0, 1 - contactRadius/maximumRadius)` multiplied by the configured push.
At contact, normalize boat position minus origin only when distance exceeds the
epsilon. Apply a zero push at coincident centers and mark the contact consumed.
Expire only after evaluating the last valid lifetime interval.

Detect the first overlap between the boat's swept circular hull and the moving
ring band using previous/current boat positions and ring radii. An endpoint-only
distance test can miss a fast ring passing entirely across the boat in one tick.
Check the whole interval, clip it to the ripple's active lifetime, and record the
contact time/direction. A test must cover contact at creation, tangency, high ring
speed, a moving boat, center coincidence, and re-entry after an earlier hit.

Rock collision uses a swept circle against an expanded circle (rock radius plus
boat radius). Bounds are inset by the hull radius. Use the earliest contact,
place the boat at that contact, zero velocity, and enter Crashed. Stable rock IDs
break equal-time ties. A dock is a trigger with full hull containment and a
speed/dwell condition; it is not a collidable wall. Ripple rings are not blocked
or reflected by rocks in the minimum version.

## Render snapshot and uniform budget

The immutable snapshot contains camera, interpolated boat transform, ripple
origins/radii/fade, rock circles, dock, and gameplay status. Interpolate boat
transforms and ripple radii at the same presentation time; reset previous/current
values on spawn/retry. While paused render the exact committed state.

Compose water, rings, rocks, dock, boat, and minimal effects in one fragment pass,
using bounded loops and analytic shape distances. Combine colors inside that
shader; the current RHI does not provide a second draw or hardware blending here.
The DOM handles text and controls. No texture API or GPU-to-CPU wave readback is
required for this vertical slice.

Budget at most 16 ring records and 16 rock records. A tentative layout of two
16-byte vectors per ring, one per rock, plus camera/boat/dock/settings is below
4096 bytes; it is a sizing proposal, not verified layout. Re-run separate Slang
SPIR-V/WGSL reflection and independent offset/size/alignment checks after adding
arrays. Do not assume the earlier 144-byte ocean contract validates new fields.
Use explicit padding, zero unused slots, bounded counts, and compile-time C++
checks. Reduce capacities or simplify visuals if shader measurements justify it.

## Hot reload and authoring compatibility

Native GameHost integration is an optional later adapter. The versioned host
function table and static shipping linkage should be reused once its rendering
and pointer services meet this game's needs. Browser dynamic Wasm reload is
deferred; the current trial rebuilds and reloads the page.

Keep simulation usable from either the current executable driver or a future
GameHost adapter. Do not invent host service calls or start a second hot reload
implementation. The inspected host work's demo rendering/input services must be
checked against the ocean's uniform and pointer needs before adopting it.

A future checkpoint explicitly encodes level/schema ID, boat pose/velocity,
gameplay tick, cooldown, dock dwell, ripple IDs/ages/contact flags, and visual
current phase needed for continuity. Include tuning revision or its owned values.
Exclude pointers, padding, callbacks, GPU handles, and wall-clock accumulators.
Incompatible schema requires a declared migration or RestartRequired. Rebuild
GPU resources through supported host ownership; never assume old module callbacks
remain valid. Tuning edits apply at tick boundaries and stay distinct from saved
level documents. Browser iteration continues to rebuild and reload the page.

## Verification

Required coverage through M3 includes exactly-once contact/placement, swept contact and obstacle
collision, drag/cooldown/dwell, state transitions, bounded queues, level validation,
reset, camera mapping, and pause/resume. Feed recorded commands at fixed tick IDs
and compare canonical state on the same build across different presentation rates.
Fixed ticks alone do not promise bit-identical floats across platforms. M1 coverage
and remaining M2/M3 tests are distinguished in the implementation record.

Real backend tests verify expanded uniform layouts, shader arrays and control
flow, visual/physical ring alignment, resize, device loss/restart, and packaged
artifacts. Physical mobile tests establish touch usability and WebGPU availability.
Offline CPU previews and mocked GPUs remain useful supplementary evidence, not
proof of real rendering, touch behavior, or performance.
