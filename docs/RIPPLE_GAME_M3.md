# Drift: three-course rescue game

> Historical course validation. Reset now seeds ambient waves and boat motion
> comes from the fluid velocity. See [Ocean interactions](OCEAN_INTERACTIONS.md).

Implemented October 3, 2026. The browser game now starts with an open-water
lesson, continues around a rock, and ends in a staggered channel. Every course
uses the existing ripple, swipe-wave and circular-current controls. This is the
M3 gameplay implementation; physical mobile and fresh-player acceptance remain
open.

## Play and progression

Tap behind the boat to push it, beside it to turn, and ahead to oppose its
motion. Swipe for waves or circle for swirls. Keep the entire hull inside the
green dock at no more than 1.5 m/s for 24 fixed ticks (0.4 seconds).

| Course | Spawn | Rocks: center, radius in meters | Dock center |
| --- | --- | --- | --- |
| First Ripples | (0, −22) | None | (8, 10) |
| Around the Rock | (0, −22) | (0, 0), 5 | (0, 24) |
| The Channel | (0, −28) | (−6, −12), 5; (6, 7), 5; (−6, 24), 4 | (10, 29) |

All courses use 60 × 80 m water bounds, a 2 m circular hull, a 5 m dock, and the
same default physics. There are routes on either side of obstacles; the channel
encourages alternating turns. Instructions and the course number change with
the active definition. Ocean tuning stays behind the Boat mode toggle.
Steering instructions hide after crash or arrival so the portrait completion
controls leave the full dock visible.

Next level appears only after arrival. It starts the next course with fresh
water effects and unpauses. Completing the third course displays final feedback
and Play again starts course one. Retry always restores the current course and
preserves the pause choice and physical tuning. A graphics Restart preserves
the course, boat and completion state. No transition reloads the page or
recreates graphics resources.

## Ownership and transitions

`RippleGame` owns the fixed authored catalog, campaign activation and zero-based
course index. The default pure simulation remains a practice world;
`OceanScene` explicitly starts the campaign. Each authored course passes the
existing `LoadLevel` validation before resetting the live game. The flow-field
update reuses its bounded storage rather than copying a temporary game. Invalid
lookups and disallowed Next transitions preserve live state and queued input. A successful custom `LoadLevel` detaches the campaign.

Retry preserves campaign identity while clearing input, stroke ownership,
rings, the velocity/height field, cooldown, counters, docking and interpolation debt.
Next and Play again also restore default physics. The scene resets cosmetic
water clocks and cancels input at transitions. The DOM owns button focus and
pointer capture; course controls never place a ripple.

The existing shader snapshot already represents every course's geometry, so
this milestone changes no shader layout or engine API. Game code remains in
Sandbox and uses the installed public Ludus SDK pinned by
`config/ludus-version.txt`. Browser WebGPU and WebGL 2 use the same simulation.

## Validation and release candidate

Build, package and serve using [BUILD.md](BUILD.md). The archive is
`out/packages/drift-ocean-web-release.zip`; extract it into a fresh directory
and serve that directory over localhost or HTTPS. The package contains compiled
shaders and the browser runtime; playing requires no SDK or shader downloads.

The milestone record in [M3_VALIDATION.json](M3_VALIDATION.json) identifies the
tested package and checks. CI repeats the fresh native/web SDK builds and tests
the extracted Release ZIP. Browser evidence is uploaded with the workflow.

The gameplay browser suite earns all three arrivals using actual mouse or
emulated-touch placements. It checks crash/Retry, guarded progression, terminal
freezing, graphics restart, final completion and replay on both backends.
Portrait and landscape screenshots supplement DOM assertions. The separate
ocean suite covers fallback, failures, DPR, resize and iframe behavior; the
gesture suite covers waves, signed swirls and input cancellation.

SwiftShader, emulated touch and delayed RAF establish behavior, not hardware
performance or physical mobile support. Native rendering remains separate from
interactive native input, which is not connected in this app. Fresh-player
steering, a physical mobile trial, measured frame timing and hosted deployment
remain the M4 acceptance work. Publishing a hosted release requires a separate
request.
