#pragma once

#include <peakemi/core/Campaign.h>
#include <peakemi/core/CorrectionTable.h>
#include <peakemi/core/Error.h>
#include <peakemi/core/Time.h>

#include <optional>
#include <string>
#include <vector>

namespace peakemi {

/// One device in the measurement chain of a test set-up.
struct SetupDevice
{
    EquipmentItem equipment;
    /// What the device does to the reading -- an antenna factor, a cable or
    /// LISN loss, an attenuator, a preamplifier's gain -- if anything. It is
    /// part of the device so documenting the chain and correcting for it are
    /// one act: a 10 dB attenuator listed is a 10 dB attenuator corrected for.
    std::optional<CorrectionTable> correction;
    std::string notes; ///< "set to 10 dB", "port 2", "bonded to the ground plane"

    friend bool operator==(const SetupDevice&, const SetupDevice&) = default;
};

/// A test bench as it is built: the devices from the equipment under test to
/// the analyzer, with their calibration and their corrections.
struct TestSetup
{
    std::string id;
    std::string name; ///< "Conducted bench -- LISN, limiter, 10 dB pad"
    TestKind kind{TestKind::Other};
    /// Layout, distances, grounding: what someone rebuilding it needs to know.
    std::string description;
    /// In signal order, from the equipment under test to the analyzer.
    std::vector<SetupDevice> devices;
    TimePoint createdAt{};
    TimePoint modifiedAt{};

    /// The devices as an equipment list, in chain order.
    [[nodiscard]] std::vector<EquipmentItem> equipment() const;
    /// The corrections the chain applies, in chain order.
    [[nodiscard]] std::vector<CorrectionTable> corrections() const;
    /// Sum of the enabled corrections' contributions at @p frequency.
    [[nodiscard]] double totalCorrectionAt(Hertz frequency) const;
    /// How many devices had a lapsed calibration on @p asOf.
    [[nodiscard]] int expiredCount(TimePoint asOf) const;

    /// A name, and corrections that can be applied to a reading.
    [[nodiscard]] Status validate() const;

    [[nodiscard]] static TestSetup createNew(std::string name, TestKind kind);

    friend bool operator==(const TestSetup&, const TestSetup&) = default;
};

/// A frequency-independent correction, for a device described by one value:
/// an attenuator, a transient limiter, a preamplifier. It spans 9 kHz to
/// 40 GHz, which covers every analyzer PeakEmi drives.
[[nodiscard]] CorrectionTable flatCorrection(std::string name, CorrectionKind kind, double valueDb);

/// The device role a correction kind usually belongs to: "Antenna" for an
/// antenna factor, "LISN" for a LISN factor, and so on.
[[nodiscard]] std::string roleFor(CorrectionKind kind);

/// A set-up recovered from what a test already holds: its equipment list and
/// its corrections. A correction is attached to the first device of the
/// matching role that has none yet; a correction no device claims becomes a
/// device of its own.
[[nodiscard]] TestSetup setupFrom(std::string name,
                                  TestKind kind,
                                  const std::vector<EquipmentItem>& equipment,
                                  const std::vector<CorrectionTable>& corrections);

/// Put @p setup on @p test: its equipment list, its corrections in the run
/// configuration, and its name and description for the report.
void applySetup(CampaignTest& test, const TestSetup& setup);

} // namespace peakemi
