#pragma once

#include <peakemi/core/LimitLine.h>
#include <peakemi/core/Session.h>
#include <peakemi/core/Trace.h>
#include <peakemi/ui/IPlotBackend.h>

#include <QColor>
#include <QImage>
#include <QPointF>
#include <QSize>
#include <QStringList>
#include <QVector>

namespace peakemi::ui {

/// A session's data as plot geometry.
///
/// One implementation for the live view, for a reopened session and for the
/// image the PDF report embeds, so a report never shows a differently drawn
/// spectrum from the one the operator approved on screen.

/// Series colours that hold up on both a light and a dark plot background. The
/// trace and the limit work on either; the verified-point green does not, and
/// is lightened where the background is dark.
[[nodiscard]] QColor traceColour();
[[nodiscard]] QColor limitColour();
[[nodiscard]] QColor verifiedPointColour();

[[nodiscard]] QVector<QPointF> tracePoints(const Trace& trace);

/// The settings a trace was measured with, as the plot's settings bar shows
/// them: "RBW 120 kHz", "Det QP", "Ref 80.0 dBuV", "Att auto", "Pts 8001".
[[nodiscard]] QStringList settingsSummary(const SweepParams& params, AmplitudeUnit unit);

/// The plot role of a limit line: average limits are drawn apart from the
/// quasi-peak and peak ones.
[[nodiscard]] PlotRole limitRole(const LimitLine& limit);

/// A limit line as a plot series, with the marginal zone of @p marginDb
/// shaded below it.
[[nodiscard]] PlotSeries limitSeries(const LimitLine& limit, FrequencyRange span, double marginDb);

/// Each verified point's verdict as a plot role, for a points series.
[[nodiscard]] QVector<PlotRole> verdictRoles(const std::vector<MeasurementPoint>& results);

/// The outcome to print on the plot: per limit line the worst verdict of the
/// points verified against it, and overall the worst of those. A scan that
/// flagged nothing passed every limit; no scan, or no limit, shows nothing.
[[nodiscard]] PlotVerdict plotVerdict(const std::vector<MeasurementPoint>& results,
                                      const std::vector<LimitLine>& limits,
                                      bool scanned);

/// Sampled densely enough that a logarithmic frequency axis stays smooth.
[[nodiscard]] QVector<QPointF> limitPoints(const LimitLine& limit, FrequencyRange span);

/// Draws everything a finished session holds: its last trace, the active limit
/// lines and the verified Phase 2 points.
void showSession(IPlotBackend& plot, const Session& session);

/// Off-screen render of that plot, for a report generated without a window.
///
/// Needs a QApplication -- the backend is a widget -- but no display: the
/// offscreen platform plugin is enough, which is what makes it usable from the
/// headless command line.
[[nodiscard]] QImage renderSessionSpectrum(const Session& session, QSize size);

} // namespace peakemi::ui
