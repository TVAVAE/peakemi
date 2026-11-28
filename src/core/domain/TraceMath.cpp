#include <peakemi/core/AtomicFileWriter.h>
#include <peakemi/core/SessionSerializer.h>
#include <peakemi/core/TraceMath.h>

#include <QFileInfo>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <limits>
#include <utility>

namespace peakemi {
namespace {

constexpr double NotANumber = std::numeric_limits<double>::quiet_NaN();

constexpr std::array<std::pair<TraceOperation, std::string_view>, 4> OperationKeys{{
    {TraceOperation::Maximum, "maximum"},
    {TraceOperation::Minimum, "minimum"},
    {TraceOperation::Average, "average"},
    {TraceOperation::Difference, "difference"},
}};

[[nodiscard]] std::string_view trimmed(std::string_view text)
{
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) {
        return {};
    }
    const auto last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

[[nodiscard]] std::optional<double> parseNumber(std::string_view text)
{
    text = trimmed(text);
    double value = 0.0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size()) {
        return std::nullopt;
    }
    return value;
}

} // namespace

std::string_view traceOperationKey(TraceOperation operation)
{
    for (const auto& [value, key] : OperationKeys) {
        if (value == operation) {
            return key;
        }
    }
    return "maximum";
}

std::optional<TraceOperation> traceOperationFromKey(std::string_view key)
{
    for (const auto& [value, name] : OperationKeys) {
        if (name == key) {
            return value;
        }
    }
    return std::nullopt;
}

double amplitudeAt(const Trace& trace, Hertz frequency)
{
    const int count = std::min(trace.size(), trace.axis.size());
    if (count <= 0) {
        return NotANumber;
    }
    const Hertz first = trace.axis.frequencyAt(0);
    const Hertz last = trace.axis.frequencyAt(count - 1);
    if (frequency < first || frequency > last) {
        return NotANumber;
    }
    if (count == 1) {
        return trace.amplitudes.front();
    }
    // The first bin at or above the frequency: bins ascend, so bisect.
    int low = 0;
    int high = count - 1;
    while (low < high) {
        const int middle = (low + high) / 2;
        if (trace.axis.frequencyAt(middle) < frequency) {
            low = middle + 1;
        } else {
            high = middle;
        }
    }
    const int upper = low;
    const Hertz upperFrequency = trace.axis.frequencyAt(upper);
    const double upperValue = trace.amplitudes[static_cast<std::size_t>(upper)];
    if (upper == 0 || upperFrequency == frequency) {
        return upperValue;
    }
    const Hertz lowerFrequency = trace.axis.frequencyAt(upper - 1);
    const double lowerValue = trace.amplitudes[static_cast<std::size_t>(upper - 1)];
    const double width = static_cast<double>((upperFrequency - lowerFrequency).value());
    if (width <= 0.0) {
        return upperValue;
    }
    const double fraction = static_cast<double>((frequency - lowerFrequency).value()) / width;
    return lowerValue + (upperValue - lowerValue) * fraction;
}

Result<Trace> combineTraces(const Trace& a, const Trace& b, TraceOperation operation)
{
    if (a.isEmpty() || b.isEmpty()) {
        return fail(ErrorCode::InvalidConfiguration, "both traces need data to be combined");
    }
    if (a.unit != b.unit) {
        return fail(ErrorCode::InvalidConfiguration,
                    "the traces are in different units (" + std::string{amplitudeUnitKey(a.unit)} +
                        " and " + std::string{amplitudeUnitKey(b.unit)} + ")");
    }

    Trace result = a;
    result.corrected = a.corrected && b.corrected;
    result.acquiredAt = std::max(a.acquiredAt, b.acquiredAt);
    bool overlap = false;
    for (int i = 0; i < result.size(); ++i) {
        const double left = a.amplitudes[static_cast<std::size_t>(i)];
        const double right = amplitudeAt(b, a.axis.frequencyAt(i));
        double value = NotANumber;
        if (std::isfinite(left) && std::isfinite(right)) {
            overlap = true;
            switch (operation) {
                case TraceOperation::Maximum:
                    value = std::max(left, right);
                    break;
                case TraceOperation::Minimum:
                    value = std::min(left, right);
                    break;
                case TraceOperation::Average:
                    value = (left + right) / 2.0;
                    break;
                case TraceOperation::Difference:
                    value = left - right;
                    break;
            }
        }
        result.amplitudes[static_cast<std::size_t>(i)] = value;
    }
    if (!overlap) {
        return fail(ErrorCode::InvalidConfiguration, "the traces share no frequency range");
    }

    const auto& nameA = a.label.empty() ? std::string{"A"} : a.label;
    const auto& nameB = b.label.empty() ? std::string{"B"} : b.label;
    switch (operation) {
        case TraceOperation::Maximum:
            result.label = "max(" + nameA + ", " + nameB + ")";
            break;
        case TraceOperation::Minimum:
            result.label = "min(" + nameA + ", " + nameB + ")";
            break;
        case TraceOperation::Average:
            result.label = "avg(" + nameA + ", " + nameB + ")";
            break;
        case TraceOperation::Difference:
            result.label = nameA + " - " + nameB + " [dB]";
            break;
    }
    return result;
}

