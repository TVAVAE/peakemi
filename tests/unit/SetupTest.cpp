#include "TestSupport.h"

#include <peakemi/core/Campaign.h>
#include <peakemi/core/CampaignSerializer.h>
#include <peakemi/core/SessionSerializer.h>
#include <peakemi/core/SetupLibrary.h>
#include <peakemi/core/TestSetup.h>
#include <peakemi/reporting/CampaignReportRenderer.h>
#include <peakemi/reporting/SetupDiagram.h>
#include <peakemi/reporting/SetupSheetRenderer.h>

#include <QFile>
#include <QImage>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>
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

    void everyMethodHasACouplingDeviceAndAnAnalyzer();
    void devicesFillTheirMethodsBlocksByRole();
    void switchingABlockOnAddsItsDeviceInChainOrder();
    void aDeviceSwitchedOffCorrectsNothing();
    void methodAndSwitchesSurviveASaveAndLoad();
    void setupWithoutAMethodGetsTheUsualOne();
    void sessionKeepsACopyOfItsSetup();
    void diagramShowsWhatIsInUseOnPaper();
    void diagramOffersTheRestWhenEditing();
    void sheetDrawsTheDiagramAndListsOnlyWhatIsInUse();
};

namespace {

[[nodiscard]] int slotIndex(SetupMethod method, std::string_view role, SlotPlace place)
{
    const auto methodSlots = slotsFor(method);
    for (std::size_t i = 0; i < methodSlots.size(); ++i) {
        if (methodSlots[i].role == role && methodSlots[i].place == place) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

[[nodiscard]] std::vector<std::string> roles(const TestSetup& setup)
{
    std::vector<std::string> result;
    for (const auto& device : setup.devices) {
        result.push_back(device.equipment.role);
    }
    return result;
}

[[nodiscard]] int countFixed(const reporting::SetupDiagram& diagram,
                             reporting::SetupDiagram::Fixed fixed)
{
    return static_cast<int>(
        std::count_if(diagram.blocks().begin(), diagram.blocks().end(), [fixed](const auto& block) {
            return block.fixed == fixed;
        }));
}

} // namespace

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

void SetupTest::everyMethodHasACouplingDeviceAndAnAnalyzer()
{
    for (const auto method : setupMethods()) {
        QCOMPARE(setupMethodFromKey(setupMethodKey(method)), method);
        const auto methodSlots = slotsFor(method);
        QVERIFY(!methodSlots.empty());
        QCOMPARE(methodSlots.back().place, SlotPlace::Analyzer);
        QVERIFY(!methodSlots.back().optional);
        const auto couplings =
            std::count_if(methodSlots.begin(), methodSlots.end(), [](const auto& slot) {
                return slot.place == SlotPlace::Coupling;
            });
        QCOMPARE(couplings, method == SetupMethod::Custom ? 0 : 1);
    }
    QCOMPARE(kindOf(SetupMethod::LisnDc), TestKind::Conducted);
    QCOMPARE(kindOf(SetupMethod::TemCell), TestKind::Radiated);
    QCOMPARE(defaultMethodFor(TestKind::Radiated), SetupMethod::Antenna);
    QVERIFY(!setupMethodFromKey("ouija-board").has_value());

    // A new set-up has a device in each block that is on by default.
    const auto dc = TestSetup::createFor("DC bench", SetupMethod::LisnDc);
    QCOMPARE(dc.kind, TestKind::Conducted);
    QCOMPARE(roles(dc),
             (std::vector<std::string>{
                 "LISN", "LISN", "Power supply", "RF cable", "Spectrum analyzer"}));
}

void SetupTest::devicesFillTheirMethodsBlocksByRole()
{
    auto setup = TestSetup::createNew("DC bench", TestKind::Conducted);
    setup.method = SetupMethod::LisnDc;
    setup.devices = {{.equipment = {.role = "Spectrum analyzer", .model = "R&S FSL6"}},
                     {.equipment = {.role = "lisn", .model = "TBOH01 #1"}},
                     {.equipment = {.role = "Mast controller", .model = "MC-1"}},
                     {.equipment = {.role = "LISN", .model = "TBOH01 #2"}}};
    const auto blocks = setup.blocks();
    const auto at = [&blocks](int slot) {
        return std::find_if(
            blocks.begin(), blocks.end(), [slot](const auto& block) { return block.slot == slot; });
    };
    // Roles match whatever their case; the first LISN measures, the second
    // sits beside it.
    QCOMPARE(at(slotIndex(SetupMethod::LisnDc, "LISN", SlotPlace::Coupling))->device, 1);
    QCOMPARE(at(slotIndex(SetupMethod::LisnDc, "LISN", SlotPlace::Parallel))->device, 3);
    QCOMPARE(at(slotIndex(SetupMethod::LisnDc, "Power supply", SlotPlace::Supply))->device, -1);
    // A device no block names joins the chain just before the analyzer.
    QCOMPARE(blocks[blocks.size() - 2].slot, -1);
    QCOMPARE(blocks[blocks.size() - 2].device, 2);
    QCOMPARE(blocks.back().place, SlotPlace::Analyzer);
    QCOMPARE(blocks.back().device, 0);
}

void SetupTest::switchingABlockOnAddsItsDeviceInChainOrder()
{
    auto setup = TestSetup::createFor("AC bench", SetupMethod::LisnAc);
    QCOMPARE(roles(setup), (std::vector<std::string>{"LISN", "RF cable", "Spectrum analyzer"}));

    const int attenuator = slotIndex(SetupMethod::LisnAc, "Attenuator", SlotPlace::Chain);
    QCOMPARE(setup.setSlotInUse(attenuator, true), 1);
    QCOMPARE(roles(setup),
             (std::vector<std::string>{"LISN", "Attenuator", "RF cable", "Spectrum analyzer"}));
    // It comes with the correction an attenuator usually has.
    QVERIFY(setup.devices[1].correction.has_value());
    QCOMPARE(setup.totalCorrectionAt(megahertz(1)), 10.0);

    // Switched on again, the same device: nothing is added twice.
    QCOMPARE(setup.setSlotInUse(attenuator, true), 1);
    QCOMPARE(setup.devices.size(), std::size_t{4});
    // Switching an empty block off changes nothing.
    QCOMPARE(setup.setSlotInUse(
                 slotIndex(SetupMethod::LisnAc, "Isolation transformer", SlotPlace::Supply), false),
             -1);
    QCOMPARE(setup.devices.size(), std::size_t{4});
    QCOMPARE(setup.setSlotInUse(99, true), -1);
}

void SetupTest::aDeviceSwitchedOffCorrectsNothing()
{
    auto setup = conductedBench();
    setup.method = SetupMethod::LisnAc;
    const int limiter = slotIndex(SetupMethod::LisnAc, "Transient limiter", SlotPlace::Chain);
    QCOMPARE(setup.setSlotInUse(limiter, false), 1);

    // Its details are kept for when it is switched on again...
    QCOMPARE(setup.devices.size(), std::size_t{4});
    QCOMPARE(setup.devices[1].equipment.model, std::string{"TBFL1"});
    // ...but it is not in the equipment list, corrects nothing, and its
    // lapsed calibration no longer counts.
    QCOMPARE(setup.equipment().size(), std::size_t{3});
    QCOMPARE(setup.corrections().size(), std::size_t{2});
    QCOMPARE(setup.totalCorrectionAt(megahertz(1)), 0.5);
    QCOMPARE(setup.expiredCount(day(2026, 11, 26)), 0);

    QCOMPARE(setup.setSlotInUse(limiter, true), 1);
    QCOMPARE(setup.totalCorrectionAt(megahertz(1)), 10.5);
}

void SetupTest::methodAndSwitchesSurviveASaveAndLoad()
{
    auto setup = conductedBench();
    setup.method = SetupMethod::CurrentProbe;
    setup.devices[1].inUse = false;
    const auto loaded = TestSetupSerializer::fromJson(TestSetupSerializer::toJson(setup));
    const auto reason = test::errorText(loaded);
    QVERIFY2(loaded.has_value(), reason.constData());
    // Field by field: the file keeps timestamps to the second.
    QCOMPARE(loaded->method, SetupMethod::CurrentProbe);
    QCOMPARE(loaded->kind, setup.kind);
    QCOMPARE(loaded->devices, setup.devices);
    QVERIFY(!loaded->devices[1].inUse);
}

void SetupTest::setupWithoutAMethodGetsTheUsualOne()
{
    // A file from before set-ups had methods and switches.
    const auto loaded = TestSetupSerializer::fromJson(R"({
        "schema": "peakemi.setup", "schema_version": 1, "id": "old", "name": "Old radiated",
        "kind": "radiated",
        "devices": [{"role": "Antenna", "model": "BL-1234"},
                    {"role": "Spectrum analyzer", "model": "R&S FSL6"}]})");
    const auto reason = test::errorText(loaded);
    QVERIFY2(loaded.has_value(), reason.constData());
    QCOMPARE(loaded->method, SetupMethod::Antenna);
    QVERIFY(loaded->devices[0].inUse);
    QCOMPARE(loaded->blocks().front().device, 0);
}

void SetupTest::sessionKeepsACopyOfItsSetup()
{
    auto session = Session::createNew("test");
    session.setup = conductedBench();
    session.setup->devices[2].inUse = false;
    const auto loaded = SessionSerializer::fromJson(SessionSerializer::toJson(session));
    const auto reason = test::errorText(loaded);
    QVERIFY2(loaded.has_value(), reason.constData());
    QVERIFY(loaded->setup.has_value());
    QCOMPARE(loaded->setup->id, session.setup->id);
    QCOMPARE(loaded->setup->name, session.setup->name);
    QCOMPARE(loaded->setup->method, session.setup->method);
    QCOMPARE(loaded->setup->description, session.setup->description);
    QCOMPARE(loaded->setup->devices, session.setup->devices);

    // And a session without one stays without one.
    const auto plain = SessionSerializer::fromJson(SessionSerializer::toJson(Session{}));
    QVERIFY(plain.has_value());
    QVERIFY(!plain->setup.has_value());
}

void SetupTest::diagramShowsWhatIsInUseOnPaper()
{
    using Fixed = reporting::SetupDiagram::Fixed;
    auto setup = conductedBench();
    setup.method = SetupMethod::LisnAc;
    const reporting::SetupDiagram paper{setup, false};
    // EUT, LISN, mains, limiter, cable, analyzer, PC: the isolation
    // transformer and the attenuator are not in this set-up.
    QCOMPARE(paper.blocks().size(), std::size_t{7});
    QCOMPARE(countFixed(paper, Fixed::Eut), 1);
    QCOMPARE(countFixed(paper, Fixed::Mains), 1);
    QCOMPARE(countFixed(paper, Fixed::PeakEmi), 1);
    for (const auto& block : paper.blocks()) {
        QVERIFY(!paper.hasSwitch(block));
        QVERIFY(paper.size().width() >= block.rect.right());
    }
    // Every link joins two blocks that are drawn.
    for (const auto& link : paper.links()) {
        QVERIFY(link.from >= 0 && static_cast<std::size_t>(link.from) < paper.blocks().size());
        QVERIFY(link.to >= 0 && static_cast<std::size_t>(link.to) < paper.blocks().size());
    }
    const auto lisn = std::find_if(paper.blocks().begin(), paper.blocks().end(), [](auto& b) {
        return b.place == SlotPlace::Coupling;
    });
    QVERIFY(lisn != paper.blocks().end());
    QCOMPARE(lisn->device, 0);
    QCOMPARE(paper.blockAt(lisn->rect.center()), static_cast<int>(lisn - paper.blocks().begin()));
    QVERIFY(paper.describe(paper.blockAt(lisn->rect.center())).contains(QStringLiteral("L-7")));

    const QImage image = paper.toImage(2.0, reporting::DiagramColours::forPrint());
    QCOMPARE(image.size(), (paper.size() * 2.0).toSize());

    // Antennas and cells pick up a field; nothing is wired to them.
    const reporting::SetupDiagram antenna{TestSetup::createFor("R", SetupMethod::Antenna), false};
    QVERIFY(std::any_of(antenna.links().begin(), antenna.links().end(), [](const auto& link) {
        return link.kind == reporting::SetupDiagram::LinkKind::Field;
    }));
    QCOMPARE(countFixed(antenna, Fixed::Mains), 0);
}

void SetupTest::diagramOffersTheRestWhenEditing()
{
    auto setup = conductedBench();
    setup.method = SetupMethod::LisnAc;
    setup.devices[1].inUse = false;
    const reporting::SetupDiagram editing{setup, true};
    // All six blocks of the method, plus EUT, mains and PC.
    QCOMPARE(editing.blocks().size(), std::size_t{9});
    int switches = 0;
    for (const auto& block : editing.blocks()) {
        if (editing.hasSwitch(block)) {
            ++switches;
            QVERIFY(block.rect.contains(editing.switchRect(block)));
        }
        if (block.device == 1) {
            QVERIFY(!block.inUse);
        }
    }
    // Isolation transformer, limiter, attenuator, cable.
    QCOMPARE(switches, 4);
}

void SetupTest::sheetDrawsTheDiagramAndListsOnlyWhatIsInUse()
{
    auto setup = conductedBench();
    setup.method = SetupMethod::LisnAc;
    setup.devices[1].inUse = false;
    reporting::SetupSheetRenderer renderer;
    renderer.setGeneratedAt(day(2026, 11, 26));
    const auto html = renderer.html(setup, ReportTemplate{});
    QVERIFY(html.contains(QStringLiteral("<h2>Diagram</h2>")));
    QVERIFY(html.contains(QStringLiteral("peakemi-setup:diagram")));
    QVERIFY(html.contains(QStringLiteral("Conducted – AC mains LISN")));
    QVERIFY(!html.contains(QStringLiteral("TBFL1")));
    QVERIFY(!html.contains(QStringLiteral("EXPIRED")));
}

QTEST_MAIN(SetupTest)
#include "SetupTest.moc"
