#include "TestSupport.h"

#include <peakemi/core/Campaign.h>
#include <peakemi/core/CampaignSerializer.h>
#include <peakemi/core/LimitCatalogue.h>
#include <peakemi/reporting/CampaignReportRenderer.h>

#include <QBuffer>
#include <QFile>
#include <QImage>
#include <QTemporaryDir>
#include <QTest>

#include <chrono>

using namespace peakemi;

namespace {

[[nodiscard]] TimePoint day(int year, unsigned month, unsigned dayOfMonth)
{
    return std::chrono::sys_days{std::chrono::year{year} / std::chrono::month{month} /
                                 std::chrono::day{dayOfMonth}} +
           std::chrono::hours{12};
}

[[nodiscard]] MeasurementPoint point(double frequencyMHz, double marginDb, Verdict verdict)
{
    MeasurementPoint result;
    result.frequency = megahertz(frequencyMHz);
    result.correctedAmplitude = 40.0 - marginDb;
    result.limitValue = 40.0;
    result.marginDb = marginDb;
    result.limitName = "CISPR 32 Class B radiated 10 m (QP)";
    result.verdict = verdict;
    return result;
}

/// A session that measured something: one short trace and @p results.
[[nodiscard]] Session measured(std::vector<MeasurementPoint> results)
{
    Session session = Session::createNew("test");
    if (auto limit = builtInLimitLine("CISPR 32 Class B radiated 10 m (QP)")) {
        session.config.limits = {*limit};
    }
    Trace trace;
    trace.axis = FrequencyAxis::linear(FrequencyRange{megahertz(30), gigahertz(1.0)}, 4);
    trace.amplitudes = {20.0, 25.0, 31.0, 22.0};
    trace.label = "Phase 1 scan";
    session.traces.push_back(trace);
    session.results = std::move(results);
    return session;
}

[[nodiscard]] std::string tinyJpegBase64()
{
    QImage image{8, 6, QImage::Format_RGB32};
    image.fill(Qt::darkCyan);
    QByteArray bytes;
    QBuffer buffer{&bytes};
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "JPEG");
    return bytes.toBase64().toStdString();
}

} // namespace

class CampaignsTest : public QObject
{
    Q_OBJECT

private slots:
    void templatesFillTheFourStandardTests();
    void unknownTemplateIsRefused();
    void measuredVerdictFollowsTheSession();
    void overrideNeedsAReason();
    void overallVerdictPutsFailuresFirst();
    void lockingNeedsVerdictsAndSignatures();
    void calibrationExpiryIsJudgedByDay();
    void campaignSurvivesASaveAndLoad();
    void newerSchemaIsRefused();
    void reportCarriesEveryTestAndTheDisclaimer();
    void reportIsWrittenAsPdf();
};

void CampaignsTest::templatesFillTheFourStandardTests()
{
    const auto templates = campaignTemplates();
    QCOMPARE(templates.size(), std::size_t{4});

    for (const auto& entry : templates) {
        const auto campaign = campaignFromTemplate(entry.id, "test");
        const auto reason = test::errorText(campaign);
        QVERIFY2(campaign.has_value(), reason.constData());
        QCOMPARE(campaign->tests.size(), std::size_t{4});
        QCOMPARE(campaign->standard, entry.standard);
        QVERIFY(!campaign->id.empty());

        int conducted = 0;
        int radiated = 0;
        for (const auto& test : campaign->tests) {
            QVERIFY(!test.id.empty());
            QVERIFY(!test.config.limits.empty());
            QVERIFY(!test.procedure.empty());
            QVERIFY(!test.equipment.empty());
            // Verified at the frequency, as a quasi-peak reading needs.
            QCOMPARE(test.config.verificationSpan, Hertz{0});
            QVERIFY(test.config.validate().has_value());
            if (test.kind == TestKind::Conducted) {
                ++conducted;
                QCOMPARE(test.config.span.start, kilohertz(150));
                QCOMPARE(test.config.span.stop, megahertz(30));
            } else if (test.kind == TestKind::Radiated) {
                ++radiated;
                QCOMPARE(test.config.span.stop, gigahertz(1.0));
            }
            QCOMPARE(test.measuredVerdict(), TestVerdict::NotTested);
        }
        QCOMPARE(conducted, 2);
        QCOMPARE(radiated, 2);
    }

    // CISPR conducted limits come as a quasi-peak and an average line.
    const auto cispr = campaignFromTemplate("cispr32-b", "test");
    QCOMPARE(cispr->tests.front().config.limits.size(), std::size_t{2});
}

