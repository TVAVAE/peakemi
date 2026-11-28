#include "TestSupport.h"

#include <peakemi/core/LimitCatalogue.h>
#include <peakemi/core/MeasurementEngine.h>
#include <peakemi/core/Session.h>
#include <peakemi/core/TraceMath.h>
#include <peakemi/drivers/SimulatedDriver.h>
#include <peakemi/reporting/CsvExporter.h>
#include <peakemi/ui/CurveEditorDialog.h>
#include <peakemi/ui/PainterPlotBackend.h>
#include <peakemi/ui/ResultsDock.h>
#include <peakemi/ui/TracesDock.h>

#include <QSignalSpy>
#include <QTest>

#include <cmath>
#include <memory>

using namespace peakemi;

namespace {

/// 30 to 100 MHz in 10 MHz steps, rising 1 dB per step from @p base.
[[nodiscard]] Trace ramp(double base, AmplitudeUnit unit = AmplitudeUnit::dBuV)
{
    Trace trace;
    trace.axis = FrequencyAxis::linear(FrequencyRange{megahertz(30), megahertz(100)}, 8);
    for (int i = 0; i < 8; ++i) {
        trace.amplitudes.push_back(base + i);
    }
    trace.unit = unit;
    trace.label = "ramp";
    return trace;
}

[[nodiscard]] MeasurementPoint pointAt(double frequencyMHz, double level)
{
    MeasurementPoint point;
    point.frequency = megahertz(frequencyMHz);
    point.correctedAmplitude = level;
    point.marginDb = 40.0 - level;
    point.verdict = level > 40.0 ? Verdict::Fail : Verdict::Pass;
    return point;
}

} // namespace

class AnalysisTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void amplitudeIsInterpolatedBetweenBins();
    void traceMathWorksOnTheFirstTracesAxis();
    void traceMathRefusesWhatCannotBeCombined();
    void exportedTracesReadBack();
    void malformedTraceCsvIsRefused();
    void verifyOnlyRunsSkipTheScan();
    void plotMarkersReadTheMeasurement();
    void remeasuredPointsReplaceTheirPredecessors();
    void referencesBecomeColouredOverlays();
    void editorRoundTripsALimitAndACorrection();
};

void AnalysisTest::initTestCase()
{
    registerMetaTypes();
}

void AnalysisTest::amplitudeIsInterpolatedBetweenBins()
{
    const auto trace = ramp(10.0);
    QCOMPARE(amplitudeAt(trace, megahertz(30)), 10.0);
    QCOMPARE(amplitudeAt(trace, megahertz(100)), 17.0);
    QCOMPARE(amplitudeAt(trace, megahertz(45)), 11.5);
    QVERIFY(std::isnan(amplitudeAt(trace, megahertz(29))));
    QVERIFY(std::isnan(amplitudeAt(trace, megahertz(101))));
}

void AnalysisTest::traceMathWorksOnTheFirstTracesAxis()
{
    auto a = ramp(10.0);
    a.label = "L";
    auto b = ramp(12.0);
    b.label = "N";
    b.amplitudes[3] = 0.0; // N is lower than L at 60 MHz only

    const auto worst = combineTraces(a, b, TraceOperation::Maximum);
    const auto reason = test::errorText(worst);
    QVERIFY2(worst.has_value(), reason.constData());
    QCOMPARE(worst->label, std::string{"max(L, N)"});
    QCOMPARE(worst->amplitudes[0], 12.0);
    QCOMPARE(worst->amplitudes[3], 13.0);
    QCOMPARE(worst->axis, a.axis);

    QCOMPARE(combineTraces(a, b, TraceOperation::Minimum)->amplitudes[3], 0.0);
    QCOMPARE(combineTraces(a, b, TraceOperation::Average)->amplitudes[0], 11.0);
    const auto difference = combineTraces(a, b, TraceOperation::Difference);
    QCOMPARE(difference->amplitudes[0], -2.0);
    QVERIFY(difference->label.find("[dB]") != std::string::npos);

    // Where B has no data, the result has none either.
    Trace narrow = ramp(0.0);
    narrow.axis = FrequencyAxis::linear(FrequencyRange{megahertz(50), megahertz(70)}, 8);
    const auto partial = combineTraces(a, narrow, TraceOperation::Maximum);
    QVERIFY(partial.has_value());
    QVERIFY(std::isnan(partial->amplitudes[0]));
    QVERIFY(std::isfinite(partial->amplitudes[3]));
}

