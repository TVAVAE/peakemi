#pragma once

#include <peakemi/core/Time.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace peakemi {

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

} // namespace peakemi
