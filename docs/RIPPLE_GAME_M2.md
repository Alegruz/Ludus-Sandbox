# Drift: danger and arrival

Implemented October 3, 2026. One authored rescue course now provides the complete
M2 loop: guide the broken boat around a rock, arrive slowly inside the green
dock, or crash and Retry. Three-level progression remains M3.

## Play

Build and serve the browser Release payload as described in [BUILD.md](BUILD.md).
Place ripples behind the boat to accelerate, beside it to turn, and ahead to
slow down. The pale circle around the boat shows its physical hull. Avoid the
rock and orange water boundary. Ripple origins on or inside a rock are rejected
without consuming cooldown or clearing valid queued input. The entire hull must stay in the green dock at
no more than 1.5 m/s for 24 ticks (0.4 seconds).

Crash and arrival freeze gameplay and reject placements. Retry resets the
authored spawn, velocity, rings, contact flags, IDs, cooldown, dwell, queued
input, clocks, counters, and interpolation. It preserves physical tuning and
the user's pause choice and reuses existing GPU resources. A graphics Restart
preserves the world, including terminal state. Boat mode still switches to the
existing ocean tuning view.

The course has 60 × 80 m water bounds, spawn (0, −22), one rock at (0, 0) with
radius 5 m, and a dock at (0, 24) with radius 5 m. A route to either side of the
rock works. The circular gameplay hull remains radius 2 m; the boat illustration
inside its visible outline is cosmetic.

## Engine and game ownership

Sandbox continues to consume the installed SDK pinned to Ludus
`9554d051b2125580327d9f89c06395798a53028d`. FoundationMath's checked linear drag
and swept radial-band kernels now also supply production rock contact queries:
a stationary radial band with radius zero and half-width equal to the combined
rock/hull radius is a closed circle. The SDK-free test path retains an independent
stable quadratic reference. No engine source, private headers, fluid solver, or
game-specific engine service is added.

`RippleGame` owns a bounded `LevelDefinition`, up to 16 rocks, the fixed tick,
terminal phase, crash reason, and dock dwell. Loading validates finite values,
bounds, spawn/rock/dock clearance, unique rock IDs, capacities, initial speed,
and dwell limits before replacing the world. Rejection preserves both live
state and queued input. The default pure simulation remains an open-water trial;
`OceanScene` explicitly selects the authored rescue course.

Each tick first predicts the existing drag/current motion, finds the earliest
rock or hull-safe boundary contact, and cuts both boat and ripple sweeps at that
time. Contact uses the straight chord between the drag integrator endpoints;
ripple impulses change velocity for the following tick. Only ripple contacts
within the surviving interval are eligible. Crash
takes priority over docking. Surviving ticks check full dock containment and
final speed; losing either condition resets dwell. Terminal transitions occur
once, even during catch-up. Subsequent ticks, frame debt, and placements freeze.

The single Slang pass draws the same bounds, circular rock radii, dock, and hull
as simulation. The upload grows from 432 to 736 bytes, preserving previous field
offsets. Bounds start at 432, dock at 448, level state at 464, and sixteen rock
records at 480. Individually named Slang rock fields preserve the established
WebGL 2 reflection contract. C++ assertions and packaging enforce the layout.
The subsequent [ocean interaction update](OCEAN_INTERACTIONS.md) appends gesture
effects and boat motion, extending the current block to 1024 bytes.

## Validation

Validation commands and rendered evidence are recorded alongside this document
in `M2_VALIDATION.json`. The following are acceptance categories, not substitutes
for a physical-device playtest:

- Native and browser Development/Release configure/build through selectable
  presets, real SDK linkage, Slang reflection, and SPIR-V validation.
- Ocean, independent gameplay reference, and real SDK gameplay CTests; 455
  gameplay checks cover swept/tangent contact, collision timing, terminal
  freezing, transactional level loading, docking, and 30 crash/Retry cycles.
- ASan/UBSan with leak detection, pinned Clang 18 formatting/static analysis,
  and existing bootstrap/setup/release Python regressions.
- Real browser shader pixels and mouse/emulated-touch placement, crash, rescue,
  Retry, resize, pause, focus, cancellation, and graphics restart on both
  WebGPU and WebGL 2, tested against the extracted Release ZIP.
- Native Vulkan resource creation and 120 rendered frames on Intel UHD 620.

Browser automation uses SwiftShader and throttled RAF. Long navigation runs at
DPR 1; the separate ocean suite retains DPR 2 layout/render coverage. It establishes rendered
behavior, not hardware performance or physical touch support. Native pointer
input is still not connected by the public platform API used by this app;
the browser is the interactive target. Fresh-player steering, physical mobile
acceptance, three-level content, and hosted deployment remain open. Publishing
a web release is a separate action.
