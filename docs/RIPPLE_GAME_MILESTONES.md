# Drift ripple game milestones

Status: M0–M2 implemented, October 3, 2026. Sequence is by working acceptance
gates, not calendar estimates: jam deadline and remaining developer hours have
not been supplied. Do not invent a release date from earlier preparation notes.

The goal is a short browser game in which placed ripples guide a disabled boat
around rocks into a dock. [Game design](RIPPLE_GAME_DESIGN.md) defines behavior;
[architecture](RIPPLE_GAME_ARCHITECTURE.md) defines ownership and timing.

## Baseline and scope

Sandbox `4a07d6c` already contains the ocean shader/playground, controls, presets,
settings JSON, SDK integration, and host logic tests. Its [validation record](VALIDATION.md)
leaves interactive GPU and device acceptance open. This documentation task did
not run the game or independently verify historical CI results.

Keep the existing ocean playground usable as a tuning mode. All game state,
physics, level definitions, and shader composition belong in Sandbox. Generic
engine gaps are handled in Ludus with a narrow reproduced requirement. Full
sprite rendering, ECS, physics middleware, editor tooling, and hot reload are
not prerequisites for this fullscreen procedural slice.

| Gate | Owner | Demonstrable result | Dependency |
| --- | --- | --- | --- |
| M0 Establish a running baseline | Sandbox, with Ludus fixes only if reproduced | Ocean runs through the public SDK on the development target | Existing checkout/toolchain |
| M1 Make one boat drift | Sandbox | Click/tap ring visibly pushes one boat | M0 |
| M2 Add danger and arrival | Sandbox | Steer around one rock, dock, crash, and retry | M1 |
| M3 Make a short game | Sandbox | Three readable levels with complete controls | M2 |
| M4 Validate and package | Sandbox, supported by engine infrastructure | Tested self-contained browser release candidate | M3 |
| Optional Native live editing | Ludus host work plus Sandbox adapter | Verified state-preserving native iteration | Independently ready host services |

M0/M1 code and automated software-GPU acceptance now pass; see the
[implementation record](RIPPLE_GAME_IMPLEMENTATION.md). Physical touch, physical
GPU performance, native interactive input and a fresh-player steering trial remain open.
M2 now supplies the rock/dock/crash/retry loop; see [its implementation record](RIPPLE_GAME_M2.md). M3–M4 gameplay delivery is pending. The packaged M1 build exercises the existing
release path; it does not complete M4's full-game acceptance.

## M0 Establish a running baseline

Inspect the pinned SDK and current build instructions, acquire the supported
host tools, build the existing native/browser targets, and run focused host
tests. Launch the actual ocean in a real WebGPU browser. Record which adapter,
browser, OS, and display environment work. Native Vulkan execution is a separate
acceptance item; retain its build coverage while the browser is the jam target.

Exit evidence:

- Public SDK linkage and genuine shader validation succeed with the pinned tools.
- Ocean animates; pause, settings import/export, resize, and restart are observed.
- Physical browser GPU support and any software-only evidence are labeled separately.
- Device-loss and unavailable-WebGPU handling remain visible.
- Existing packaging/input problems have exact reproducers and repository owners.

Do not redesign the ocean or SDK workflow to bypass a failing gate. A missing
mobile WebGPU adapter is a product/platform constraint, not evidence to quietly
replace the renderer. Select supported devices explicitly before promising mobile.

## M1 Make one boat drift

Add the game session/fixed ticks, one boat, drag, bounded ripple records, and
mouse/touch placement. Extend the single game shader to draw the boat and rings
using the same state as physics. Include fitted camera mapping and a minimal
cooldown/readiness indicator. Initially omit hazards and background force.

Exit evidence:

- A ripple gives a single outward impulse at ring contact, never every frame.
- Behind/beside/ahead placement can accelerate, turn, and oppose motion.
- CPU/render ring positions agree; zero-distance contact is finite and defined.
- Swept tests catch a ring crossing within one tick and a moving boat.
- Inputs survive zero-tick frames and fire once during catch-up; UI never emits rings.
- Mouse and a primary touch each create one ring on a physical device, including cancellation tests.
- Expanded ripple uniforms pass per-target layout validation and real pipeline creation.
- A new player can explain the force direction after a short open-water trial.

