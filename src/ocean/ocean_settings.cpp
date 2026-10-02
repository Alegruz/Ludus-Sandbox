#include "ocean/ocean_settings.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>

namespace ludus::sandbox::ocean
{
namespace
{
[[nodiscard]] bool IsFinite(float32 value) noexcept
{
    return std::isfinite(value);
}

[[nodiscard]] float32 ClampTo(float32 value, const Bounds& bounds, float32 fallback) noexcept
{
    if (!IsFinite(value))
    {
        return fallback;
    }
    if (value < bounds.Min)
    {
        return bounds.Min;
    }
    if (value > bounds.Max)
    {
        return bounds.Max;
    }
    return value;
}

// value vs. fallback are distinct roles; callers always pass the field first.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
[[nodiscard]] float32 NormalizeDegrees(float32 degrees, float32 fallback) noexcept
{
    if (!IsFinite(degrees))
    {
        return fallback;
    }
    float32 wrapped = std::fmod(degrees, 360.0F);
    if (wrapped < 0.0F)
    {
        wrapped += 360.0F;
    }
    return wrapped;
}

[[nodiscard]] Color ClampColor(const Color& color, const Color& fallback) noexcept
{
    return Color{
        .R = ClampTo(color.R, kColorChannelBounds, fallback.R),
        .G = ClampTo(color.G, kColorChannelBounds, fallback.G),
        .B = ClampTo(color.B, kColorChannelBounds, fallback.B),
    };
}

// Append a float with a fixed, locale-independent format. Uses snprintf into a
// small buffer (allowed under the std-usage policy only inside this isolated
// serializer; the ocean layer is not engine infrastructure).
void AppendFloat(std::string& out, float32 value) noexcept
{
    std::array<char, 32> buffer{};
    const int written = std::snprintf(buffer.data(), buffer.size(), "%.6g", static_cast<double>(value));
    if (written > 0)
    {
        out.append(buffer.data(), static_cast<std::string::size_type>(written));
    }
    else
    {
        out.append("0");
    }
}

void AppendColor(std::string& out, std::string_view key, const Color& color) noexcept
{
    out.append("  \"");
    out.append(key);
    out.append("\": [");
    AppendFloat(out, color.R);
    out.append(", ");
    AppendFloat(out, color.G);
    out.append(", ");
    AppendFloat(out, color.B);
    out.append("]");
}

void AppendScalar(std::string& out, std::string_view key, float32 value) noexcept
{
    out.append("  \"");
    out.append(key);
    out.append("\": ");
    AppendFloat(out, value);
}

// --- Minimal JSON scanning helpers (object of known keys) -------------------

struct Scanner final
{
    std::string_view text;
    std::size_t pos = 0;

    void SkipWhitespace() noexcept
    {
        while (pos < text.size())
        {
            const char c = text[pos];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
            {
                ++pos;
            }
            else
            {
                break;
            }
        }
    }

    [[nodiscard]] bool Eof() noexcept
    {
        SkipWhitespace();
        return pos >= text.size();
    }

    [[nodiscard]] char Peek() noexcept
    {
        SkipWhitespace();
        return pos < text.size() ? text[pos] : '\0';
    }

    [[nodiscard]] bool Consume(char expected) noexcept
    {
        SkipWhitespace();
        if (pos < text.size() && text[pos] == expected)
        {
            ++pos;
            return true;
        }
        return false;
    }

    [[nodiscard]] bool ParseString(std::string& out) noexcept
    {
        SkipWhitespace();
        if (pos >= text.size() || text[pos] != '"')
        {
            return false;
        }
        ++pos;
        out.clear();
        while (pos < text.size())
        {
            const char c = text[pos++];
            if (c == '"')
            {
                return true;
            }
            if (c == '\\')
            {
                if (pos >= text.size())
                {
                    return false;
                }
                const char escaped = text[pos++];
                switch (escaped)
                {
                    case '"':
                        out.push_back('"');
                        break;
                    case '\\':
                        out.push_back('\\');
                        break;
                    case '/':
                        out.push_back('/');
                        break;
                    case 'n':
                        out.push_back('\n');
                        break;
                    case 't':
                        out.push_back('\t');
                        break;
                    default:
                        // Unsupported escape -> reject rather than guess.
                        return false;
                }
            }
            else
            {
                out.push_back(c);
            }
        }
        return false;
    }