void CampaignsTest::unknownTemplateIsRefused()
{
    const auto campaign = campaignFromTemplate("mil-std-461", "test");
    QVERIFY(!campaign.has_value());
    QCOMPARE(campaign.error().code, ErrorCode::InvalidConfiguration);
}

void CampaignsTest::measuredVerdictFollowsTheSession()
{
    auto test = makeCampaignTest(TestKind::Radiated, "Radiated -- horizontal");
    QCOMPARE(test.measuredVerdict(), TestVerdict::NotTested);

    test.session = measured({point(100.0, 8.0, Verdict::Pass), point(200.0, -3.5, Verdict::Fail)});
    QCOMPARE(test.measuredVerdict(), TestVerdict::Fail);
    QCOMPARE(test.finalVerdict(), TestVerdict::Fail);
    QVERIFY(test.worstResult().has_value());
    QCOMPARE(test.worstResult()->marginDb, -3.5);

    test.session = measured({point(100.0, 4.0, Verdict::Marginal)});
    QCOMPARE(test.measuredVerdict(), TestVerdict::Marginal);

    // A scan against a limit that flagged nothing found nothing near it.
    test.session = measured({});
    QCOMPARE(test.measuredVerdict(), TestVerdict::Pass);

    // Without a limit line there was nothing to judge against.
    test.session->config.limits.clear();
    QCOMPARE(test.measuredVerdict(), TestVerdict::NotTested);
}

void CampaignsTest::overrideNeedsAReason()
{
    auto test = makeCampaignTest(TestKind::Radiated, "Radiated -- vertical");
    test.session = measured({point(98.0, -2.0, Verdict::Fail)});

    auto refused = test.overrideWith(TestVerdict::Pass, "   ");
    QVERIFY(!refused.has_value());
    QVERIFY(!test.isOverridden());
    QCOMPARE(test.finalVerdict(), TestVerdict::Fail);

    refused = test.overrideWith(TestVerdict::NotTested, "reason");
    QVERIFY(!refused.has_value());

    QVERIFY(test.overrideWith(TestVerdict::Pass, "98 MHz is an FM broadcast ambient").has_value());
    QVERIFY(test.isOverridden());
    QCOMPARE(test.measuredVerdict(), TestVerdict::Fail);
    QCOMPARE(test.finalVerdict(), TestVerdict::Pass);

    test.clearOverride();
    QCOMPARE(test.finalVerdict(), TestVerdict::Fail);
    QVERIFY(test.overrideReason.empty());
}

void CampaignsTest::overallVerdictPutsFailuresFirst()
{
    auto campaign = *campaignFromTemplate("cispr32-b", "test");
    QCOMPARE(campaign.overallVerdict(), TestVerdict::NotTested);
    QCOMPARE(campaign.completedCount(), 0);

    for (auto& test : campaign.tests) {
        test.session = measured({point(100.0, 9.0, Verdict::Pass)});
    }
    QCOMPARE(campaign.overallVerdict(), TestVerdict::Pass);
    QCOMPARE(campaign.completedCount(), 4);

    campaign.tests[1].session = measured({point(100.0, 3.0, Verdict::Marginal)});
    QCOMPARE(campaign.overallVerdict(), TestVerdict::Marginal);

    // An open test outweighs a marginal one: the campaign is not finished.
    campaign.tests[2].session.reset();
    QCOMPARE(campaign.overallVerdict(), TestVerdict::NotTested);

    // A failure outweighs everything.
    campaign.tests[3].session = measured({point(100.0, -1.0, Verdict::Fail)});
    QCOMPARE(campaign.overallVerdict(), TestVerdict::Fail);

    // Not applicable counts neither way.
    QVERIFY(campaign.tests[2]
                .overrideWith(TestVerdict::NotApplicable, "EUT is battery powered")
                .has_value());
    QVERIFY(campaign.tests[3].overrideWith(TestVerdict::Pass, "ambient").has_value());
    QCOMPARE(campaign.overallVerdict(), TestVerdict::Marginal);
}

