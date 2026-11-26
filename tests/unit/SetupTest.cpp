#include "TestSupport.h"

#include <peakemi/core/CampaignSerializer.h>
#include <peakemi/core/SetupLibrary.h>
#include <peakemi/core/TestSetup.h>
#include <peakemi/reporting/CampaignReportRenderer.h>
#include <peakemi/reporting/SetupSheetRenderer.h>

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include <chrono>
#include <cmath>
#include <limits>

using namespace peakemi;

namespace {

[[nodiscard]] TimePoint day(int year, unsigned month, unsigned dayOfMonth)
{
    return std::chrono::sys_days{std::chrono::year{year} / std::chrono::month{month} /
                                 std::chrono::day{dayOfMonth}} +
           std::chrono::hours{12};
}

[[nodiscard]] CorrectionTable antennaFactor()
{
    return CorrectionTable{.name = "Biconilog BL-1234",
                           .kind = CorrectionKind::AntennaFactor,
                           .enabled = true,
                           .points = {{megahertz(30), 17.6}, {gigahertz(1.0), 24.6}}};
}

/// A conducted bench: LISN, a 10 dB limiter, a cable, and the analyzer.
[[nodiscard]] TestSetup conductedBench()
{
    auto setup = TestSetup::createNew("Conducted bench A", TestKind::Conducted);
    setup.description = "EUT 0.4 m from the vertical plane.";
    setup.devices = {{.equipment = {.role = "LISN",
                                    .model = "NNLK 8121",
                                    .serial = "L-7",
                                    .calibrationDue = "2027-01-31"},
                      .correction = flatCorrection("NNLK 8121", CorrectionKind::LisnFactor, 0.3),
                      .notes = "Bonded to the ground plane"},
                     {.equipment = {.role = "Transient limiter",
                                    .model = "TBFL1",
                                    .serial = "T-2",
                                    .calibrationDue = "2026-06-30"},
                      .correction = flatCorrection("TBFL1", CorrectionKind::Attenuator, 10.0),
                      .notes = ""},
                     {.equipment = {.role = "RF cable",
                                    .model = "RG-58, 2 m",
                                    .serial = "",
                                    .calibrationDue = ""},
                      .correction = flatCorrection("RG-58", CorrectionKind::CableLoss, 0.2),
                      .notes = ""},
                     {.equipment = {.role = "Spectrum analyzer",
                                    .model = "R&S FSL6",
                                    .serial = "100005",
                                    .calibrationDue = "2027-03-01"},
                      .correction = std::nullopt,
                      .notes = "VXI-11"}};
    return setup;
}

} // namespace

class SetupTest : public QObject
{
    Q_OBJECT

private slots:
    void flatCorrectionCoversEveryAnalyzer();
    void chainSumsItsCorrections();
    void validationNamesTheBrokenDevice();
    void setupIsRecoveredFromATest();
    void setupIsAppliedToATest();
    void setupSurvivesASaveAndLoad();
    void libraryKeepsOneFilePerSetup();
    void sheetDocumentsTheChain();
    void campaignReportNamesTheSetupAndCorrections();
};

void SetupTest::flatCorrectionCoversEveryAnalyzer()
{
    const auto pad = flatCorrection("10 dB pad", CorrectionKind::Attenuator, 10.0);
    QVERIFY(pad.validate().has_value());
    QCOMPARE(pad.kind, CorrectionKind::Attenuator);
    QCOMPARE(pad.valueAt(kilohertz(9)), 10.0);
    QCOMPARE(pad.valueAt(megahertz(100)), 10.0);
    QCOMPARE(pad.valueAt(gigahertz(18.0)), 10.0);
    QCOMPARE(roleFor(CorrectionKind::AntennaFactor), std::string{"Antenna"});
    QCOMPARE(roleFor(CorrectionKind::AmplifierGain), std::string{"Preamplifier"});
}