namespace trace_io {

Result<Trace> fromCsvText(std::string_view text)
{
    Trace trace;
    std::optional<AmplitudeUnit> unit;
    std::vector<Hertz> frequencies;
    std::size_t start = 0;
    int lineNumber = 0;
    while (start <= text.size()) {
        auto end = text.find('\n', start);
        if (end == std::string_view::npos) {
            end = text.size();
        }
        const auto line = trimmed(text.substr(start, end - start));
        start = end + 1;
        ++lineNumber;
        if (line.empty()) {
            continue;
        }
        if (line.front() == '#') {
            // "# key: value" metadata, as CsvExporter writes it.
            const auto body = trimmed(line.substr(1));
            const auto colon = body.find(':');
            if (colon == std::string_view::npos) {
                continue;
            }
            const auto key = trimmed(body.substr(0, colon));
            const auto value = trimmed(body.substr(colon + 1));
            if (key == "trace") {
                trace.label = std::string{value};
            } else if (key == "unit") {
                unit = amplitudeUnitFromKey(value);
            } else if (key == "detector") {
                trace.detector = detectorFromKey(value).value_or(Detector::Peak);
            } else if (key == "corrections_applied") {
                trace.corrected = value == "yes";
            }
            continue;
        }
        const auto comma = line.find(',');
        if (comma == std::string_view::npos) {
            return fail(ErrorCode::ParseFailure,
                        "line " + std::to_string(lineNumber) + " is not 'frequency,amplitude'");
        }
        const auto frequency = parseNumber(line.substr(0, comma));
        const auto amplitude = parseNumber(line.substr(comma + 1));
        if (!frequency || !amplitude) {
            if (frequencies.empty()) {
                // The header row: "frequency_hz,amplitude_dBuV".
                const auto header = trimmed(line.substr(comma + 1));
                constexpr std::string_view Prefix = "amplitude_";
                if (header.starts_with(Prefix) && !unit) {
                    unit = amplitudeUnitFromKey(header.substr(Prefix.size()));
                }
                continue;
            }
            return fail(ErrorCode::ParseFailure,
                        "line " + std::to_string(lineNumber) + " does not hold two numbers");
        }
        if (!std::isfinite(*frequency) || *frequency <= 0.0 || *frequency > 1e12) {
            return fail(ErrorCode::ParseFailure,
                        "line " + std::to_string(lineNumber) + " has no usable frequency");
        }
        const Hertz hertz{static_cast<std::int64_t>(std::llround(*frequency))};
        if (!frequencies.empty() && hertz <= frequencies.back()) {
            return fail(ErrorCode::ParseFailure,
                        "frequencies must ascend (line " + std::to_string(lineNumber) + ")");
        }
        frequencies.push_back(hertz);
        trace.amplitudes.push_back(*amplitude);
    }
    if (trace.amplitudes.size() < 2) {
        return fail(ErrorCode::ParseFailure, "a trace needs at least two points");
    }
    trace.unit = unit.value_or(AmplitudeUnit::dBuV);
    trace.axis.start = frequencies.front();
    trace.axis.stop = frequencies.back();
    trace.axis.points = static_cast<int>(frequencies.size());
    trace.axis.explicitPoints = std::move(frequencies);
    return trace;
}

Result<Trace> load(const QString& path)
{
    auto content = readFile(path);
    if (!content) {
        return std::unexpected(content.error());
    }
    const QFileInfo file{path};
    Result<Trace> trace = fail(ErrorCode::ParseFailure, "no trace");
    if (file.suffix().compare(QStringLiteral("csv"), Qt::CaseInsensitive) == 0) {
        trace = fromCsvText(*content);
    } else {
        auto session = SessionSerializer::fromJson(*content);
        if (!session) {
            return std::unexpected(session.error());
        }
        if (session->traces.empty()) {
            return fail(ErrorCode::ParseFailure, "the session holds no trace");
        }
        trace = session->traces.back();
    }
    if (trace && trace->label.empty()) {
        trace->label = file.completeBaseName().toStdString();
    }
    return trace;
}

} // namespace trace_io

} // namespace peakemi
