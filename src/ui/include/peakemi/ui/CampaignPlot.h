#pragma once

#include <peakemi/core/Campaign.h>
#include <peakemi/ui/IPlotBackend.h>

#include <QColor>
#include <QImage>
#include <QSize>

namespace peakemi::ui {

/// Grey that reads under the trace on a light and on a dark display.
[[nodiscard]] QColor ambientColour();

/// Draws a campaign test: its limit lines, its ambient scan under its
/// measurement, and its verified points. A test not yet measured shows its
/// limits over its frequency range, which is what is about to be measured.
void showTest(IPlotBackend& plot, const CampaignTest& test);

/// Off-screen render of that plot, for the campaign report.
[[nodiscard]] QImage renderTestSpectrum(const CampaignTest& test, QSize size);

} // namespace peakemi::ui
