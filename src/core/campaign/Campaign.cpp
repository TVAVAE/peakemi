#include <peakemi/core/Campaign.h>
#include <peakemi/core/LimitCatalogue.h>

#include <QUuid>

#include <array>
#include <charconv>
#include <chrono>
#include <utility>

namespace peakemi {
namespace {

struct VerdictKey
{
    TestVerdict verdict;
    std::string_view key;
};

constexpr std::array<VerdictKey, 5> VerdictKeys{{{TestVerdict::NotTested, "not-tested"},
                                                 {TestVerdict::Pass, "pass"},
                                                 {TestVerdict::Marginal, "marginal"},
                                                 {TestVerdict::Fail, "fail"},
                                                 {TestVerdict::NotApplicable, "not-applicable"}}};

/// "2027-03-01" as a calendar day, or nothing when it is not one.
[[nodiscard]] std::optional<std::chrono::sys_days> parseDate(std::string_view text)
{
    if (text.size() != 10 || text[4] != '-' || text[7] != '-') {
        return std::nullopt;
    }
    const auto field = [text](std::size_t offset, std::size_t length) -> std::optional<int> {
        int value = 0;
        const auto* first = text.data() + offset;
        const auto [end, error] = std::from_chars(first, first + length, value);
        if (error != std::errc{} || end != first + length) {
            return std::nullopt;
        }
        return value;
    };
    const auto year = field(0, 4);
    const auto month = field(5, 2);
    const auto day = field(8, 2);
    if (!year || !month || !day) {
        return std::nullopt;
    }
    const std::chrono::year_month_day date{std::chrono::year{*year},
                                           std::chrono::month{static_cast<unsigned>(*month)},
                                           std::chrono::day{static_cast<unsigned>(*day)}};
    if (!date.ok()) {
        return std::nullopt;
    }
    return std::chrono::sys_days{date};
}

/// What a template needs to know to fill its four tests.
struct TemplateSpec
{
    CampaignTemplate info;
    std::vector<std::string> conductedLimits;
    std::vector<std::string> radiatedLimits;
    int antennaDistanceMetres{10};
};

[[nodiscard]] std::vector<TemplateSpec> templateSpecs()
{
    return {
        {.info = {.id = "cispr32-b",
                  .name = "CISPR 32 / EN 55032 – Class B",
                  .standard = "CISPR 32:2015 / EN 55032:2015, class B"},
         .conductedLimits = {"CISPR 32 Class B conducted (QP)", "CISPR 32 Class B conducted (AV)"},
         .radiatedLimits = {"CISPR 32 Class B radiated 10 m (QP)"},
         .antennaDistanceMetres = 10},
        {.info = {.id = "cispr32-a",
                  .name = "CISPR 32 / EN 55032 – Class A",
                  .standard = "CISPR 32:2015 / EN 55032:2015, class A"},
         .conductedLimits = {"CISPR 32 Class A conducted (QP)", "CISPR 32 Class A conducted (AV)"},
         .radiatedLimits = {"CISPR 32 Class A radiated 10 m (QP)"},
         .antennaDistanceMetres = 10},
        {.info = {.id = "fcc15b-b",
                  .name = "FCC Part 15 Subpart B – Class B",
                  .standard = "47 CFR Part 15 Subpart B, class B"},
         .conductedLimits = {"FCC Part 15B Class B conducted (QP)"},
         .radiatedLimits = {"FCC Part 15B Class B radiated 3 m (QP)"},
         .antennaDistanceMetres = 3},
        {.info = {.id = "fcc15b-a",
                  .name = "FCC Part 15 Subpart B – Class A",
                  .standard = "47 CFR Part 15 Subpart B, class A"},
         .conductedLimits = {"FCC Part 15B Class A conducted (QP)"},
         .radiatedLimits = {"FCC Part 15B Class A radiated 10 m (QP)"},
         .antennaDistanceMetres = 10},
    };
}

[[nodiscard]] Result<std::vector<LimitLine>> limitsNamed(const std::vector<std::string>& names)
{
    std::vector<LimitLine> limits;
    for (const auto& name : names) {
        auto limit = builtInLimitLine(name);
        if (!limit) {
            return fail(ErrorCode::InvalidConfiguration, "no built-in limit line '" + name + "'");
        }
        limits.push_back(std::move(*limit));
    }
    return limits;
}

[[nodiscard]] std::string conductedProcedure(std::string_view line)
{
    return "Table-top EUT on a non-conductive table 0.8 m above the ground reference plane "
           "and 0.4 m from the vertical reference plane. Mains through a 50 ohm / 50 uH LISN "
           "bonded to the ground plane, LISN switched to " +
           std::string{line} +
           ". Enter the LISN and cable corrections in the run configuration before measuring.";
}

[[nodiscard]] std::string radiatedProcedure(std::string_view polarisation, int distance)
{
    return "EUT on the turntable 0.8 m above the ground plane, antenna at " +
           std::to_string(distance) + " m with " + std::string{polarisation} +
           " polarisation. For each emission rotate the turntable through 360 degrees and "
           "scan the antenna height from 1 to 4 m to find the maximum before it is verified. "
           "Enter the antenna factor and cable loss in the run configuration before measuring.";
}

} // namespace

std::string_view testVerdictKey(TestVerdict verdict)
{
    for (const auto& entry : VerdictKeys) {
        if (entry.verdict == verdict) {
            return entry.key;
        }
    }
    return "not-tested";
}

std::optional<TestVerdict> testVerdictFromKey(std::string_view key)
{
    for (const auto& entry : VerdictKeys) {
        if (entry.key == key) {
            return entry.verdict;
        }
    }
    return std::nullopt;
}

std::string_view testKindKey(TestKind kind)
{
    switch (kind) {
        case TestKind::Conducted:
            return "conducted";
        case TestKind::Radiated:
            return "radiated";
        case TestKind::Other:
            break;
    }
    return "other";
}

std::optional<TestKind> testKindFromKey(std::string_view key)
{
    for (const auto kind : {TestKind::Conducted, TestKind::Radiated, TestKind::Other}) {
        if (testKindKey(kind) == key) {
            return kind;
        }
    }
    return std::nullopt;
}

bool EquipmentItem::calibrationExpired(TimePoint asOf) const
{
    const auto due = parseDate(calibrationDue);
    if (!due) {
        return false;
    }
    return std::chrono::floor<std::chrono::days>(asOf) > *due;
}

TestVerdict CampaignTest::measuredVerdict() const
{
    if (!session) {
        return TestVerdict::NotTested;
    }
    if (!session->results.empty()) {
        switch (session->overallVerdict()) {
            case Verdict::Fail:
                return TestVerdict::Fail;
            case Verdict::Marginal:
                return TestVerdict::Marginal;
            case Verdict::Pass:
                return TestVerdict::Pass;
            case Verdict::Unknown:
                break;
        }
        return TestVerdict::NotTested;
    }
    // A scan with a limit line but nothing to verify found no emission within
    // the peak threshold of the limit: that is a pass. Without a limit line,
    // or without a scan, there was nothing to judge against.
    if (!session->traces.empty() && !session->config.limits.empty()) {
        return TestVerdict::Pass;
    }
    return TestVerdict::NotTested;
}

TestVerdict CampaignTest::finalVerdict() const
{
    return overrideVerdict.value_or(measuredVerdict());
}

std::optional<MeasurementPoint> CampaignTest::worstResult() const
{
    if (!session) {
        return std::nullopt;
    }
    return session->worstResult();
}

Status CampaignTest::overrideWith(TestVerdict verdict, std::string reason)
{
    if (verdict == TestVerdict::NotTested) {
        return fail(ErrorCode::InvalidConfiguration, "'not tested' cannot be set as a verdict");
    }
    const auto first = reason.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return fail(ErrorCode::InvalidConfiguration,
                    "overriding a measured verdict needs a reason, for the report");
    }
    overrideVerdict = verdict;
    overrideReason = std::move(reason);
    return {};
}

