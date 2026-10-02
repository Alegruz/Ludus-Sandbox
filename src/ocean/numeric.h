#pragma once

// Numeric aliases for the ocean playground.
//
// The Ludus engine mandates fixed-width aliases (uint32, float32, usize, ...)
// from <ludus/foundation/base/types.h> and bans raw std:: primitive spellings
// in engine code (see AGENTS.md). The ocean *settings/logic* layer is pure and
// must also be unit-testable WITHOUT an installed SDK, so this header bridges
// the two worlds:
//
//   * When LUDUS_SANDBOX_WITH_SDK is defined (the real build consuming the SDK),
//     we re-export the engine's aliases, satisfying the engine convention and
//     guaranteeing identical widths.
//   * Otherwise (standalone host unit tests) we define exact <cstdint> aliases
//     with the same names and widths. These are the SAME underlying types the
//     engine aliases wrap, so code is bit-for-bit identical either way.
//
// Keeping the alias names identical means ocean_settings.h / coordinate code is
// written once and compiled unchanged in both configurations.

#if defined(LUDUS_SANDBOX_WITH_SDK)

#    include <ludus/foundation/base/types.h>

namespace ludus::sandbox::ocean::numeric
{
using ludus::foundation::float32;
using ludus::foundation::float64;
using ludus::foundation::int32;
using ludus::foundation::uint32;
using ludus::foundation::uint64;
using ludus::foundation::uint8;
using ludus::foundation::usize;
} // namespace ludus::sandbox::ocean::numeric

#else

#    include <cstddef>
#    include <cstdint>

namespace ludus::sandbox::ocean::numeric
{
using float32 = float;
using float64 = double;
using int32 = std::int32_t;
using uint8 = std::uint8_t;
using uint32 = std::uint32_t;
using uint64 = std::uint64_t;
using usize = std::size_t;
} // namespace ludus::sandbox::ocean::numeric

#endif
