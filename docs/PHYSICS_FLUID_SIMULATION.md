# Drift physics and fluid simulation

The game uses a bounded CPU simulation for steering and analytic procedural
water for appearance. Ludus supplies checked drag coefficients and swept radial
contact through `Ludus::FoundationMath`. Sandbox owns gameplay state, current,
impulses and cooldown. This is the first implementation of the
[engine simulation design](https://github.com/Alegruz/Ludus/blob/codex/physics-fluid-kernels/docs/architecture/physics-fluid-simulation.md)
and its
[reference review](https://github.com/Alegruz/Ludus/blob/codex/physics-fluid-kernels/docs/architecture/physics-fluid-reference-review.md).

## Physical settings

`PhysicsSettings` in `src/game/ripple_game.h` is the single editable physical
configuration. The ocean panel remains cosmetic. Defaults retain the original
steering model: zero water velocity, drag 0.7/s, push 2.5 m/s, cap 7 m/s.

```cpp
ludus::sandbox::game::PhysicsSettings physics;
physics.WaterVelocity = {2.0, -1.0}; // World meters per second.
physics.DragRate = 0.7;
if (!game.SetPhysics(physics))
{
    // Keep the previous settings and show an authoring error.
}
```

SetPhysics validates all fields and computes cached drag coefficients before
installing anything. Drag and push accept [0,100], the speed cap accepts (0,100],
and water velocity must be finite with magnitude at most that cap. Zero drag
means water does not couple to the boat. The cap is world-relative for gameplay;
it does not constrain the relative water speed independently. The setting change
affects the next tick without moving the boat. Reset retains physical settings
while clearing boat/ripple/input/counter state. Physical settings currently have
no DOM controls or JSON schema; code and tests are the authoring surface.

## Update and fluid model

At 60 Hz, the exact constant-current solution is
`v1=u+(v0-u)*exp(-k*dt)` and
`x1=x0+u*dt+(v0-u)*(-expm1(-k*dt)/k)`; at zero k, motion is ballistic.
The expensive coefficients are computed once when tuning changes.

Ring/hull contact uses the earliest overlap of the hull sweep and both radial
band boundaries, clipped to ripple lifetime. The SDK kernel uses normalized
geometry and stable quadratic roots. Creation IDs order equal-time contacts;
each ring pushes once. Displacement responds on the next tick, preserving the
existing steering latency. The sweep is a linear chord through exact drag
endpoints, not an exact nonlinear time-of-impact calculation. Its default
position/timing error bound is described in the engine design.

A physical current is independent of moving color/wave patterns. Neither wakes,
foam nor ring shading feeds back into simulation. This supplies the game's
steering mechanics, not a Navier Stokes or shallow-water solver. Interference,
reflection, buoyancy and depth transport are future features with explicit
admission/validation requirements in the engine design. Existing planned rock,
boundary and dock work remains in the game milestones.

## Tests and debugging

The existing SDK-free tests retain an independent bounded-world contact reference
for fast CI. Production and `ludus_sandbox_ripple_sdk_tests` use the installed
FoundationMath implementation. Both run the same game-level regressions, including
current/drag closed-form motion, tiny/zero drag, invalid-setting transactionality,
pause/reset, contact consumption, interpolation and frame-rate invariance.
Engine tests separately exercise scaling, stable roots, tangency, shrinking bands,
invalid/range errors, output preservation and zero allocation.

Inspect named boat/ripple records and GetPhysics() in the debugger. Tick, contact,
placement and discarded-debt counters explain state changes. Replay must record
ordered placements and physical setting changes at tick boundaries; cross-target
bitwise reproducibility is not promised. The uniform layout and public fullscreen
rendering contract are unchanged.

Build/bootstrap instructions are in [BUILD.md](BUILD.md). The pinned engine commit
now includes the dynamics kernels; a local SDK override must contain
`ludus/foundation/math/dynamics.hpp` and its matching library. Use the generated
selectable native/browser presets and run all three native CTest targets.

## Validation record

Validated October 3, 2026, with Ludus engine commit
`9554d051b2125580327d9f89c06395798a53028d` from
[Ludus PR 64](https://github.com/Alegruz/Ludus/pull/64). This is the bootstrap
pin in config/ludus-version.txt; SDK overrides must provide the same kernels.

- Native app/shaders and browser Release app/shaders build warning-clean.
- All three native CTest targets pass. Both reference and SDK ripple tests pass
  89 checks. The production SDK path also passes ASan/UBSan with sanitized engine
  libraries; the standalone reference passes sanitizers separately.
- Clang-format 18 and clang-tidy 18 pass on changed production code, including
  the real SDK compile path. All 18 setup/release Python regressions pass.
- The existing repair command generated selectable native/browser configure and
  build presets and the native test preset, verified with the selected CMake.
  Native and web SDKs, compiler, Ninja, shader tools and editor preset mode pass
  the setup checks. Machine paths remain in ignored CMakeUserPresets.json.
- The exact Release ZIP passes the four rendered Chromium 140.0.7339.186 cases:
  WebGPU/WebGL 2 with mouse and emulated touch, including push, cancellation,
  UI ownership, pause/reset and restart. No browser errors were captured.
  The [browser report](physics-fluid-browser-validation.json) records the cases.
  Software GPU/emulated touch do not certify physical mobile hardware, native
  GPU rendering or hardware frame timing.

The engine's numerical/reference review records the larger kernel suite,
public-header gates, allocation probe and SDK consumer verification. This integration builds on
[Sandbox PR 4](https://github.com/Alegruz/Ludus-Sandbox/pull/4), which supplies the
first-playable boat/ripple baseline. The numerical/tuning changes are a separate
commit above that baseline.