void CampaignsTest::lockingNeedsVerdictsAndSignatures()
{
    auto campaign = *campaignFromTemplate("fcc15b-b", "test");
    QVERIFY(!campaign.canLock().has_value());

    for (auto& test : campaign.tests) {
        test.session = measured({point(100.0, 9.0, Verdict::Pass)});
    }
    auto refused = campaign.lock();
    QVERIFY(!refused.has_value());
    QVERIFY(refused.error().detail.find("tester") != std::string::npos);

    campaign.testedBy = Signoff{.name = "A. Tester", .at = day(2026, 11, 26)};
    refused = campaign.lock();
    QVERIFY(!refused.has_value());
    QVERIFY(refused.error().detail.find("reviewer") != std::string::npos);

    campaign.reviewedBy = Signoff{.name = "R. Viewer", .at = day(2026, 11, 27)};
    QVERIFY(campaign.lock().has_value());
    QVERIFY(campaign.locked);

    // Reopening withdraws the review, which no longer covers what may change.
    campaign.unlock();
    QVERIFY(!campaign.locked);
    QVERIFY(!campaign.reviewedBy.isSigned());
    QVERIFY(campaign.testedBy.isSigned());
}

void CampaignsTest::calibrationExpiryIsJudgedByDay()
{
    EquipmentItem antenna{
        .role = "Antenna", .model = "Biconilog", .serial = "1234", .calibrationDue = "2026-11-26"};
    QVERIFY(!antenna.calibrationExpired(day(2026, 11, 25)));
    QVERIFY(!antenna.calibrationExpired(day(2026, 11, 26)));
    QVERIFY(antenna.calibrationExpired(day(2026, 11, 27)));

    // No date, or one that cannot be read, is no evidence of expiry.
    antenna.calibrationDue.clear();
    QVERIFY(!antenna.calibrationExpired(day(2030, 1, 1)));
    antenna.calibrationDue = "26/11/2026";
    QVERIFY(!antenna.calibrationExpired(day(2030, 1, 1)));
    antenna.calibrationDue = "2026-02-30";
    QVERIFY(!antenna.calibrationExpired(day(2030, 1, 1)));
}

void CampaignsTest::campaignSurvivesASaveAndLoad()
{
    auto campaign = *campaignFromTemplate("cispr32-b", "0.3.0");
    campaign.eutName = "Widget v2";
    campaign.eutSerial = "W2-0042";
    campaign.customer = "VA Engineering";
    campaign.notes = "Mains 230 V 50 Hz.\nPre-scan only.";
    campaign.testedBy = Signoff{.name = "A. Tester", .at = day(2026, 11, 26)};

    auto& entry = campaign.tests[2];
    entry.session = measured({point(98.0, -2.0, Verdict::Fail), point(300.0, 7.5, Verdict::Pass)});
    entry.ambient = entry.session->traces.front();
    entry.ambient->label = "Ambient";
    QVERIFY(entry.overrideWith(TestVerdict::Pass, "98 MHz is FM broadcast; present in ambient")
                .has_value());
    entry.comments = "Cable dressed along the table edge.";
    entry.equipment.front().model = "R&S FSL6";
    entry.equipment.front().serial = "100005";
    entry.equipment.front().calibrationDue = "2027-03-01";
    entry.photos.push_back(SetupPhoto{
        .caption = "Front view", .mimeType = "image/jpeg", .base64Data = tinyJpegBase64()});
    entry.testedBy = Signoff{.name = "A. Tester", .at = day(2026, 11, 26)};

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto path = directory.filePath(QStringLiteral("widget.peakemi-campaign.json"));
    QVERIFY(CampaignSerializer::save(campaign, path).has_value());

    const auto loaded = CampaignSerializer::load(path);
    const auto reason = test::errorText(loaded);
    QVERIFY2(loaded.has_value(), reason.constData());
    QCOMPARE(loaded->eutName, campaign.eutName);
    QCOMPARE(loaded->notes, campaign.notes);
    QCOMPARE(loaded->testedBy, campaign.testedBy);
    QCOMPARE(loaded->tests.size(), campaign.tests.size());

    const auto& back = loaded->tests[2];
    QCOMPARE(back.id, entry.id);
    QCOMPARE(back.name, entry.name);
    QCOMPARE(back.kind, TestKind::Radiated);
    QCOMPARE(back.config, entry.config);
    QVERIFY(back.session.has_value());
    QCOMPARE(back.session->results, entry.session->results);
    QCOMPARE(back.session->traces.size(), std::size_t{1});
    QVERIFY(back.ambient.has_value());
    QCOMPARE(back.ambient->amplitudes, entry.ambient->amplitudes);
    QCOMPARE(back.measuredVerdict(), TestVerdict::Fail);
    QCOMPARE(back.finalVerdict(), TestVerdict::Pass);
    QCOMPARE(back.overrideReason, entry.overrideReason);
    QCOMPARE(back.comments, entry.comments);
    QCOMPARE(back.equipment, entry.equipment);
    QCOMPARE(back.photos, entry.photos);
    QCOMPARE(back.testedBy, entry.testedBy);
    QCOMPARE(loaded->overallVerdict(), campaign.overallVerdict());
}

