#include <peakemi/ui/CampaignPlot.h>
#include <peakemi/ui/PainterPlotBackend.h>
#include <peakemi/ui/SessionPlot.h>

namespace peakemi::ui {
namespace {

[[nodiscard]] QString qs(std::string_view text)
{
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

} // namespace

QColor ambientColour()
{
    return QColor{0x8A, 0x8F, 0x98};
}

void showTest(IPlotBackend& plot, const CampaignTest& test)
{
    plot.clearSeries();
    const auto& config = test.session ? test.session->config : test.config;

    for (const auto& limit : config.limits) {
        PlotSeries series;
        series.id = QStringLiteral("limit:%1").arg(qs(limit.name));
        series.label = qs(limit.name);
        series.colour = limitColour();
        series.role = PlotRole::Limit;
        series.width = 1.6;
        series.points = limitPoints(limit, config.span);
        plot.setSeries(series);
    }

    AmplitudeUnit unit = config.limits.empty() ? AmplitudeUnit::dBuV : config.limits.front().unit;

    // The ambient first, so the measurement is drawn over it.
    if (test.ambient) {
        PlotSeries ambient;
        ambient.id = QStringLiteral("ambient");
        ambient.label = QObject::tr("Ambient (EUT off)");
        ambient.colour = ambientColour();
        ambient.points = tracePoints(*test.ambient);
        plot.setSeries(ambient);
        unit = test.ambient->unit;
    }

    if (test.session && !test.session->traces.empty()) {
        const auto& trace = test.session->traces.back();
        unit = trace.unit;
        PlotSeries series;
        series.id = QStringLiteral("trace:live");
        series.label = trace.label.empty() ? QObject::tr("EUT") : qs(trace.label);
        series.colour = traceColour();
        series.role = PlotRole::Trace;
        series.points = tracePoints(trace);
        plot.setSeries(series);
        plot.setSettingsSummary(settingsSummary(trace.params, trace.unit));
    } else {
        plot.setSettingsSummary(settingsSummary(config.phase1SweepParams(config.span), unit));
    }

    if (test.session && !test.session->results.empty()) {
        PlotSeries verified;
        verified.id = QStringLiteral("phase2");
        verified.label = QObject::tr("Verified points");
        verified.colour = verifiedPointColour();
        verified.role = PlotRole::Verified;
        verified.style = PlotStyle::Points;
        verified.width = 7.0;
        for (const auto& point : test.session->results) {
            verified.points.append(
                QPointF{static_cast<double>(point.frequency.value()), point.correctedAmplitude});
        }
        plot.setSeries(verified);
    }

    plot.setAmplitudeLabel(qs(amplitudeUnitKey(unit)));
    plot.setFrequencyRange(config.span);
    plot.autoScale();
}

QImage renderTestSpectrum(const CampaignTest& test, QSize size)
{
    PainterPlotBackend plot;
    plot.resize(size);
    // The report is printed on white paper, whatever the screen looked like.
    plot.setDisplayScheme(Scheme::Light);
    showTest(plot, test);
    return plot.renderToImage(size);
}

} // namespace peakemi::ui
