#include <peakemi/ui/PainterPlotBackend.h>
#include <peakemi/ui/SessionPlot.h>

#include <QGuiApplication>
#include <QPalette>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace peakemi::ui {
// The colours live here rather than in the window, so the image the report
// embeds is drawn with the same palette the operator approved on screen.

QColor traceColour()
{
    return QColor{0x1A, 0x73, 0xE8};
}

QColor limitColour()
{
    return QColor{0xC0, 0x39, 0x2B};
}

QColor verifiedPointColour()
{
    const bool dark = QGuiApplication::palette().color(QPalette::Base).lightness() < 128;
    return dark ? QColor{0x4C, 0xD9, 0x8C} : QColor{0x0B, 0x80, 0x43};
}

QVector<QPointF> tracePoints(const Trace& trace)
{
    QVector<QPointF> points;
    points.reserve(trace.size());
    for (int i = 0; i < trace.size(); ++i) {
        points.append(QPointF{static_cast<double>(trace.axis.frequencyAt(i).value()),
                              trace.amplitudes[static_cast<std::size_t>(i)]});
    }
    return points;
}

namespace {

[[nodiscard]] QString formatBandwidth(Hertz bandwidth)
{
    const auto hertz = static_cast<double>(bandwidth.value());
    if (hertz >= 1e6) {
        return QStringLiteral("%1 MHz").arg(hertz / 1e6, 0, 'g', 4);
    }
    if (hertz >= 1e3) {
        return QStringLiteral("%1 kHz").arg(hertz / 1e3, 0, 'g', 4);
    }
    return QStringLiteral("%1 Hz").arg(hertz, 0, 'g', 4);
}

/// The abbreviations an analyzer shows next to its trace.
[[nodiscard]] QString detectorAbbreviation(Detector detector)
{
    switch (detector) {
        case Detector::Peak:
            return QStringLiteral("PK");
        case Detector::QuasiPeak:
            return QStringLiteral("QP");
        case Detector::Average:
            return QStringLiteral("AV");
        case Detector::Rms:
            return QStringLiteral("RMS");
        case Detector::Sample:
            return QStringLiteral("SA");
    }
    return {};
}

} // namespace

QStringList settingsSummary(const SweepParams& params, AmplitudeUnit unit)
{
    const auto unitKey = amplitudeUnitKey(unit);
    const auto unitText = QString::fromUtf8(unitKey.data(), static_cast<qsizetype>(unitKey.size()));

    QStringList items;
    items << (params.rbw > Hertz{0} ? QStringLiteral("RBW %1").arg(formatBandwidth(params.rbw))
                                    : QStringLiteral("RBW auto"));
    if (params.vbw > Hertz{0}) {
        items << QStringLiteral("VBW %1").arg(formatBandwidth(params.vbw));
    }
    items << QStringLiteral("Det %1").arg(detectorAbbreviation(params.detector));
    items << QStringLiteral("Ref %1 %2").arg(params.refLevel.value(), 0, 'f', 1).arg(unitText);
    items << (params.automaticAttenuation
                  ? QStringLiteral("Att auto")
                  : QStringLiteral("Att %1 dB").arg(params.attenuation.value(), 0, 'f', 0));
    if (params.preamp) {
        items << QStringLiteral("Preamp on");
    }
    if (params.sweepTime.count() > 0) {
        items << QStringLiteral("SWT %1 ms").arg(params.sweepTime.count());
    }
    items << QStringLiteral("Pts %1").arg(params.points);
    return items;
}

QVector<QPointF> limitPoints(const LimitLine& limit, FrequencyRange span)
{
    QVector<QPointF> points;
    const auto coverage = limit.coverage();
    const auto start = std::max(coverage.start, span.start);
    const auto stop = std::min(coverage.stop, span.stop);
    if (stop <= start) {
        return points;
    }

    constexpr int Samples = 400;
    for (int i = 0; i <= Samples; ++i) {
        const double fraction = static_cast<double>(i) / Samples;
        const auto frequency = start + Hertz{static_cast<std::int64_t>(
                                           fraction * static_cast<double>((stop - start).value()))};
        const double value = limit.evaluateAt(frequency);
        if (std::isfinite(value)) {
            points.append(QPointF{static_cast<double>(frequency.value()), value});
        }
    }
    return points;
}

void showSession(IPlotBackend& plot, const Session& session)
{
    plot.clearSeries();

    for (const auto& limit : session.config.limits) {
        PlotSeries series;
        series.id = QStringLiteral("limit:%1").arg(QString::fromStdString(limit.name));
        series.label = QString::fromStdString(limit.name);
        series.colour = limitColour();
        series.role = PlotRole::Limit;
        series.width = 1.6;
        series.points = limitPoints(limit, session.config.span);
        plot.setSeries(series);
    }

    AmplitudeUnit unit =
        session.config.limits.empty() ? AmplitudeUnit::dBuV : session.config.limits.front().unit;
    if (!session.traces.empty()) {
        const auto& trace = session.traces.back();
        unit = trace.unit;

        PlotSeries series;
        series.id = QStringLiteral("trace:live");
        series.label = QString::fromStdString(trace.label);
        series.colour = traceColour();
        series.role = PlotRole::Trace;
        series.points = tracePoints(trace);
        plot.setSeries(series);
        plot.setSettingsSummary(settingsSummary(trace.params, trace.unit));
    } else {
        plot.setSettingsSummary(
            settingsSummary(session.config.phase1SweepParams(session.config.span), unit));
    }
    plot.setAmplitudeLabel(QString::fromUtf8(
        amplitudeUnitKey(unit).data(), static_cast<qsizetype>(amplitudeUnitKey(unit).size())));

    if (!session.results.empty()) {
        PlotSeries verified;
        verified.id = QStringLiteral("phase2");
        verified.colour = verifiedPointColour();
        verified.role = PlotRole::Verified;
        verified.style = PlotStyle::Points;
        verified.width = 7.0;
        verified.points.reserve(static_cast<qsizetype>(session.results.size()));
        for (const auto& point : session.results) {
            verified.points.append(
                QPointF{static_cast<double>(point.frequency.value()), point.correctedAmplitude});
        }
        plot.setSeries(verified);
    }

    plot.setFrequencyRange(session.config.span);
    plot.autoScale();
}

QImage renderSessionSpectrum(const Session& session, QSize size)
{
    // Never shown: the widget is resized to the target size and painted into an
    // image, so nothing about this needs a window or a compositor.
    PainterPlotBackend plot;
    plot.resize(size);
    // The report is printed on white paper, whatever the screen looked like.
    plot.setDisplayScheme(Scheme::Light);
    showSession(plot, session);
    return plot.renderToImage(size);
}

} // namespace peakemi::ui
