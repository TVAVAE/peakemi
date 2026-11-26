#pragma once

#include <peakemi/core/Error.h>
#include <peakemi/core/MeasurementPoint.h>
#include <peakemi/core/RunConfiguration.h>
#include <peakemi/core/Session.h>
#include <peakemi/core/Time.h>
#include <peakemi/core/Trace.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace peakemi {

/// The outcome of one test in a campaign, and of the campaign as a whole.
enum class TestVerdict : std::uint8_t
{
    NotTested, ///< no measurement yet, or one no limit line could judge
    Pass,
    Marginal, ///< within the limit, but closer to it than the marginal threshold
    Fail,
    NotApplicable ///< set by the operator, with a reason, for a test that does not apply
};

[[nodiscard]] std::string_view testVerdictKey(TestVerdict verdict);
[[nodiscard]] std::optional<TestVerdict> testVerdictFromKey(std::string_view key);

/// What a test measures, which decides the equipment it is set up with.
enum class TestKind : std::uint8_t
{
    Conducted,
    Radiated,
    Other
};

[[nodiscard]] std::string_view testKindKey(TestKind kind);
[[nodiscard]] std::optional<TestKind> testKindFromKey(std::string_view key);

/// One instrument or accessory in a test setup, as a lab lists it in a report.
struct EquipmentItem
{
    std::string role;  ///< "Spectrum analyzer", "LISN", "Antenna", "Cable", ...
    std::string model; ///< "R&S FSL6"
    std::string serial;
    /// Calibration due date as "YYYY-MM-DD"; empty when the item is not calibrated.
    std::string calibrationDue;

    /// Whether the calibration had lapsed on @p asOf. False when no due date is
    /// set or it cannot be read: an unreadable date is not evidence of expiry.
    [[nodiscard]] bool calibrationExpired(TimePoint asOf) const;

    friend bool operator==(const EquipmentItem&, const EquipmentItem&) = default;
};

/// A photo of the test setup. The image is kept inside the campaign file, so a
/// campaign is one self-contained record rather than a file plus loose pictures.
struct SetupPhoto
{
    std::string caption;
    std::string mimeType{"image/jpeg"};
    std::string base64Data; ///< the encoded image bytes, base64

    friend bool operator==(const SetupPhoto&, const SetupPhoto&) = default;
};

/// A name and the moment it was put to something.
struct Signoff
{
    std::string name;
    TimePoint at{};

    [[nodiscard]] bool isSigned() const { return !name.empty(); }

    friend bool operator==(const Signoff&, const Signoff&) = default;
};

/// One test of the plan: what to measure, how, and what came of it.
struct CampaignTest
{
    std::string id;
    std::string name; ///< "Conducted emissions -- AC mains L1"
    TestKind kind{TestKind::Other};
    std::string port;      ///< "AC mains, line L1", "Enclosure, horizontal polarisation"
    std::string procedure; ///< setup and method, as the operator should carry it out
    RunConfiguration config;

    /// The measurement with the EUT operating, once there is one.
    std::optional<Session> session;
    /// A Phase 1 scan with the EUT switched off, to tell ambient signals apart.
    std::optional<Trace> ambient;

    /// The operator's verdict, which replaces the measured one. Only ever set
    /// together with a reason (see overrideWith()).
    std::optional<TestVerdict> overrideVerdict;
    std::string overrideReason;

    std::string comments;
    std::vector<EquipmentItem> equipment;
    std::vector<SetupPhoto> photos;
    Signoff testedBy;
    /// The saved set-up this test was built from, if any, as it read then.
    std::string setupName;
    std::string setupDescription;

    /// What the measurement says: NotTested without a session, or when no
    /// limit line covered what was measured.
    [[nodiscard]] TestVerdict measuredVerdict() const;
    /// The override when there is one, else the measured verdict.
    [[nodiscard]] TestVerdict finalVerdict() const;

    [[nodiscard]] bool isOverridden() const { return overrideVerdict.has_value(); }

    /// Worst (smallest) margin among the verified points, if any.
    [[nodiscard]] std::optional<MeasurementPoint> worstResult() const;

    /// Replace the measured verdict. Refused without a reason, and refused for
    /// NotTested, which is not a verdict an operator can reach.
    [[nodiscard]] Status overrideWith(TestVerdict verdict, std::string reason);
    void clearOverride();

    friend bool operator==(const CampaignTest&, const CampaignTest&) = default;
};

/// A test campaign: the plan for one equipment under test and its record.
struct Campaign
{
    std::string id;
    std::string title;
    std::string standard; ///< "CISPR 32 / EN 55032, class B"
    std::string templateId;

    std::string eutName;
    std::string eutSerial;
    std::string eutOperatingMode;
    std::string eutDescription;
    std::string customer;
    std::string location; ///< where the tests were made
    std::string notes;

    std::vector<CampaignTest> tests;

    Signoff testedBy;
    Signoff reviewedBy;
    /// A locked campaign is a closed record: the application refuses to edit it.
    bool locked{false};

    std::string applicationVersion;
    TimePoint createdAt{};
    TimePoint modifiedAt{};

    /// Fail if any test fails; NotTested while any test is still open;
    /// Marginal if any is marginal; Pass otherwise. NotApplicable tests do not
    /// count either way, and a plan with nothing but those is NotApplicable.
    [[nodiscard]] TestVerdict overallVerdict() const;
    /// Tests with a final verdict other than NotTested.
    [[nodiscard]] int completedCount() const;

    [[nodiscard]] CampaignTest* findTest(std::string_view testId);
    [[nodiscard]] const CampaignTest* findTest(std::string_view testId) const;

    /// Why the campaign cannot be locked yet, or nothing: every test has a
    /// verdict, and both the tester and the reviewer have signed.
    [[nodiscard]] Status canLock() const;
    [[nodiscard]] Status lock();
    /// Reopen for editing. The review no longer covers what may change, so it
    /// is withdrawn.
    void unlock();

    [[nodiscard]] static Campaign createNew(std::string applicationVersion);

    friend bool operator==(const Campaign&, const Campaign&) = default;
};

/// A fresh identifier for a campaign or a test.
[[nodiscard]] std::string newIdentifier();

/// A test plan a campaign can start from.
struct CampaignTemplate
{
    std::string id;
    std::string name;     ///< as offered to the operator
    std::string standard; ///< copied into the campaign
};

/// The built-in templates, in the order they are offered.
[[nodiscard]] std::vector<CampaignTemplate> campaignTemplates();

/// A campaign pre-filled with the tests @p templateId names: conducted
/// emissions on L1 and N, and radiated emissions with the antenna horizontal
/// and vertical, each with its limit lines, method and equipment roles.
[[nodiscard]] Result<Campaign> campaignFromTemplate(std::string_view templateId,
                                                    std::string applicationVersion);

/// An empty test of @p kind, set up the way a template sets one up.
[[nodiscard]] CampaignTest makeCampaignTest(TestKind kind, std::string name);

} // namespace peakemi