void CampaignTest::clearOverride()
{
    overrideVerdict.reset();
    overrideReason.clear();
}

TestVerdict Campaign::overallVerdict() const
{
    if (tests.empty()) {
        return TestVerdict::NotTested;
    }
    bool open = false;
    bool marginal = false;
    bool passed = false;
    for (const auto& test : tests) {
        switch (test.finalVerdict()) {
            case TestVerdict::Fail:
                return TestVerdict::Fail;
            case TestVerdict::NotTested:
                open = true;
                break;
            case TestVerdict::Marginal:
                marginal = true;
                break;
            case TestVerdict::Pass:
                passed = true;
                break;
            case TestVerdict::NotApplicable:
                break;
        }
    }
    if (open) {
        return TestVerdict::NotTested;
    }
    if (marginal) {
        return TestVerdict::Marginal;
    }
    return passed ? TestVerdict::Pass : TestVerdict::NotApplicable;
}

int Campaign::completedCount() const
{
    int count = 0;
    for (const auto& test : tests) {
        if (test.finalVerdict() != TestVerdict::NotTested) {
            ++count;
        }
    }
    return count;
}

CampaignTest* Campaign::findTest(std::string_view testId)
{
    for (auto& test : tests) {
        if (test.id == testId) {
            return &test;
        }
    }
    return nullptr;
}