void SetupTest::chainSumsItsCorrections()
{
    auto setup = conductedBench();
    QCOMPARE(setup.equipment().size(), std::size_t{4});
    QCOMPARE(setup.equipment().back().model, std::string{"R&S FSL6"});
    QCOMPARE(setup.corrections().size(), std::size_t{3});
    QCOMPARE(setup.totalCorrectionAt(megahertz(1)), 10.5);

    // A preamplifier's gain is subtracted.
    setup.devices.push_back(
        {.equipment = {.role = "Preamplifier"},
         .correction = flatCorrection("Preamp", CorrectionKind::AmplifierGain, 20.0)});
    QCOMPARE(setup.totalCorrectionAt(megahertz(1)), -9.5);

    QCOMPARE(setup.expiredCount(day(2026, 11, 26)), 1);
    QCOMPARE(setup.expiredCount(day(2026, 6, 1)), 0);
}

void SetupTest::validationNamesTheBrokenDevice()
{
    auto setup = conductedBench();
    QVERIFY(setup.validate().has_value());

    setup.devices[1].correction->points[0].second = std::numeric_limits<double>::quiet_NaN();
    const auto broken = setup.validate();
    QVERIFY(!broken.has_value());
    QVERIFY(broken.error().detail.find("Transient limiter") != std::string::npos);

    setup = conductedBench();
    setup.name = "  ";
    QVERIFY(!setup.validate().has_value());
}

void SetupTest::setupIsRecoveredFromATest()
{
    const std::vector<EquipmentItem> equipment{{.role = "Spectrum analyzer", .model = "R&S FSL6"},
                                               {.role = "Antenna", .model = "Biconilog"},
                                               {.role = "RF cable", .model = "RG-223"}};
    const std::vector<CorrectionTable> corrections{
        antennaFactor(),
        flatCorrection("RG-223 5 m", CorrectionKind::CableLoss, 2.1),
        flatCorrection("6 dB pad", CorrectionKind::Attenuator, 6.0)};

    const auto setup = setupFrom("Radiated 10 m", TestKind::Radiated, equipment, corrections);
    QCOMPARE(setup.name, std::string{"Radiated 10 m"});
    QCOMPARE(setup.kind, TestKind::Radiated);
    QCOMPARE(setup.devices.size(), std::size_t{4});
    QVERIFY(!setup.devices[0].correction.has_value());
    QCOMPARE(setup.devices[1].correction->kind, CorrectionKind::AntennaFactor);
    QCOMPARE(setup.devices[2].correction->kind, CorrectionKind::CableLoss);
    // No device claimed the pad, so it became one.
    QCOMPARE(setup.devices[3].equipment.role, std::string{"Attenuator"});
    QCOMPARE(setup.devices[3].equipment.model, std::string{"6 dB pad"});
    QCOMPARE(setup.corrections().size(), corrections.size());
}

void SetupTest::setupIsAppliedToATest()
{
    auto test = makeCampaignTest(TestKind::Conducted, "Conducted emissions – AC mains L1");
    const auto setup = conductedBench();
    applySetup(test, setup);
    QCOMPARE(test.equipment, setup.equipment());
    QCOMPARE(test.config.corrections, setup.corrections());
    QCOMPARE(test.setupName, setup.name);
    QCOMPARE(test.setupDescription, setup.description);

    // And the campaign file keeps which set-up it was.
    auto campaign = *campaignFromTemplate("cispr32-b", "test");
    campaign.tests.front() = test;
    const auto loaded = CampaignSerializer::fromJson(CampaignSerializer::toJson(campaign));
    QVERIFY(loaded.has_value());
    QCOMPARE(loaded->tests.front().setupName, setup.name);
    QCOMPARE(loaded->tests.front().setupDescription, setup.description);
    QCOMPARE(loaded->tests.front().config.corrections, setup.corrections());
}

void SetupTest::setupSurvivesASaveAndLoad()
{
    auto setup = conductedBench();
    setup.devices[0].correction = antennaFactor(); // a table, not a flat value
    setup.devices[0].correction->enabled = false;

    const auto loaded = TestSetupSerializer::fromJson(TestSetupSerializer::toJson(setup));
    const auto reason = test::errorText(loaded);
    QVERIFY2(loaded.has_value(), reason.constData());
    QCOMPARE(loaded->id, setup.id);
    QCOMPARE(loaded->name, setup.name);
    QCOMPARE(loaded->kind, setup.kind);
    QCOMPARE(loaded->description, setup.description);
    QCOMPARE(loaded->devices, setup.devices);

    auto json = TestSetupSerializer::toJson(setup);
    const auto marker = std::string{"\"schema_version\": 1"};
    json.replace(json.find(marker), marker.size(), "\"schema_version\": 7");
    const auto newer = TestSetupSerializer::fromJson(json);
    QVERIFY(!newer.has_value());
    QCOMPARE(newer.error().code, ErrorCode::SchemaVersionUnsupported);
}

