// Workflows that cross features: what one feature does to what another keeps.

#include "TestSupport.h"

#include <peakemi/core/Campaign.h>
#include <peakemi/core/LimitCatalogue.h>
#include <peakemi/core/MeasurementEngine.h>
#include <peakemi/drivers/SimulatedDriver.h>
#include <peakemi/ui/CampaignDock.h>
#include <peakemi/ui/InstrumentDock.h>
#include <peakemi/ui/MainWindow.h>
#include <peakemi/ui/ResultsDock.h>
#include <peakemi/ui/RunConfigDock.h>

#include <QAction>
#include <QSettings>
#include <QStandardPaths>
#include <QTest>

#include <algorithm>
#include <chrono>

using namespace peakemi;

class WorkflowTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void remeasuredPeaksAreFiledUnderTheirCampaignTest();
};

void WorkflowTest::initTestCase()
{
    // Autosaves and settings go to a throwaway location, not the user's.
    QStandardPaths::setTestModeEnabled(true);
    QSettings{}.clear();
    registerMetaTypes();
    drivers::registerBuiltInDrivers();
}

void WorkflowTest::remeasuredPeaksAreFiledUnderTheirCampaignTest()
{
    ui::MainWindow window;
    auto* instruments = window.findChild<ui::InstrumentDock*>();
    auto* config = window.findChild<ui::RunConfigDock*>();
    auto* results = window.findChild<ui::ResultsDock*>();
    auto* campaigns = window.findChild<ui::CampaignDock*>();
    auto* start = window.findChild<QAction*>(QStringLiteral("actionStartRun"));
    QVERIFY(instruments != nullptr && config != nullptr && results != nullptr &&
            campaigns != nullptr && start != nullptr);

    // A radiated run against the simulated bench, which fails class B.
    RunConfiguration run;
    run.span = FrequencyRange{megahertz(30), gigahertz(1.0)};
    run.phase1Points = 2001;
    run.limits = {*builtInLimitLine("CISPR 32 Class B radiated 10 m (QP)")};
    run.verificationDetector = Detector::QuasiPeak;
    run.dwellTime = std::chrono::milliseconds{20};
    run.verificationSpan = Hertz{0};
    config->setConfiguration(run);

    const TransportDescriptor simulated{.kind = TransportKind::Simulated,
                                        .address = "simulated",
                                        .port = 0,
                                        .baudRate = 0,
                                        .terminator = "\n",
                                        .defaultTimeout = std::chrono::milliseconds{1000}};
    emit instruments->connectRequested(simulated, QString{});
    QTRY_VERIFY_WITH_TIMEOUT(start->isEnabled(), 10000);
    start->trigger();
    QTRY_VERIFY_WITH_TIMEOUT(start->isEnabled() && !results->points().empty(), 60000);

    // File that run under the horizontal radiated test and select it: the
    // results table now shows the test's points.
    auto campaign = campaignFromTemplate("cispr32-b", "workflow");
    QVERIFY(campaign.has_value());
    campaign->tests[2].session = window.session();
    campaigns->setCampaign(std::move(*campaign));
    campaigns->selectTest(2);
    QVERIFY(campaigns->activeTest() != nullptr);
    const auto filed = campaigns->activeTest()->session->results;
    QVERIFY(!filed.empty());
    QCOMPARE(results->points().size(), filed.size());

    // Re-measure the first peak: the test keeps as many points, one of them new.
    const auto target = filed.front();
    emit results->remeasureRequested({target.frequency});
    const auto refiled = [&] {
        const auto* test = campaigns->activeTest();
        if (test == nullptr || !test->session || test->session->results.size() != filed.size()) {
            return false;
        }
        return std::ranges::any_of(test->session->results, [&](const MeasurementPoint& point) {
            const auto apart = point.frequency > target.frequency
                                   ? point.frequency - target.frequency
                                   : target.frequency - point.frequency;
            return apart <= kilohertz(10) && point.measuredAt > target.measuredAt;
        });
    };
    QTRY_VERIFY_WITH_TIMEOUT(refiled(), 30000);
    QTRY_VERIFY_WITH_TIMEOUT(start->isEnabled(), 10000);
    QCOMPARE(results->points().size(), filed.size());
}

QTEST_MAIN(WorkflowTest)
#include "WorkflowTest.moc"