const CampaignTest* Campaign::findTest(std::string_view testId) const
{
    for (const auto& test : tests) {
        if (test.id == testId) {
            return &test;
        }
    }
    return nullptr;
}

Status Campaign::canLock() const
{
    if (tests.empty()) {
        return fail(ErrorCode::InvalidConfiguration, "the campaign has no tests");
    }
    for (const auto& test : tests) {
        if (test.finalVerdict() == TestVerdict::NotTested) {
            return fail(ErrorCode::InvalidConfiguration, "'" + test.name + "' has no verdict yet");
        }
    }
    if (!testedBy.isSigned()) {
        return fail(ErrorCode::InvalidConfiguration, "the campaign is not signed by the tester");
    }
    if (!reviewedBy.isSigned()) {
        return fail(ErrorCode::InvalidConfiguration, "the campaign is not signed by a reviewer");
    }
    return {};
}

Status Campaign::lock()
{
    if (auto status = canLock(); !status) {
        return status;
    }
    locked = true;
    return {};
}

void Campaign::unlock()
{
    locked = false;
    reviewedBy = Signoff{};
}

Campaign Campaign::createNew(std::string applicationVersion)
{
    Campaign campaign;
    campaign.id = newIdentifier();
    campaign.applicationVersion = std::move(applicationVersion);
    campaign.createdAt = std::chrono::system_clock::now();
    campaign.modifiedAt = campaign.createdAt;
    return campaign;
}

std::string newIdentifier()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
}

std::vector<CampaignTemplate> campaignTemplates()
{
    std::vector<CampaignTemplate> templates;
    for (const auto& spec : templateSpecs()) {
        templates.push_back(spec.info);
    }
    return templates;
}

CampaignTest makeCampaignTest(TestKind kind, std::string name)
{
    CampaignTest test;
    test.id = newIdentifier();
    test.name = std::move(name);
    test.kind = kind;
    // Verified in zero span: a quasi-peak reading needs its dwell spent at the
    // frequency, which a swept verification span divides across its points.
    test.config.verificationSpan = Hertz{0};
    test.config.verificationDetector = Detector::QuasiPeak;
    switch (kind) {
        case TestKind::Conducted:
            test.config.span = FrequencyRange{kilohertz(150), megahertz(30)};
            test.equipment = {
                {.role = "Spectrum analyzer"}, {.role = "LISN"}, {.role = "RF cable"}};
            break;
        case TestKind::Radiated:
            test.config.span = FrequencyRange{megahertz(30), gigahertz(1.0)};
            test.equipment = {
                {.role = "Spectrum analyzer"}, {.role = "Antenna"}, {.role = "RF cable"}};
            break;
        case TestKind::Other:
            test.equipment = {{.role = "Spectrum analyzer"}};
            break;
    }
    return test;
}

Result<Campaign> campaignFromTemplate(std::string_view templateId, std::string applicationVersion)
{
    for (const auto& spec : templateSpecs()) {
        if (spec.info.id != templateId) {
            continue;
        }
        auto conducted = limitsNamed(spec.conductedLimits);
        if (!conducted) {
            return std::unexpected(conducted.error());
        }
        auto radiated = limitsNamed(spec.radiatedLimits);
        if (!radiated) {
            return std::unexpected(radiated.error());
        }

        auto campaign = Campaign::createNew(std::move(applicationVersion));
        campaign.templateId = spec.info.id;
        campaign.standard = spec.info.standard;
        campaign.title = spec.info.name;

        for (const auto& [line, port] :
             {std::pair{"L1", "AC mains, line L1"}, std::pair{"N", "AC mains, neutral N"}})
        {
            auto test = makeCampaignTest(TestKind::Conducted,
                                         "Conducted emissions – AC mains " + std::string{line});
            test.port = port;
            test.procedure = conductedProcedure(line);
            test.config.limits = *conducted;
            campaign.tests.push_back(std::move(test));
        }
        for (const auto& polarisation : {"horizontal", "vertical"}) {
            auto test = makeCampaignTest(TestKind::Radiated,
                                         "Radiated emissions – " + std::string{polarisation});
            test.port = "Enclosure, " + std::string{polarisation} + " polarisation";
            test.procedure = radiatedProcedure(polarisation, spec.antennaDistanceMetres);
            test.config.limits = *radiated;
            campaign.tests.push_back(std::move(test));
        }
        return campaign;
    }
    return fail(ErrorCode::InvalidConfiguration,
                "no campaign template '" + std::string{templateId} + "'");
}

} // namespace peakemi