Save one reproducible tuning preset and observations. Tune propagation, push,
drag, and cooldown before adding levels. If steering remains confusing, improve
the visible contact response before adding more water detail.

## M2 Add danger and arrival

Introduce a single authored level with one rock, visible bounds, boat spawn, and
dock. Implement swept collision, crash, low-speed dock dwell, Retry, and the
terminal-state priority defined in the architecture.

Exit evidence:

- Player maneuvers around the rock and arrives intentionally.
- Fast boat motion cannot tunnel through rocks or the playable boundary.
- Collision silhouette and physical radius visibly agree.
- Docking requires full containment and sustained acceptable speed.
- Retry restores boat, ripples, cooldown, clocks, dwell, and queued input exactly.
- Crash/arrival fires once; paused or terminal scenes reject placements.
- Repeated crash/retry does not recreate GPU resources or grow memory.

This is the first complete gameplay loop and the minimum fallback if jam time
becomes short. A polished single level is preferable to three broken levels.

## M3 Make a short game

Author the three levels in the design: open-water learning, one-rock steering,
and a short staggered route. Add goal instruction, Pause/Resume, Retry, Next,
and end-of-game feedback. Hide tuning controls by default but retain an explicit
development/tuning mode. Validate level definitions before activation.

Exit evidence:

- Levels progress and retry without page reload or invalid saved state.
- All obstacles and the dock stay visible at portrait and landscape sizes.
- Mouse and physical touch players can finish the route without precise hover input.
- UI controls are legible, focusable, and do not obscure critical play space.
- Invalid definitions/settings retain the existing working scene and explain the error.
- A short playtest records confusion, crashes, arrival success, and tuning adjustments.

Add sound or a cosmetic wake only after these gates pass and time remains.
Do not add moving enemies, procedural levels, inventory, or a second mechanic.

## M4 Validate and package

Build a self-contained web archive using the established packaging path. Test
clean extraction and a localhost/HTTPS deployment; if an itch.io release is
chosen, test its actual iframe/fullscreen behavior before claiming support.
Publishing itself is a separate user-authorized action, not implied by this plan.

Exit evidence:

- Appropriate warning-clean, unit, pinned format/tidy, and native ASan/UBSan checks pass.
- Native and browser builds generate/ship the correct shader artifacts; no runtime
  compiler, SDK install, tool download, or network shader fetch is required.
- Real GPU screenshots show boat, ring contact, hazards, dock, and terminal states.
- Resize/DPR, blur, pause, hidden/resume, input cancellation, repeated retry, and
  graphics restart pass on recorded device/browser combinations.
- Measure CPU simulation time and frame timing at the tested viewport. Target smooth
  60 fps at 1080p on the development device; record actual mobile resolution/results.
- Confirm touch on at least one physical mobile WebGPU device before labeling mobile
  supported. Unsupported devices show a clear availability message.
- A validation note records exact commands/build revisions, adapters, performance,
  remaining limitations, and the package location. Unavailable/skipped checks remain open.

When rendering is too slow, reduce costly water noise or ripple visual detail
first. Keep simulation and visible hazards intact; never silently reduce physics
accuracy based on frame rate.

## Optional Native live editing

Coordinate with the existing GameHost/editor work instead of implementing a
second module loader. Verify public host input/render services can represent the
game before writing an adapter. Preserve one simulation implementation across
dynamic native play and static shipping; browser iteration rebuilds/reloads.

Acceptance requires explicit checkpoint state, supported migration/restart
behavior, frame/tick-boundary tuning, correct resource ownership, state-preserving
success, unchanged old state on a rejected reload, and real debugger/render tests.
Missing host services do not block M1 through M4 on the existing application path.

## Handoff for implementation

Implement M0 and M1 first, preserving the existing ocean tuning mode and following
the two companion documents. Inspect both repository standards and actual SDK
APIs before editing. Do not claim a gate from compilation alone. Report changed
files, commands/outcomes, a runnable entry point, and concrete remaining gaps at
each gate. Promote only reproduced reusable infrastructure needs into Ludus.