void SetupTest::libraryKeepsOneFilePerSetup()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const SetupLibrary library{directory.filePath(QStringLiteral("setups"))};
    QVERIFY(library.load().setups.empty()); // the folder does not exist yet

    auto radiated = TestSetup::createNew("Radiated 3 m", TestKind::Radiated);
    radiated.devices.push_back({.equipment = {.role = "Antenna"}, .correction = antennaFactor()});
    const auto conducted = conductedBench();
    QVERIFY(library.save(radiated).has_value());
    QVERIFY(library.save(conducted).has_value());
    QVERIFY(QFile::exists(library.pathFor(radiated.id)));

    // A broken file is reported and does not hide the others.
    QFile broken{QDir{library.directory()}.filePath(QStringLiteral("broken.peakemi-setup.json"))};
    QVERIFY(broken.open(QIODevice::WriteOnly));
    broken.write("{ not json");
    broken.close();

    auto contents = library.load();
    QCOMPARE(contents.setups.size(), std::size_t{2});
    QCOMPARE(contents.setups[0].name, std::string{"Conducted bench A"}); // by name
    QCOMPARE(contents.setups[1].devices, radiated.devices);
    QCOMPARE(contents.problems.size(), std::size_t{1});

    // A set-up without a name is not written.
    auto nameless = TestSetup::createNew("", TestKind::Other);
    QVERIFY(!library.save(nameless).has_value());

    QVERIFY(library.remove(radiated.id).has_value());
    contents = library.load();
    QCOMPARE(contents.setups.size(), std::size_t{1});
    QVERIFY(library.remove("no-such-id").has_value());
}

void SetupTest::sheetDocumentsTheChain()
{
    reporting::SetupSheetRenderer renderer;
    renderer.setGeneratedAt(day(2026, 11, 26));
    const auto setup = conductedBench();
    const auto html = renderer.html(setup, ReportTemplate{});

    QVERIFY(html.contains(QStringLiteral("Conducted bench A")));
    QVERIFY(html.contains(QStringLiteral("NNLK 8121")));
    QVERIFY(html.contains(QStringLiteral("R&amp;S FSL6")));
    QVERIFY(html.contains(QStringLiteral("Bonded to the ground plane")));
    QVERIFY(html.contains(QStringLiteral("+10.0 dB, flat")));
    QVERIFY(html.contains(QStringLiteral("EXPIRED")));
    QVERIFY(html.contains(QStringLiteral("1 device(s) out of calibration")));
    QVERIFY(html.contains(QStringLiteral("Total correction")));
    QVERIFY(html.contains(QStringLiteral("+10.5 dB")));

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto path = directory.filePath(QStringLiteral("sheet.pdf"));
    const auto status = renderer.render(setup, ReportTemplate{}, path);
    const auto reason = test::errorText(status);
    QVERIFY2(status.has_value(), reason.constData());
    QFile file{path};
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.read(5), QByteArray{"%PDF-"});
}

void SetupTest::campaignReportNamesTheSetupAndCorrections()
{
    auto campaign = *campaignFromTemplate("cispr32-b", "test");
    applySetup(campaign.tests.front(), conductedBench());

    reporting::CampaignReportRenderer renderer;
    renderer.setGeneratedAt(day(2026, 11, 26));
    const auto html = renderer.html(campaign, ReportTemplate{});
    QVERIFY(html.contains(QStringLiteral("Conducted bench A")));
    QVERIFY(html.contains(QStringLiteral("EUT 0.4 m from the vertical plane.")));
    QVERIFY(html.contains(QStringLiteral("<h3>Corrections</h3>")));
    QVERIFY(html.contains(QStringLiteral("+10.0 dB, flat")));
    QVERIFY(html.contains(QStringLiteral("LISN factor")));
    // A test without corrections says so rather than leaving it unclear.
    QVERIFY(html.contains(QStringLiteral("levels are as the analyzer read them")));
}

QTEST_MAIN(SetupTest)
#include "SetupTest.moc"