void AnalysisTest::traceMathRefusesWhatCannotBeCombined()
{
    const auto mixed =
        combineTraces(ramp(0.0), ramp(0.0, AmplitudeUnit::dBuV_per_m), TraceOperation::Maximum);
    QVERIFY(!mixed.has_value());
    QVERIFY(mixed.error().detail.find("units") != std::string::npos);

    Trace elsewhere = ramp(0.0);
    elsewhere.axis = FrequencyAxis::linear(FrequencyRange{megahertz(200), megahertz(300)}, 8);
    QVERIFY(!combineTraces(ramp(0.0), elsewhere, TraceOperation::Maximum).has_value());
}

void AnalysisTest::exportedTracesReadBack()
{
    auto trace = ramp(41.25, AmplitudeUnit::dBuV_per_m);
    trace.label = "Before the ferrite";
    trace.detector = Detector::Peak;
    trace.corrected = true;
    const auto csv = reporting::csv::traceToCsv(Session{}, trace);

    const auto back = trace_io::fromCsvText(csv);
    const auto reason = test::errorText(back);
    QVERIFY2(back.has_value(), reason.constData());
    QCOMPARE(back->label, trace.label);
    QCOMPARE(back->unit, AmplitudeUnit::dBuV_per_m);
    QVERIFY(back->corrected);
    QCOMPARE(back->size(), trace.size());
    for (int i = 0; i < trace.size(); ++i) {
        QCOMPARE(back->axis.frequencyAt(i), trace.axis.frequencyAt(i));
        QCOMPARE(back->amplitudes[static_cast<std::size_t>(i)],
                 trace.amplitudes[static_cast<std::size_t>(i)]);
    }

    // A bare two-column file reads too, taking its unit from the header.
    const auto bare =
        trace_io::fromCsvText("frequency_hz,amplitude_dBuA\n150000,20\n300000,25.5\n");
    QVERIFY(bare.has_value());
    QCOMPARE(bare->unit, AmplitudeUnit::dBuA);
    QCOMPARE(bare->amplitudes[1], 25.5);
}

void AnalysisTest::malformedTraceCsvIsRefused()
{
    QVERIFY(!trace_io::fromCsvText("100,1\n50,2\n").has_value()); // descending
    QVERIFY(!trace_io::fromCsvText("100,1\n200,abc\n").has_value());
    QVERIFY(!trace_io::fromCsvText("100,1\n").has_value()); // one point
    QVERIFY(!trace_io::fromCsvText("-5,1\n200,2\n").has_value());
}

void AnalysisTest::verifyOnlyRunsSkipTheScan()
{
    RunConfiguration config;
    config.span = FrequencyRange{megahertz(30), gigahertz(1.0)};
    config.limits = {*builtInLimitLine("CISPR 32 Class B radiated 10 m (QP)")};
    config.verificationDetector = Detector::QuasiPeak;
    config.dwellTime = std::chrono::milliseconds{20};
    config.verificationSpan = Hertz{0};
    config.autosave = false;
    config.passes = 3; // ignored: re-measuring is one pass
    config.verifyOnly = {megahertz(48), megahertz(667)};

    auto driver = std::make_shared<drivers::SimulatedDriver>();
    QVERIFY(driver->open(nullptr).has_value());
    MeasurementEngine engine;
    engine.setDriver(driver);
    engine.setConfiguration(config);

    QSignalSpy traces{&engine, &MeasurementEngine::traceAcquired};
    QSignalSpy peaks{&engine, &MeasurementEngine::peaksFlagged};
    QSignalSpy points{&engine, &MeasurementEngine::pointMeasured};
    QSignalSpy finished{&engine, &MeasurementEngine::runFinished};
    QSignalSpy failed{&engine, &MeasurementEngine::runFailed};
    engine.start();

    QCOMPARE(failed.count(), 0);
    QCOMPARE(finished.count(), 1);
    QCOMPARE(traces.count(), 0); // no scan
    QCOMPARE(peaks.count(), 0);
    QCOMPARE(points.count(), 2);
    const auto session = finished.front().front().value<Session>();
    QCOMPARE(session.results.size(), std::size_t{2});
    QCOMPARE(session.results[0].frequency, megahertz(48));
    QCOMPARE(session.results[1].frequency, megahertz(667));
    QCOMPARE(session.results[0].detector, Detector::QuasiPeak);
}

