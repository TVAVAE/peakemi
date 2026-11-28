#pragma once

#include <peakemi/core/Error.h>
#include <peakemi/core/Trace.h>

#include <QString>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace peakemi {

/// How two traces are combined into one.
enum class TraceOperation : std::uint8_t
{
    Maximum,   ///< worst case of the two: L and N, or two supply lines
    Minimum,   ///< what both share, e.g. a steady ambient
    Average,   ///< mean of the two, in dB
    Difference ///< A - B in dB: the effect of a change, before against after
};

[[nodiscard]] std::string_view traceOperationKey(TraceOperation operation);
[[nodiscard]] std::optional<TraceOperation> traceOperationFromKey(std::string_view key);

/// @p trace's amplitude at @p frequency, interpolated linearly between the two
/// nearest bins; NaN outside the trace.
[[nodiscard]] double amplitudeAt(const Trace& trace, Hertz frequency);

/// @p a and @p b combined point by point on @p a's frequency axis, where both
/// cover the frequency (elsewhere the result is NaN). Refused when the traces
/// are in different units: a worst case of dBuV and dBuV/m means nothing.
[[nodiscard]] Result<Trace> combineTraces(const Trace& a, const Trace& b, TraceOperation operation);

/// Reading traces back: the CSV PeakEmi exports, or the last trace of a
/// session file.
namespace trace_io {

/// "frequency_hz,amplitude_<unit>" rows, with the "# key: value" metadata
/// PeakEmi writes above them; the unit is taken from either.
[[nodiscard]] Result<Trace> fromCsvText(std::string_view text);

/// A ".csv" trace export, or a session file, whose last trace is taken.
[[nodiscard]] Result<Trace> load(const QString& path);

} // namespace trace_io

} // namespace peakemi