void CampaignsTest::newerSchemaIsRefused()
{
    auto json = CampaignSerializer::toJson(*campaignFromTemplate("cispr32-a", "test"));
    const auto marker = std::string{"\"schema_version\": 1"};
    const auto at = json.find(marker);
    QVERIFY(at != std::string::npos);
    json.replace(at, marker.size(), "\"schema_version\": 99");
    const auto loaded = CampaignSerializer::fromJson(json);
    QVERIFY(!loaded.has_value());
    QCOMPARE(loaded.error().code, ErrorCode::SchemaVersionUnsupported);

    QCOMPARE(CampaignSerializer::fromJson("{\"schema\": \"peakemi.session\"}").error().code,
             ErrorCode::ParseFailure);

    // An override without its reason is not a valid record.
    auto campaign = *campaignFromTemplate("cispr32-a", "test");
    QVERIFY(campaign.tests[0].overrideWith(TestVerdict::Pass, "ambient").has_value());
    json = CampaignSerializer::toJson(campaign);
    const auto reason = std::string{"\"reason\": \"ambient\""};
    const auto found = json.find(reason);
    QVERIFY(found != std::string::npos);
    json.replace(found, reason.size(), "\"reason\": \"\"");
    QVERIFY(!CampaignSerializer::fromJson(json).has_value());
}

void CampaignsTest::reportCarriesEveryTestAndTheDisclaimer()
{
    auto campaign = *campaignFromTemplate("cispr32-b", "0.3.0");
    campaign.eutName = "Widget <v2>";
    campaign.tests[0].session = measured({point(0.5, 4.0, Verdict::Marginal)});
    campaign.tests[1].session = measured({point(0.6, -2.0, Verdict::Fail)});
    QVERIFY(
        campaign.tests[1].overrideWith(TestVerdict::Pass, "LISN not bonded; repeat").has_value());
    campaign.tests[2].equipment.front().calibrationDue = "2026-01-01";
    campaign.tests[2].comments = "Line one\nLine two";

    reporting::CampaignReportRenderer renderer;
    renderer.setGeneratedAt(day(2026, 11, 26));
    const auto html = renderer.html(campaign, ReportTemplate{});

    for (const auto& test : campaign.tests) {
        QVERIFY2(html.contains(QString::fromStdString(test.name).toHtmlEscaped()),
                 test.name.c_str());
    }
    QVERIFY(html.contains(QStringLiteral("Widget &lt;v2&gt;")));
    QVERIFY(html.contains(QStringLiteral("NOT TESTED"))); // overall: two tests still open
    QVERIFY(html.contains(QStringLiteral("LISN not bonded; repeat")));
    QVERIFY(html.contains(QStringLiteral("EXPIRED")));
    QVERIFY(html.contains(QStringLiteral("Line one<br>Line two")));
    QVERIFY(html.contains(QStringLiteral("PRE-COMPLIANCE DATA ONLY")));
    QVERIFY(html.contains(QStringLiteral("Overridden from FAIL")));
}

void CampaignsTest::reportIsWrittenAsPdf()
{
    auto campaign = *campaignFromTemplate("fcc15b-b", "0.3.0");
    campaign.tests[0].session = measured({point(0.5, 6.5, Verdict::Pass)});
    campaign.tests[0].photos.push_back(
        SetupPhoto{.caption = "Setup", .mimeType = "image/jpeg", .base64Data = tinyJpegBase64()});

    reporting::CampaignReportRenderer renderer;
    renderer.setSpectrumRenderer([](const peakemi::CampaignTest& /*test*/, QSize size) {
        QImage image{size, QImage::Format_RGB32};
        image.fill(Qt::white);
        return image;
    });

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto path = directory.filePath(QStringLiteral("campaign.pdf"));
    const auto status = renderer.render(campaign, ReportTemplate{}, path);
    const auto reason = test::errorText(status);
    QVERIFY2(status.has_value(), reason.constData());

    QFile file{path};
    QVERIFY(file.open(QIODevice::ReadOnly));
    const auto head = file.read(5);
    QCOMPARE(head, QByteArray{"%PDF-"});
    QVERIFY(file.size() > 4000);
}

QTEST_MAIN(CampaignsTest)
#include "CampaignTest.moc"