void AnalysisTest::plotMarkersReadTheMeasurement()
{
    ui::PainterPlotBackend plot;
    plot.resize(800, 400);
    QVERIFY(std::isnan(plot.measuredAmplitudeAt(50e6))); // nothing measured yet

    ui::PlotSeries trace;
    trace.id = QStringLiteral("trace:live");
    trace.role = ui::PlotRole::Trace;
    for (int i = 0; i <= 100; ++i) {
        trace.points.append(QPointF{30e6 + i * 1e6, i == 40 ? 60.0 : 20.0});
    }
    plot.setSeries(trace);
    QCOMPARE(plot.measuredAmplitudeAt(70e6), 60.0);
    QCOMPARE(plot.measuredAmplitudeAt(70.2e6), 60.0); // nearest bin

    plot.placeUserMarker(2, 50e6);
    plot.placeUserMarker(1, 70e6);
    QCOMPARE(plot.userMarkers().size(), 2);
    QCOMPARE(plot.userMarkers().front().number, 1); // kept in number order
    plot.placeUserMarker(1, 80e6);                  // moving M1 replaces it
    QCOMPARE(plot.userMarkers().size(), 2);
    QCOMPARE(plot.userMarkers().front().frequency, 80e6);

    // Overlays and markers are drawn without trouble, and markers survive
    // the measurement being cleared.
    plot.setOverlaySeries({trace});
    QVERIFY(!plot.renderToImage(QSize{800, 400}).isNull());
    plot.clearSeries();
    QCOMPARE(plot.userMarkers().size(), 2);
    plot.clearUserMarkers();
    QVERIFY(plot.userMarkers().isEmpty());
}

void AnalysisTest::remeasuredPointsReplaceTheirPredecessors()
{
    ui::ResultsDock dock;
    dock.setPoints({pointAt(100.0, 45.0), pointAt(200.0, 30.0)});

    // Re-measured 50 kHz away -- within the drift tolerance -- and now passing.
    dock.mergePoints({pointAt(100.05, 38.0)}, kilohertz(100));
    QCOMPARE(dock.points().size(), std::size_t{2});
    QCOMPARE(dock.points()[0].frequency, megahertz(100.05));
    QCOMPARE(dock.points()[0].correctedAmplitude, 38.0);

    // A frequency nothing was measured at before is added, in order.
    dock.mergePoints({pointAt(150.0, 33.0)}, kilohertz(100));
    QCOMPARE(dock.points().size(), std::size_t{3});
    QCOMPARE(dock.points()[1].frequency, megahertz(150));
}

void AnalysisTest::referencesBecomeColouredOverlays()
{
    ui::TracesDock dock;
    QSignalSpy changed{&dock, &ui::TracesDock::overlaysChanged};
    auto before = ramp(30.0);
    before.label = "Before";
    auto after = ramp(25.0);
    after.label = "After";
    dock.addReference(before, QStringLiteral("test"));
    dock.addReference(after, QStringLiteral("test"));
    QCOMPARE(changed.count(), 2);

    const auto overlays = dock.overlaySeries();
    QCOMPARE(overlays.size(), 2);
    QCOMPARE(overlays[0].label, QStringLiteral("Before"));
    QCOMPARE(overlays[0].points.size(), 8);
    QVERIFY(overlays[0].colour != overlays[1].colour);
    QCOMPARE(overlays[0].role, ui::PlotRole::Custom);
}

void AnalysisTest::editorRoundTripsALimitAndACorrection()
{
    const auto builtIn = *builtInLimitLine("CISPR 32 Class B conducted (QP)");
    ui::CurveEditorDialog limitEditor{ui::CurveEditorDialog::Mode::Limit};
    limitEditor.setLimit(builtIn);
    const auto limit = limitEditor.limit();
    QCOMPARE(limit.name, builtIn.name);
    QCOMPARE(limit.detector, builtIn.detector);
    QCOMPARE(limit.unit, builtIn.unit);
    QVERIFY(!limit.builtIn); // an edited limit is never a catalogue entry
    QCOMPARE(limit.points.size(), builtIn.points.size());
    for (std::size_t i = 0; i < limit.points.size(); ++i) {
        QCOMPARE(limit.points[i].frequency, builtIn.points[i].frequency);
        QCOMPARE(limit.points[i].amplitude, builtIn.points[i].amplitude);
        QCOMPARE(limit.points[i].interpolationToNext, builtIn.points[i].interpolationToNext);
    }
    QVERIFY(limit.validate().has_value());

    CorrectionTable cable{.name = "RG-58 2 m",
                          .kind = CorrectionKind::CableLoss,
                          .enabled = false,
                          .points = {{megahertz(0.15), 0.1}, {megahertz(30), 0.4}}};
    ui::CurveEditorDialog correctionEditor{ui::CurveEditorDialog::Mode::Correction};
    correctionEditor.setCorrection(cable);
    QCOMPARE(correctionEditor.correction(), cable);
}

QTEST_MAIN(AnalysisTest)
#include "AnalysisTest.moc"
