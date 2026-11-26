#pragma once

#include <peakemi/core/Campaign.h>
#include <peakemi/core/IReportRenderer.h>

#include <QImage>
#include <QSize>
#include <QString>

#include <functional>

namespace peakemi::reporting {

/// One PDF for a whole test campaign, laid out the way a test lab reports.
///
/// A title block with the equipment under test and the overall result, a
/// summary table of every test with its measured and final verdict, the
/// sign-off, then a section per test: method, settings, spectrum, verified
/// points, equipment with calibration status, comments and setup photos. The
/// compliance disclaimer opens and closes it.
class CampaignReportRenderer
{
public:
    /// Draws a test's spectrum, with its ambient scan when it has one. Supplied
    /// by the UI, which owns the plot, so reporting stays independent of it.
    using SpectrumRenderer = std::function<QImage(const CampaignTest& test, QSize size)>;

    void setSpectrumRenderer(SpectrumRenderer renderer) { m_spectrum = std::move(renderer); }

    /// The moment the report states it was generated, and against which
    /// calibration due dates are judged. Defaults to now.
    void setGeneratedAt(TimePoint at) { m_generatedAt = at; }

    [[nodiscard]] Status render(const Campaign& campaign,
                                const ReportTemplate& reportTemplate,
                                const QString& path) const;

    /// The report before layout, as rich text. The images it refers to are
    /// added by render(); this is what tests read the content from.
    [[nodiscard]] QString html(const Campaign& campaign,
                               const ReportTemplate& reportTemplate) const;

private:
    [[nodiscard]] TimePoint generatedAt() const;

    SpectrumRenderer m_spectrum;
    TimePoint m_generatedAt{};
};

} // namespace peakemi::reporting
