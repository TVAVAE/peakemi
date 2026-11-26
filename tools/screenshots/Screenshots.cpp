// Generates the screenshots embedded in README.md.
//
// The images are produced by driving the real main window against the simulated
// analyzer, so they cannot drift away from what the application actually looks
// like: regenerate them with
//
//     cmake --preset debug -DPEAKEMI_BUILD_TOOLS=ON
//     cmake --build --preset debug --target peakemi_screenshots
//     QT_QPA_PLATFORM=offscreen ./build/debug/bin/peakemi_screenshots docs/images
//
// It runs headless, so it works in CI and over SSH.

#include <peakemi/core/Campaign.h>
#include <peakemi/core/LimitCatalogue.h>
#include <peakemi/core/MeasurementEngine.h>
#include <peakemi/core/RunConfiguration.h>
#include <peakemi/core/SetupLibrary.h>
#include <peakemi/drivers/SimulatedDriver.h>
#include <peakemi/reporting/CampaignReportRenderer.h>
#include <peakemi/reporting/SetupSheetRenderer.h>
#include <peakemi/ui/AboutDialog.h>
#include <peakemi/ui/CampaignDock.h>
#include <peakemi/ui/CampaignPlot.h>
#include <peakemi/ui/InstrumentDock.h>
#include <peakemi/ui/LogDock.h>
#include <peakemi/ui/MainWindow.h>
#include <peakemi/ui/PainterPlotBackend.h>
#include <peakemi/ui/ResultsDock.h>
#include <peakemi/ui/RunConfigDock.h>
#include <peakemi/ui/SetupLibraryDialog.h>
#include <peakemi/ui/Theme.h>

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QFont>
#include <QFontDatabase>
#include <QPalette>
#include <QPixmap>
#include <QSettings>
#include <QStringList>
#include <QStyleHints>
#include <QTabBar>
#include <QTemporaryDir>
#include <QTextStream>

#include <algorithm>
#include <exception>
#include <functional>

