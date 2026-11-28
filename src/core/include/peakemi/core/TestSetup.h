#pragma once

#include <peakemi/core/CorrectionTable.h>
#include <peakemi/core/Equipment.h>
#include <peakemi/core/Error.h>
#include <peakemi/core/Time.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace peakemi {

struct CampaignTest;

/// How the equipment under test is coupled to the measurement chain. The
/// method decides the blocks a set-up's diagram offers.
enum class SetupMethod : std::uint8_t
{
    Custom,         ///< a plain chain of devices, no template
    LisnAc,         ///< mains, isolation transformer, a 50 uH LISN, the EUT
    LisnDc,         ///< a DC supply and two 5 uH LISNs, one of them terminated
    CurrentProbe,   ///< an RF current probe clamped round the EUT's cable
    Antenna,        ///< the EUT radiating towards a measurement antenna
    TemCell,        ///< the EUT inside a TEM cell
    NearFieldProbe, ///< the EUT scanned with a near-field probe
};

[[nodiscard]] std::string_view setupMethodKey(SetupMethod method);
[[nodiscard]] std::optional<SetupMethod> setupMethodFromKey(std::string_view key);
/// "Conducted – AC mains LISN", "Radiated – antenna", ...
[[nodiscard]] std::string_view setupMethodName(SetupMethod method);
[[nodiscard]] std::vector<SetupMethod> setupMethods();
[[nodiscard]] TestKind kindOf(SetupMethod method);
/// The method a set-up of @p kind without one is taken to use.
[[nodiscard]] SetupMethod defaultMethodFor(TestKind kind);

/// Where a block sits in a set-up's diagram.
enum class SlotPlace : std::uint8_t
{
    Coupling, ///< what takes the emission off the EUT: LISN, probe, antenna, cell
    Parallel, ///< beside the coupling device, between the same neighbours
    Supply,   ///< on the power side of the coupling device, away from the EUT
    Chain,    ///< between the coupling device and the analyzer
    Analyzer,
};

/// A block a method's diagram offers, which a device of its role fills.
struct SetupSlot
{
    std::string role;    ///< the device role it holds: "LISN", "Attenuator", ...
    std::string caption; ///< what the diagram calls it: "50 µH LISN"
    SlotPlace place{SlotPlace::Chain};
    bool optional{false};   ///< can be switched off
    bool onByDefault{true}; ///< part of a new set-up of this method
};

/// The blocks of @p method's diagram: the coupling device first, then what is
/// beside it and on its supply side (nearest first), the chain, the analyzer.
[[nodiscard]] std::vector<SetupSlot> slotsFor(SetupMethod method);

/// A block of a set-up's diagram: a slot of its method and the device in it,
/// or a device no slot names, which joins the chain before the analyzer.
struct SetupBlock
{
    int slot{-1};   ///< into slotsFor(method); -1 for a device no slot names
    int device{-1}; ///< into devices; -1 for a slot no device fills
    SlotPlace place{SlotPlace::Chain};

    friend bool operator==(const SetupBlock&, const SetupBlock&) = default;
};

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
    /// Switched off, the device keeps its details for when it is switched
    /// on again, but is not in the equipment list and corrects nothing.
    bool inUse{true};

    friend bool operator==(const SetupDevice&, const SetupDevice&) = default;
};

/// A test bench as it is built: the devices from the equipment under test to
/// the analyzer, with their calibration and their corrections.
struct TestSetup
{
    std::string id;
    std::string name; ///< "Conducted bench -- LISN, limiter, 10 dB pad"
    TestKind kind{TestKind::Other};
    SetupMethod method{SetupMethod::Custom};
    /// Layout, distances, grounding: what someone rebuilding it needs to know.
    std::string description;
    /// In signal order, from the equipment under test to the analyzer.
    std::vector<SetupDevice> devices;
    TimePoint createdAt{};
    TimePoint modifiedAt{};

    /// The devices in use as an equipment list, in chain order.
    [[nodiscard]] std::vector<EquipmentItem> equipment() const;
    /// The corrections the devices in use apply, in chain order.
    [[nodiscard]] std::vector<CorrectionTable> corrections() const;
    /// Sum of the enabled corrections' contributions at @p frequency.
    [[nodiscard]] double totalCorrectionAt(Hertz frequency) const;
    /// How many devices in use had a lapsed calibration on @p asOf.
    [[nodiscard]] int expiredCount(TimePoint asOf) const;

    /// The diagram's blocks: each slot of the method with the device that
    /// fills it -- the first unclaimed device of its role, in list order --
    /// and the devices no slot names, before the analyzer.
    [[nodiscard]] std::vector<SetupBlock> blocks() const;
    /// Switch @p slot's block on or off. Off keeps the device and its details;
    /// on brings it back, or adds a device for the slot after the devices of
    /// the slots before it. Returns the slot's device, or -1 if it has none.
    int setSlotInUse(int slot, bool inUse);

    /// A name, and corrections that can be applied to a reading.
    [[nodiscard]] Status validate() const;

    [[nodiscard]] static TestSetup createNew(std::string name, TestKind kind);
    /// A new set-up of @p method with a device in each slot that is on by default.
    [[nodiscard]] static TestSetup createFor(std::string name, SetupMethod method);

    friend bool operator==(const TestSetup&, const TestSetup&) = default;
};

/// A frequency-independent correction, for a device described by one value:
/// an attenuator, a transient limiter, a preamplifier. It spans 9 kHz to
/// 40 GHz, which covers every analyzer PeakEmi drives.
[[nodiscard]] CorrectionTable flatCorrection(std::string name, CorrectionKind kind, double valueDb);

/// The device role a correction kind usually belongs to: "Antenna" for an
/// antenna factor, "LISN" for a LISN factor, and so on.
[[nodiscard]] std::string roleFor(CorrectionKind kind);

/// A new device of @p role, with the correction such a device usually has: 10 dB
/// for an attenuator or a transient limiter, 20 dB of gain for a preamplifier.
[[nodiscard]] SetupDevice newDeviceFor(std::string role);

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
