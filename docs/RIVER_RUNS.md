# Drift: vertical river runs

The jam course now flows from bottom to top. The current keeps bringing hazards
closer while the player steers a disabled boat by moving the water. A following
camera shows upcoming obstacles; it gives the boat room below center and clamps
near the start and rescue dock. Each rock gate changes the open lane, followed by
space to recover. There is no forced camera scroll or hidden countdown.

| Course | Water bounds | Mean current | Hazards | Challenge |
| --- | --- | --- | --- | --- |
| River Mouth | 44 × 144 m | 1.8 m/s | 3 rocks | Learn to correct the drift before the next gate |
| Rock Gates | 40 × 176 m | 2.25 m/s | 5 rocks | Cross the current and choose the opening between rocks |
| The Rapids | 36 × 208 m | 2.7 m/s | 7 rocks | Tighter lanes, faster stretches, and less time to react |

The boat starts 12 m from the bottom; each dock is 13 m from the top. Flow is
slower at the banks and increases through two authored 24 m rapid stretches
in later courses. The dock approach fades the mean current to zero, giving the
player a place to brake and earn the existing full-hull, slow-speed rescue.
Wind waves and player-made currents still disturb this calm approach.

## Game and engine boundary

All course data, mean transport, boat rules, camera and graphics remain in
Ludus-Sandbox. `WaterField` still consumes the installed engine PhysicsFluid
SDK for gesture momentum, finite splashes, gravity waves, reflections and foam.
`RiverCurrent` adds an authored mean velocity to the boat's water sample. This
is prescribed transport: the closed-basin fluid solver does not solve an open
river's inflow/outflow or advect disturbances along the mean flow. A future
engine open-boundary solver can replace that model separately.

The CPU and Slang use the same mean speed, lane profile, rapid envelopes and dock
fade. Broken foam trails and upward marks show the flow. Rocks and bank outlines
show the exact circular and rectangular collision boundaries.

Rendering and input use the same interpolated camera. Zero-current practice
levels keep their whole-level camera. Ocean tuning shows the complete basin and
hides mean river transport; returning to Boat retains course progress. A stroke
captures the presented camera at pointer down so following the boat cannot
inject momentum by itself. Pause, blur, help, Retry, Next, Replay and graphics
restart retain their existing input/lifecycle rules. Retry keeps the current
river and resets the camera with the boat. The uniform block stays 16,128 bytes;
reserved scalar and bounds lanes carry camera center and authored river data.

## Acceptance

Verify spontaneous upward drift, faster rapids, clear docks, invalid authored
flow rejection, camera/resize mapping, all three rescue routes with public input,
crash/Retry, progression, terminal freeze and Replay. Browser QA uses real mouse
and emulated touch on both shader backends, with screenshots of the scrolling
river and arrival. Physical mobile trials, first-player difficulty tuning and
measured hardware frame rates remain release acceptance work.

## Validation record

Native Development and browser Development/Release builds pass with the pinned
installed SDK and shader tools. The 5,483-check gameplay suite passes, including
all three authored routes, Retry and campaign progression; ASan/UBSan with leak
detection also passes. Pinned Clang 18 formatting and native/web static analysis
pass, as do 25 bootstrap/setup regressions and actual preset/SDK discovery.

Browser coverage includes full WebGPU mouse progression, both-backend mouse and
emulated-touch drag/swirl checks, mobile HUD layouts, and finite-wave overlap.
Full campaign/recovery checks continue in CI. These use software rendering and
are separate from physical-device or fresh-player acceptance.