namespace {

QTextStream& out()
{
    static QTextStream stream{stdout};
    return stream;
}

/// Spin the event loop until @p ready reports true or the timeout expires.
bool waitFor(const std::function<bool()>& ready, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (!ready()) {
        if (timer.elapsed() > timeoutMs) {
            return false;
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    return true;
}

bool save(const QPixmap& pixmap, const QString& path)
{
    if (pixmap.isNull() || !pixmap.save(path, "PNG")) {
        out() << "failed to write " << path << Qt::endl;
        return false;
    }
    out() << "wrote " << path << " (" << pixmap.width() << "x" << pixmap.height() << ")"
          << Qt::endl;
    return true;
}

/// A configuration that shows the tool at its most informative: a radiated
/// class B scan with an antenna factor, which the simulated bench fails.
peakemi::RunConfiguration demoConfiguration()
{
    peakemi::RunConfiguration config;
    config.span = peakemi::FrequencyRange{peakemi::megahertz(30), peakemi::gigahertz(1.0)};
    config.phase1Points = 8001;
    config.phase1Rbw = peakemi::kilohertz(120);
    config.refLevel = peakemi::decibel(80.0);
    config.peaks.marginThresholdDb = 6.0;
    config.peaks.minimumSpacing = peakemi::megahertz(8);
    config.peaks.maximumCount = 8;
    config.verificationDetector = peakemi::Detector::QuasiPeak;
    config.dwellTime = std::chrono::milliseconds{1000};
    if (const auto limit = peakemi::builtInLimitLine("CISPR 32 Class B radiated 10 m (QP)")) {
        config.limits = {*limit};
    }

    peakemi::CorrectionTable antenna;
    antenna.name = "Biconilog antenna factor";
    antenna.kind = peakemi::CorrectionKind::AntennaFactor;
    antenna.points = {{peakemi::megahertz(30), 17.6},
                      {peakemi::megahertz(100), 12.9},
                      {peakemi::megahertz(300), 17.8},
                      {peakemi::megahertz(700), 22.3},
                      {peakemi::gigahertz(1.0), 24.6}};

    peakemi::CorrectionTable cable;
    cable.name = "RG-223 5 m cable loss";
    cable.kind = peakemi::CorrectionKind::CableLoss;
    cable.points = {{peakemi::megahertz(30), 0.6}, {peakemi::gigahertz(1.0), 3.4}};

    config.corrections = {antenna, cable};
    return config;
}

} // namespace

int main(int argc, char* argv[])
try {
    QApplication application{argc, argv};

    // A throwaway settings scope, so the screenshots always show the default
    // dock layout rather than whatever the developer last dragged around.
    QCoreApplication::setOrganizationName(QStringLiteral("PeakEmiScreenshots"));
    QCoreApplication::setApplicationName(QStringLiteral("PeakEmiScreenshots"));
    QSettings{}.clear();

    peakemi::registerMetaTypes();
    peakemi::drivers::registerBuiltInDrivers();

    // "--dark" renders everything in the dark colour scheme, which is how the
    // legibility of the plot and the results table there gets checked rather
    // than assumed.
    QStringList arguments;
    for (int i = 1; i < argc; ++i) {
        arguments.append(QString::fromLocal8Bit(argv[i]));
    }
    const bool dark = arguments.removeAll(QStringLiteral("--dark")) > 0;
    // "--report <file>" also writes the demo campaign's PDF report there.
    QString reportPath;
    if (const auto at = arguments.indexOf(QStringLiteral("--report"));
        at >= 0 && at + 1 < arguments.size())
    {
        reportPath = arguments.at(at + 1);
        arguments.remove(at, 2);
    }
    // The offscreen platform has no system theme to follow, so the scheme is
    // forced: the same theme the application installs, in the colours asked for.
    if (dark) {
        QGuiApplication::styleHints()->setColorScheme(Qt::ColorScheme::Dark);
    }
    peakemi::ui::installTheme(application,
                              dark ? peakemi::ui::Scheme::Dark : peakemi::ui::Scheme::Light);
    // Offscreen on Windows takes the first font it finds in QT_QPA_FONTDIR;
    // the application itself gets Segoe UI there.
    if (QFontDatabase::hasFamily(QStringLiteral("Segoe UI"))) {
        QApplication::setFont(QFont{QStringLiteral("Segoe UI"), 9});
    }

    const QString directory =
        arguments.isEmpty() ? QStringLiteral("docs/images") : arguments.first();
    if (!QDir{}.mkpath(directory)) {
        out() << "cannot create " << directory << Qt::endl;
        return 1;
    }

    peakemi::ui::MainWindow window;
    window.resize(1500, 940);
    window.show();
    QCoreApplication::processEvents();

    auto* instruments = window.findChild<peakemi::ui::InstrumentDock*>();
    auto* config = window.findChild<peakemi::ui::RunConfigDock*>();
    auto* results = window.findChild<peakemi::ui::ResultsDock*>();
    auto* logDock = window.findChild<peakemi::ui::LogDock*>();
    auto* plot = window.findChild<peakemi::ui::PainterPlotBackend*>();
    auto* start = window.findChild<QAction*>(QStringLiteral("actionStartRun"));
    if (instruments == nullptr || config == nullptr || results == nullptr || logDock == nullptr ||
        plot == nullptr || start == nullptr)
    {
        out() << "main window is missing an expected child widget" << Qt::endl;
        return 1;
    }

    config->setConfiguration(demoConfiguration());

    // Connect the simulated analyzer the same way the instrument dock does.
    const peakemi::TransportDescriptor simulated{.kind = peakemi::TransportKind::Simulated,
                                                 .address = "simulated",
                                                 .port = 0,
                                                 .baudRate = 0,
                                                 .terminator = "\n",
                                                 .defaultTimeout = std::chrono::milliseconds{1000}};
    emit instruments->connectRequested(simulated, QString{});
    if (!waitFor([start] { return start->isEnabled(); }, 10000)) {
        out() << "the simulated instrument did not connect" << Qt::endl;
        return 1;
    }

    start->trigger();
    // The run disables the action for its duration and re-enables it at the end.
    if (!waitFor([start] { return !start->isEnabled(); }, 5000)) {
        out() << "the run did not start" << Qt::endl;
        return 1;
    }
    if (!waitFor([start, results] { return start->isEnabled() && !results->points().empty(); },
                 120000))
    {
        out() << "the run did not finish in time" << Qt::endl;
        return 1;
    }
    out() << "run finished with " << results->points().size() << " verified point(s)" << Qt::endl;

    const QString suffix = dark ? QStringLiteral("-dark") : QString{};
    bool ok = save(window.grab(),
                   directory + QStringLiteral("/main-window") + suffix + QStringLiteral(".png"));
    ok = save(plot->grab(),
              directory + QStringLiteral("/spectrum-plot") + suffix + QStringLiteral(".png")) &&
         ok;
    ok = save(results->grab(),
              directory + QStringLiteral("/results-table") + suffix + QStringLiteral(".png")) &&
         ok;

    // Raise the log tab to show the application log and the SCPI console.
    logDock->raise();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
    ok = save(logDock->grab(),
              directory + QStringLiteral("/log-console") + suffix + QStringLiteral(".png")) &&
         ok;

    // Grabbed rather than shown: the dialog is modal, so exec() would block.
    peakemi::ui::AboutDialog about{&window};
    about.show();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
    ok = save(about.grab(),
              directory + QStringLiteral("/about-dialog") + suffix + QStringLiteral(".png")) &&
         ok;
    about.close();

    // A campaign part-way through: the run above filed under the horizontal
    // radiated test, the conducted tests set aside with a reason.
    auto* campaignDock = window.findChild<peakemi::ui::CampaignDock*>();
    auto campaign = peakemi::campaignFromTemplate("cispr32-b", "demo");
    if (campaignDock == nullptr || !campaign) {
        out() << "the campaign dock or template is missing" << Qt::endl;
        return 1;
    }
    campaign->title = "Widget v2 – EN 55032 class B pre-scan";
    campaign->eutName = "Widget v2 controller board";
    campaign->eutSerial = "W2-0042";
    campaign->eutOperatingMode = "Display on, Ethernet link up, 12 V DC supply";
    campaign->location = "Bench 3, semi-anechoic room";
    campaign->testedBy =
        peakemi::Signoff{.name = "A. Tester", .at = std::chrono::system_clock::now()};
    for (auto index : {0, 1}) {
        (void)campaign->tests[static_cast<std::size_t>(index)].overrideWith(
            peakemi::TestVerdict::NotApplicable, "DC powered from a lab supply; no AC mains port.");
    }
    auto& horizontal = campaign->tests[2];
    horizontal.session = window.session();
    horizontal.comments = "Highest emission at 667 MHz, a clock harmonic. Spread-spectrum "
                          "clocking to be tried on the next revision.";
    horizontal.equipment = {
        {.role = "Spectrum analyzer",
         .model = "PeakEmi Simulated Analyzer",
         .serial = "SIM-0001",
         .calibrationDue = "2027-06-30"},
        {.role = "Antenna",
         .model = "Biconilog 30 MHz-1 GHz",
         .serial = "BL-1234",
         .calibrationDue = "2026-09-30"},
        {.role = "RF cable", .model = "RG-223, 5 m", .serial = "C-17", .calibrationDue = ""}};
    horizontal.testedBy =
        peakemi::Signoff{.name = "A. Tester", .at = std::chrono::system_clock::now()};

    // The bench the horizontal test was measured on, as a saved set-up: the
    // equipment above with the run's own antenna factor and cable loss.
    auto radiatedBench = peakemi::setupFrom("Radiated, 3 m bench",
                                            peakemi::TestKind::Radiated,
                                            horizontal.equipment,
                                            horizontal.session->config.corrections);
    radiatedBench.description = "Turntable 0.8 m above the ground plane, antenna mast at 3 m. "
                                "Cable routed under the floor channel.";
    radiatedBench.devices.back().notes = "Front input, VXI-11 on 192.168.1.20";
    std::rotate(radiatedBench.devices.begin(),
                radiatedBench.devices.begin() + 1,
                radiatedBench.devices.end()); // analyzer last: EUT to analyzer
    peakemi::applySetup(horizontal, radiatedBench);
    horizontal.config.corrections = horizontal.session->config.corrections;

    auto conductedBench =
        peakemi::TestSetup::createNew("Conducted bench, 50 uH LISN", peakemi::TestKind::Conducted);
    conductedBench.description = "EUT 0.4 m from the vertical reference plane, LISN bonded to "
                                 "the ground plane, 10 dB limiter on the analyzer input.";
    conductedBench.devices = {
        {.equipment = {.role = "LISN",
                       .model = "NNLK 8121",
                       .serial = "L-0007",
                       .calibrationDue = "2027-01-31"},
         .correction =
             peakemi::flatCorrection("NNLK 8121", peakemi::CorrectionKind::LisnFactor, 0.3),
         .notes = "Line selected by the start commands"},
        {.equipment = {.role = "Transient limiter",
                       .model = "TBFL1",
                       .serial = "T-0002",
                       .calibrationDue = "2026-06-30"},
         .correction = peakemi::flatCorrection("TBFL1", peakemi::CorrectionKind::Attenuator, 10.0),
         .notes = ""},
        {.equipment =
             {.role = "RF cable", .model = "RG-58, 2 m", .serial = "C-03", .calibrationDue = ""},
         .correction =
             peakemi::flatCorrection("RG-58 2 m", peakemi::CorrectionKind::CableLoss, 0.2),
         .notes = ""},
        {.equipment = {.role = "Spectrum analyzer",
                       .model = "R&S FSL6",
                       .serial = "100005",
                       .calibrationDue = "2027-03-01"},
         .correction = std::nullopt,
         .notes = ""}};

    // The library lives in a throwaway folder, so the screenshot never shows
    // or touches the developer's own set-ups.
    QTemporaryDir setupFolder;
    QSettings{}.setValue(QStringLiteral("setups/directory"), setupFolder.path());
    const peakemi::SetupLibrary library{setupFolder.path()};
    if (!setupFolder.isValid() || !library.save(radiatedBench) || !library.save(conductedBench)) {
        out() << "cannot write the demo set-ups" << Qt::endl;
        return 1;
    }

    campaignDock->setCampaign(std::move(*campaign));
    campaignDock->selectTest(2);
    campaignDock->raise();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 200);
    ok = save(window.grab(),
              directory + QStringLiteral("/test-plan-window") + suffix + QStringLiteral(".png")) &&
         ok;
    ok = save(campaignDock->grab(),
              directory + QStringLiteral("/test-plan") + suffix + QStringLiteral(".png")) &&
         ok;

    {
        peakemi::ui::SetupLibraryDialog setups{&window};
        setups.setApplyTarget(QStringLiteral("test 'Radiated emissions – horizontal'"));
        setups.show();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 200);
        ok = save(setups.grab(),
                  directory + QStringLiteral("/test-setups") + suffix + QStringLiteral(".png")) &&
             ok;
    }
    if (!reportPath.isEmpty()) {
        const auto sheetPath =
            QFileInfo{reportPath}.absolutePath() + QStringLiteral("/setup-sheet.pdf");
        const peakemi::reporting::SetupSheetRenderer sheet;
        if (sheet.render(conductedBench, peakemi::ReportTemplate{}, sheetPath)) {
            out() << "wrote " << sheetPath << Qt::endl;
        }
    }

    if (!reportPath.isEmpty()) {
        peakemi::reporting::CampaignReportRenderer renderer;
        renderer.setSpectrumRenderer(&peakemi::ui::renderTestSpectrum);
        if (auto status =
                renderer.render(*campaignDock->campaign(), peakemi::ReportTemplate{}, reportPath);
            !status)
        {
            out() << "campaign report failed: " << QString::fromStdString(status.error().message())
                  << Qt::endl;
            ok = false;
        } else {
            out() << "wrote " << reportPath << Qt::endl;
        }
    }

    return ok ? 0 : 1;
} catch (const std::exception& error) {
    out() << "screenshot generation failed: " << error.what() << Qt::endl;
    return 1;
}