    // Parse a JSON number into a double. Rejects non-finite/no-digit input.
    [[nodiscard]] bool ParseNumber(double& out) noexcept
    {
        SkipWhitespace();
        const std::size_t start = pos;
        while (pos < text.size())
        {
            const char c = text[pos];
            const bool numeric = (c >= '0' && c <= '9') || c == '+' || c == '-' || c == '.' || c == 'e' || c == 'E';
            if (!numeric)
            {
                break;
            }
            ++pos;
        }
        if (pos == start)
        {
            return false;
        }
        // Copy into a NUL-terminated buffer for strtod.
        const std::string token(text.substr(start, pos - start));
        char* end = nullptr;
        const double value = std::strtod(token.c_str(), &end);
        if (end != token.c_str() + token.size())
        {
            return false;
        }
        if (!std::isfinite(value))
        {
            return false;
        }
        out = value;
        return true;
    }
};

[[nodiscard]] std::string RangeMessage(std::string_view field, const Bounds& bounds) noexcept
{
    std::string message;
    message.append("'");
    message.append(field);
    message.append("' must be a finite number between ");
    AppendFloat(message, bounds.Min);
    message.append(" and ");
    AppendFloat(message, bounds.Max);
    message.append(".");
    return message;
}

} // namespace

OceanSettings DefaultSettings() noexcept
{
    return MakePreset(Preset::Drifting);
}

OceanSettings MakePreset(Preset preset) noexcept
{
    OceanSettings s{};
    switch (preset)
    {
        case Preset::Calm:
            s.CurrentDirectionDegrees = 20.0F;
            s.CurrentSpeedMetersPerSecond = 0.5F;
            s.WaveScaleMeters = 26.0F;
            s.WaveAnimationSpeed = 0.5F;
            s.WaveIntensity = 0.30F;
            s.FoamAmount = 0.12F;
            break;
        case Preset::Drifting:
            s.CurrentDirectionDegrees = 35.0F;
            s.CurrentSpeedMetersPerSecond = 1.5F;
            s.WaveScaleMeters = 18.0F;
            s.WaveAnimationSpeed = 1.0F;
            s.WaveIntensity = 0.55F;
            s.FoamAmount = 0.35F;
            break;
        case Preset::Choppy:
            s.CurrentDirectionDegrees = 70.0F;
            s.CurrentSpeedMetersPerSecond = 3.5F;
            s.WaveScaleMeters = 10.0F;
            s.WaveAnimationSpeed = 1.9F;
            s.WaveIntensity = 0.85F;
            s.FoamAmount = 0.70F;
            break;
    }
    // Palette is shared across presets (restrained target palette).
    return s;
}

OceanSettings Clamp(const OceanSettings& settings) noexcept
{
    const OceanSettings d = DefaultSettings();
    OceanSettings out{};
    out.CurrentDirectionDegrees = NormalizeDegrees(settings.CurrentDirectionDegrees, d.CurrentDirectionDegrees);
    out.CurrentSpeedMetersPerSecond =
        ClampTo(settings.CurrentSpeedMetersPerSecond, kCurrentSpeedBounds, d.CurrentSpeedMetersPerSecond);
    out.WaveScaleMeters = ClampTo(settings.WaveScaleMeters, kWaveScaleBounds, d.WaveScaleMeters);
    out.WaveAnimationSpeed = ClampTo(settings.WaveAnimationSpeed, kWaveAnimationSpeedBounds, d.WaveAnimationSpeed);
    out.WaveIntensity = ClampTo(settings.WaveIntensity, kWaveIntensityBounds, d.WaveIntensity);
    out.FoamAmount = ClampTo(settings.FoamAmount, kFoamAmountBounds, d.FoamAmount);
    out.DeepColor = ClampColor(settings.DeepColor, d.DeepColor);
    out.MidColor = ClampColor(settings.MidColor, d.MidColor);
    out.ShallowColor = ClampColor(settings.ShallowColor, d.ShallowColor);
    out.FoamColor = ClampColor(settings.FoamColor, d.FoamColor);
    return out;
}

namespace
{
[[nodiscard]] ValidationResult CheckScalar(float32 value, std::string_view field, const Bounds& bounds) noexcept
{
    if (!IsFinite(value) || value < bounds.Min || value > bounds.Max)
    {
        return ValidationResult{.Ok = false, .Message = RangeMessage(field, bounds)};
    }
    return ValidationResult{.Ok = true, .Message = {}};
}

[[nodiscard]] ValidationResult CheckColor(const Color& color, std::string_view field) noexcept
{
    const auto bad = [&](float32 v) noexcept {
        return !IsFinite(v) || v < kColorChannelBounds.Min || v > kColorChannelBounds.Max;
    };
    if (bad(color.R) || bad(color.G) || bad(color.B))
    {
        std::string message;
        message.append("'");
        message.append(field);
        message.append("' channels must each be a finite number between 0 and 1.");
        return ValidationResult{.Ok = false, .Message = message};
    }
    return ValidationResult{.Ok = true, .Message = {}};
}
} // namespace

ValidationResult Validate(const OceanSettings& settings) noexcept
{
    if (!IsFinite(settings.CurrentDirectionDegrees))
    {
        return ValidationResult{.Ok = false, .Message = "'currentDirectionDegrees' must be a finite number."};
    }
    if (auto r = CheckScalar(settings.CurrentSpeedMetersPerSecond, "currentSpeed", kCurrentSpeedBounds); !r.Ok)
    {
        return r;
    }
    if (auto r = CheckScalar(settings.WaveScaleMeters, "waveScale", kWaveScaleBounds); !r.Ok)
    {
        return r;
    }
    if (auto r = CheckScalar(settings.WaveAnimationSpeed, "waveAnimationSpeed", kWaveAnimationSpeedBounds); !r.Ok)
    {
        return r;
    }
    if (auto r = CheckScalar(settings.WaveIntensity, "waveIntensity", kWaveIntensityBounds); !r.Ok)
    {
        return r;
    }
    if (auto r = CheckScalar(settings.FoamAmount, "foamAmount", kFoamAmountBounds); !r.Ok)
    {
        return r;
    }
    if (auto r = CheckColor(settings.DeepColor, "deepColor"); !r.Ok)
    {
        return r;
    }
    if (auto r = CheckColor(settings.MidColor, "midColor"); !r.Ok)
    {
        return r;
    }
    if (auto r = CheckColor(settings.ShallowColor, "shallowColor"); !r.Ok)
    {
        return r;
    }
    if (auto r = CheckColor(settings.FoamColor, "foamColor"); !r.Ok)
    {
        return r;
    }
    return ValidationResult{.Ok = true, .Message = {}};
}

std::string ToJson(const OceanSettings& settings) noexcept
{
    const OceanSettings s = Clamp(settings);
    std::string out;
    out.reserve(512);
    out.append("{\n");
    out.append("  \"schemaVersion\": ");
    out.append(std::to_string(kSettingsSchemaVersion));
    out.append(",\n");
    out.append("  \"app\": \"ludus-sandbox-ocean\",\n");
    AppendScalar(out, "currentDirectionDegrees", s.CurrentDirectionDegrees);
    out.append(",\n");
    AppendScalar(out, "currentSpeed", s.CurrentSpeedMetersPerSecond);
    out.append(",\n");
    AppendScalar(out, "waveScale", s.WaveScaleMeters);
    out.append(",\n");
    AppendScalar(out, "waveAnimationSpeed", s.WaveAnimationSpeed);
    out.append(",\n");
    AppendScalar(out, "waveIntensity", s.WaveIntensity);
    out.append(",\n");
    AppendScalar(out, "foamAmount", s.FoamAmount);
    out.append(",\n");
    AppendColor(out, "deepColor", s.DeepColor);
    out.append(",\n");
    AppendColor(out, "midColor", s.MidColor);
    out.append(",\n");
    AppendColor(out, "shallowColor", s.ShallowColor);
    out.append(",\n");
    AppendColor(out, "foamColor", s.FoamColor);
    out.append("\n}\n");
    return out;
}

namespace
{
// Parse a 3-element color array [r, g, b].
[[nodiscard]] bool ParseColorArray(Scanner& scanner, Color& out) noexcept
{
    if (!scanner.Consume('['))
    {
        return false;
    }
    double channels[3] = {0.0, 0.0, 0.0};
    for (int i = 0; i < 3; ++i)
    {
        if (!scanner.ParseNumber(channels[i]))
        {
            return false;
        }
        if (i < 2 && !scanner.Consume(','))
        {
            return false;
        }
    }
    if (!scanner.Consume(']'))
    {
        return false;
    }
    out.R = static_cast<float32>(channels[0]);
    out.G = static_cast<float32>(channels[1]);
    out.B = static_cast<float32>(channels[2]);
    return true;
}
} // namespace

ParseResult FromJson(std::string_view json) noexcept
{
    ParseResult result{};
    Scanner scanner{.text = json, .pos = 0};

    if (!scanner.Consume('{'))
    {
        result.Message = "Settings must be a JSON object starting with '{'.";
        return result;
    }

    // Start from defaults so partial objects still produce a complete, valid
    // settings struct; unknown keys are skipped tolerantly.
    OceanSettings parsed = DefaultSettings();
    bool sawVersion = false;
    bool sawAnyKnownKey = false;

    if (scanner.Peek() != '}')
    {
        for (;;)
        {
            std::string key;
            if (!scanner.ParseString(key))
            {
                result.Message = "Expected a quoted field name.";
                return result;
            }
            if (!scanner.Consume(':'))
            {
                result.Message = "Expected ':' after field name '" + key + "'.";
                return result;
            }

            bool handled = true;
            if (key == "schemaVersion")
            {
                double v = 0.0;
                if (!scanner.ParseNumber(v))
                {
                    result.Message = "'schemaVersion' must be a number.";
                    return result;
                }
                const int version = static_cast<int>(v);
                if (version != kSettingsSchemaVersion)
                {
                    result.Message = "Unsupported settings schemaVersion " + std::to_string(version) +
                                     "; this build expects version " + std::to_string(kSettingsSchemaVersion) + ".";
                    return result;
                }
                sawVersion = true;
            }
            else if (key == "app")
            {
                std::string ignored;
                if (!scanner.ParseString(ignored))
                {
                    result.Message = "'app' must be a string.";
                    return result;
                }
            }
            else if (key == "currentDirectionDegrees" || key == "currentSpeed" || key == "waveScale" ||
                     key == "waveAnimationSpeed" || key == "waveIntensity" || key == "foamAmount")
            {
                double v = 0.0;
                if (!scanner.ParseNumber(v))
                {
                    result.Message = "'" + key + "' must be a finite number.";
                    return result;
                }
                const auto fv = static_cast<float32>(v);
                if (key == "currentDirectionDegrees")
                {
                    parsed.CurrentDirectionDegrees = fv;
                }
                else if (key == "currentSpeed")
                {
                    parsed.CurrentSpeedMetersPerSecond = fv;
                }
                else if (key == "waveScale")
                {
                    parsed.WaveScaleMeters = fv;
                }
                else if (key == "waveAnimationSpeed")
                {
                    parsed.WaveAnimationSpeed = fv;
                }
                else if (key == "waveIntensity")
                {
                    parsed.WaveIntensity = fv;
                }
                else
                {
                    parsed.FoamAmount = fv;
                }
                sawAnyKnownKey = true;
            }
            else if (key == "deepColor" || key == "midColor" || key == "shallowColor" || key == "foamColor")
            {
                Color color{};
                if (!ParseColorArray(scanner, color))
                {
                    result.Message = "'" + key + "' must be an array of three finite numbers [r, g, b].";
                    return result;
                }
                if (key == "deepColor")
                {
                    parsed.DeepColor = color;
                }
                else if (key == "midColor")
                {
                    parsed.MidColor = color;
                }
                else if (key == "shallowColor")
                {
                    parsed.ShallowColor = color;
                }
                else
                {
                    parsed.FoamColor = color;
                }
                sawAnyKnownKey = true;
            }
            else
            {
                handled = false;
            }

            if (!handled)
            {
                result.Message = "Unknown settings field '" + key + "'.";
                return result;
            }

            if (scanner.Consume(','))
            {
                continue;
            }
            break;
        }
    }

    if (!scanner.Consume('}'))
    {
        result.Message = "Expected '}' to close the settings object.";
        return result;
    }
    if (!scanner.Eof())
    {
        result.Message = "Unexpected trailing characters after the settings object.";
        return result;
    }
    if (!sawVersion)
    {
        result.Message = "Missing required 'schemaVersion' field.";
        return result;
    }
    if (!sawAnyKnownKey)
    {
        result.Message = "No recognizable settings fields were provided.";
        return result;
    }

    // Validate the raw parsed values so out-of-range input yields a friendly
    // message rather than silently snapping. Then clamp to guarantee a safe,
    // finite struct for the scene.
    const ValidationResult validation = Validate(parsed);
    if (!validation.Ok)
    {
        result.Message = validation.Message;
        return result;
    }

    result.Ok = true;
    result.Value = Clamp(parsed);
    return result;
}

// --- Coordinate mapping -----------------------------------------------------

float32 WorldViewWidthMeters(uint32 widthPx, uint32 heightPx) noexcept
{
    if (widthPx == 0 || heightPx == 0)
    {
        return kWorldViewHeightMeters;
    }
    const float32 aspect = static_cast<float32>(widthPx) / static_cast<float32>(heightPx);
    return kWorldViewHeightMeters * aspect;
}

Vec2 ScreenToWorld(float32 pixelX, float32 pixelY, uint32 widthPx, uint32 heightPx) noexcept
{
    if (widthPx == 0 || heightPx == 0 || !std::isfinite(pixelX) || !std::isfinite(pixelY))
    {
        return Vec2{.X = 0.0F, .Y = 0.0F};
    }
    const float32 w = static_cast<float32>(widthPx);
    const float32 h = static_cast<float32>(heightPx);
    const float32 metersPerPixel = kWorldViewHeightMeters / h;
    return Vec2{
        .X = (pixelX - w * 0.5F) * metersPerPixel,
        .Y = (h * 0.5F - pixelY) * metersPerPixel, // flip Y: screen down -> world up
    };
}

} // namespace ludus::sandbox::ocean
