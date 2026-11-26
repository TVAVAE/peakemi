#pragma once

#include <QColor>
#include <QIcon>
#include <QString>

#include <cstdint>
#include <optional>

class QApplication;
class QWidget;

namespace peakemi::ui {

/// The two colour schemes the interface is drawn in.
enum class Scheme : std::uint8_t
{
    Light,
    Dark
};

/// The measurement display: the spectrum plot and the image the report embeds.
///
/// Styled after the screen of a bench spectrum analyzer -- a display inset in
/// the window, a dotted graticule, a settings bar along the top -- so that what
/// the operator reads off PeakEmi looks like what they read off the instrument.
struct DisplayColours
{
    QColor surround;   ///< the widget around the display
    QColor background; ///< inside the graticule
    QColor frame;
    QColor graticule;
    QColor text;      ///< axis labels
    QColor mutedText; ///< units, start and stop
    QColor barBackground;
    QColor barText;
    QColor trace;
    QColor limit;
    QColor limitAverage;
    QColor verified;
    QColor marker;
    QColor markerFail;
    QColor markerMarginal;
    QColor cursor;
};

/// Every colour the interface uses, for one scheme.
struct Theme
{
    Scheme scheme{Scheme::Light};
    QColor window;
    QColor panel;
    QColor header; ///< dock titles, toolbar, table headers, status bar
    QColor input;
    QColor border;
    QColor hover;
    QColor text;
    QColor mutedText;
    QColor accent;
    QColor accentHover;
    QColor accentPressed;
    QColor accentText;
    QColor pass;
    QColor marginal;
    QColor fail;
    DisplayColours display;
};

[[nodiscard]] Theme themeFor(Scheme scheme);

/// The scheme the interface is drawn in now: the one installTheme() was told to
/// use, or else the one Windows (or the desktop) asks for.
[[nodiscard]] Scheme currentScheme();

[[nodiscard]] Theme currentTheme();

/// Fusion style, palette and stylesheet for @p forced, or for the system colour
/// scheme when nothing is forced -- in which case the interface follows the
/// system as it switches between light and dark.
///
/// Called by the composition roots (the application and the screenshot tool),
/// not by widgets, so tests and the headless runner keep the plain style.
void installTheme(QApplication& application, std::optional<Scheme> forced = std::nullopt);

/// The application stylesheet for @p theme.
[[nodiscard]] QString styleSheetFor(const Theme& theme);

/// The toolbar's icons, painted rather than loaded so they are sharp at any
/// scale and drawn in the theme's own colours.
enum class ToolIcon : std::uint8_t
{
    Start,
    Pause,
    Abort,
    AutoScale,
    Report
};

[[nodiscard]] QIcon toolIcon(ToolIcon icon, const QColor& colour);

/// Re-evaluate a widget's stylesheet after one of its dynamic properties, which
/// the stylesheet selects on, has changed.
void repolish(QWidget* widget);

} // namespace peakemi::ui
