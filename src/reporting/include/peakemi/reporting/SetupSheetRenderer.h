#pragma once

#include <peakemi/core/CorrectionTable.h>
#include <peakemi/core/IReportRenderer.h>
#include <peakemi/core/TestSetup.h>

#include <QString>

namespace peakemi::reporting {

/// A one-page record of a test set-up, to print and keep with the bench: the
/// signal chain from the equipment under test to the analyzer, each device's
/// calibration and correction, and the chain's total correction.
class SetupSheetRenderer
{
public:
    /// The moment the sheet is dated, and against which calibration due
    /// dates are judged. Defaults to now.
    void setGeneratedAt(TimePoint at) { m_generatedAt = at; }

    [[nodiscard]] Status
    render(const TestSetup& setup, const ReportTemplate& reportTemplate, const QString& path) const;

    [[nodiscard]] QString html(const TestSetup& setup, const ReportTemplate& reportTemplate) const;

private:
    TimePoint m_generatedAt{};
};

/// "+10.0 dB, flat" or "+17.6 to +24.6 dB, 5 points": a correction in one line.
/// The values are as the table holds them; a gain is subtracted when applied.
[[nodiscard]] QString describeCorrection(const CorrectionTable& table);

/// The display name of a correction kind: "Antenna factor", "Cable loss", ...
[[nodiscard]] QString correctionKindName(CorrectionKind kind);

} // namespace peakemi::reporting
