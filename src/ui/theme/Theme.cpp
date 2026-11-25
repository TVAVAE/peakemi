#include <peakemi/ui/Theme.h>

#include <QApplication>
#include <QGuiApplication>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPixmap>
#include <QStyle>
#include <QStyleFactory>
#include <QStyleHints>
#include <QWidget>

#include <algorithm>
#include <array>
#include <utility>

namespace peakemi::ui {
namespace {

/// What installTheme() was told to use, if anything.
std::optional<Scheme>& forcedScheme()
{
    static std::optional<Scheme> forced;
    return forced;
}

/// @p colour blended towards @p base, for the tinted backgrounds of the status
/// pills: a translucent colour in a stylesheet would composite differently on
/// every surface it lands on.
[[nodiscard]] QColor tint(const QColor& colour, const QColor& base, double amount)
{
    const auto mix = [amount](int a, int b) {
        return static_cast<int>(a * amount + b * (1.0 - amount) + 0.5);
    };
    return QColor{mix(colour.red(), base.red()),
                  mix(colour.green(), base.green()),
                  mix(colour.blue(), base.blue())};
}

[[nodiscard]] Theme lightTheme()
{
    Theme theme;
    theme.scheme = Scheme::Light;
    theme.window = QColor{0xEE, 0xF1, 0xF5};
    theme.panel = QColor{0xF7, 0xF9, 0xFB};
    theme.header = QColor{0xE2, 0xE7, 0xEE};
    theme.input = QColor{0xFF, 0xFF, 0xFF};
    theme.border = QColor{0xC5, 0xCD, 0xD8};
    theme.hover = QColor{0xD6, 0xDD, 0xE7};
    theme.text = QColor{0x18, 0x21, 0x2D};
    theme.mutedText = QColor{0x5A, 0x66, 0x75};
    theme.accent = QColor{0x0B, 0x63, 0xCE};
    theme.accentHover = QColor{0x1D, 0x74, 0xE0};
    theme.accentPressed = QColor{0x09, 0x52, 0xAB};
    theme.accentText = QColor{0xFF, 0xFF, 0xFF};
    theme.pass = QColor{0x15, 0x80, 0x3D};
    theme.marginal = QColor{0xB4, 0x53, 0x09};
    theme.fail = QColor{0xC6, 0x28, 0x28};

    // A printed-plot look: white display, light graticule, a blue trace. White
    // around it too, because the report embeds this display on white paper.
    theme.display = DisplayColours{.surround = QColor{0xFF, 0xFF, 0xFF},
                                   .background = QColor{0xFF, 0xFF, 0xFF},
                                   .frame = QColor{0x8E, 0x99, 0xA8},
                                   .graticule = QColor{0xD5, 0xDB, 0xE3},
                                   .text = QColor{0x3A, 0x44, 0x52},
                                   .mutedText = QColor{0x6B, 0x75, 0x85},
                                   .barBackground = theme.header,
                                   .barText = theme.text,
                                   .trace = QColor{0x15, 0x65, 0xC0},
                                   .limit = QColor{0xD3, 0x2F, 0x2F},
                                   .verified = QColor{0x0B, 0x80, 0x43},
                                   .marker = QColor{0x6A, 0x1B, 0x9A},
                                   .markerFail = QColor{0xC6, 0x28, 0x28},
                                   .markerMarginal = QColor{0xB4, 0x53, 0x09},
                                   .cursor = QColor{0x5A, 0x66, 0x75}};
    return theme;
}

[[nodiscard]] Theme darkTheme()
{
    Theme theme;
    theme.scheme = Scheme::Dark;
    theme.window = QColor{0x17, 0x1A, 0x20};
    theme.panel = QColor{0x1D, 0x21, 0x29};
    theme.header = QColor{0x23, 0x28, 0x33};
    theme.input = QColor{0x12, 0x15, 0x1B};
    theme.border = QColor{0x2F, 0x35, 0x41};
    theme.hover = QColor{0x2A, 0x30, 0x3B};
    theme.text = QColor{0xE4, 0xE8, 0xEE};
    theme.mutedText = QColor{0x8C, 0x95, 0xA3};
    theme.accent = QColor{0x3B, 0x82, 0xF6};
    theme.accentHover = QColor{0x5B, 0x97, 0xF8};
    theme.accentPressed = QColor{0x25, 0x63, 0xEB};
    theme.accentText = QColor{0xFF, 0xFF, 0xFF};
    theme.pass = QColor{0x34, 0xD3, 0x99};
    theme.marginal = QColor{0xFB, 0xBF, 0x24};
    theme.fail = QColor{0xF8, 0x71, 0x71};

    // An analyzer screen: near-black display, dim graticule, a yellow trace
    // and a red limit line.
    theme.display = DisplayColours{.surround = theme.window,
                                   .background = QColor{0x05, 0x06, 0x08},
                                   .frame = QColor{0x3A, 0x42, 0x4F},
                                   .graticule = QColor{0x2C, 0x33, 0x3D},
                                   .text = QColor{0xAA, 0xB3, 0xC0},
                                   .mutedText = QColor{0x6F, 0x78, 0x86},
                                   .barBackground = QColor{0x1D, 0x24, 0x30},
                                   .barText = QColor{0xD7, 0xDE, 0xE8},
                                   .trace = QColor{0xFF, 0xD6, 0x0A},
                                   .limit = QColor{0xFF, 0x4D, 0x4F},
                                   .verified = QColor{0x34, 0xD3, 0x99},
                                   .marker = QColor{0x5A, 0xC8, 0xFA},
                                   .markerFail = QColor{0xFF, 0x6B, 0x6B},
                                   .markerMarginal = QColor{0xFB, 0xBF, 0x24},
                                   .cursor = QColor{0x9C, 0xA3, 0xAF}};
    return theme;
}

[[nodiscard]] QPalette paletteFor(const Theme& theme)
{
    QPalette palette;
    palette.setColor(QPalette::Window, theme.window);
    palette.setColor(QPalette::WindowText, theme.text);
    palette.setColor(QPalette::Base, theme.input);
    palette.setColor(QPalette::AlternateBase, theme.panel);
    palette.setColor(QPalette::Text, theme.text);
    palette.setColor(QPalette::PlaceholderText, theme.mutedText);
    palette.setColor(QPalette::Button, theme.header);
    palette.setColor(QPalette::ButtonText, theme.text);
    palette.setColor(QPalette::BrightText, theme.fail);
    palette.setColor(QPalette::ToolTipBase, theme.header);
    palette.setColor(QPalette::ToolTipText, theme.text);
    palette.setColor(QPalette::Highlight, theme.accent);
    palette.setColor(QPalette::HighlightedText, theme.accentText);
    palette.setColor(QPalette::Link,
                     theme.scheme == Scheme::Dark ? theme.accentHover : theme.accent);
    palette.setColor(QPalette::LinkVisited,
                     theme.scheme == Scheme::Dark ? theme.accentHover : theme.accentPressed);
    palette.setColor(QPalette::Light, theme.scheme == Scheme::Dark ? theme.hover : theme.input);
    palette.setColor(QPalette::Midlight, theme.hover);
    palette.setColor(QPalette::Mid, theme.border);
    palette.setColor(QPalette::Dark, theme.border.darker(130));
    palette.setColor(QPalette::Shadow, theme.border.darker(160));
    for (const auto role : {QPalette::Text, QPalette::WindowText, QPalette::ButtonText}) {
        palette.setColor(QPalette::Disabled, role, theme.mutedText);
    }
    palette.setColor(QPalette::Disabled, QPalette::Highlight, theme.border);
    return palette;
}

void applyScheme(QApplication& application, Scheme scheme)
{
    const Theme theme = themeFor(scheme);
    QApplication::setPalette(paletteFor(theme));
    application.setStyleSheet(styleSheetFor(theme));
}

/// The scheme the system asks for, falling back on the palette when the
/// platform does not say -- the offscreen platform, for one.
[[nodiscard]] Scheme systemScheme()
{
    switch (QGuiApplication::styleHints()->colorScheme()) {
        case Qt::ColorScheme::Dark:
            return Scheme::Dark;
        case Qt::ColorScheme::Light:
            return Scheme::Light;
        case Qt::ColorScheme::Unknown:
            break;
    }
    return QGuiApplication::palette().color(QPalette::Window).lightness() < 128 ? Scheme::Dark
                                                                                : Scheme::Light;
}

void paintIcon(QPainter& painter, ToolIcon icon, const QColor& colour, double size)
{
    const double s = size;
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(colour);

    switch (icon) {
        case ToolIcon::Start: {
            const QPolygonF triangle{{QPointF{0.30 * s, 0.18 * s},
                                      QPointF{0.30 * s, 0.82 * s},
                                      QPointF{0.82 * s, 0.50 * s}}};
            painter.drawPolygon(triangle);
            break;
        }
        case ToolIcon::Pause:
            painter.drawRoundedRect(
                QRectF{0.26 * s, 0.20 * s, 0.17 * s, 0.60 * s}, 0.03 * s, 0.03 * s);
            painter.drawRoundedRect(
                QRectF{0.57 * s, 0.20 * s, 0.17 * s, 0.60 * s}, 0.03 * s, 0.03 * s);
            break;
        case ToolIcon::Abort:
            painter.drawRoundedRect(
                QRectF{0.24 * s, 0.24 * s, 0.52 * s, 0.52 * s}, 0.06 * s, 0.06 * s);
            break;
        case ToolIcon::AutoScale: {
            // Four corner brackets around a small spectrum: "fit to the data".
            QPen pen{colour, std::max(1.0, s / 12.0)};
            pen.setCapStyle(Qt::RoundCap);
            pen.setJoinStyle(Qt::RoundJoin);
            painter.setPen(pen);
            painter.setBrush(Qt::NoBrush);
            const double a = 0.14 * s;
            const double b = 0.86 * s;
            const double l = 0.24 * s;
            for (const auto& corner :
                 std::array<std::pair<QPointF, QPointF>, 4>{{{QPointF{a, a}, QPointF{1, 1}},
                                                             {QPointF{b, a}, QPointF{-1, 1}},
                                                             {QPointF{a, b}, QPointF{1, -1}},
                                                             {QPointF{b, b}, QPointF{-1, -1}}}})
            {
                const auto [origin, direction] = corner;
                painter.drawLine(origin, origin + QPointF{direction.x() * l, 0.0});
                painter.drawLine(origin, origin + QPointF{0.0, direction.y() * l});
            }
            QPainterPath spectrum;
            spectrum.moveTo(0.30 * s, 0.64 * s);
            spectrum.lineTo(0.42 * s, 0.58 * s);
            spectrum.lineTo(0.50 * s, 0.34 * s);
            spectrum.lineTo(0.58 * s, 0.60 * s);
            spectrum.lineTo(0.70 * s, 0.54 * s);
            painter.drawPath(spectrum);
            break;
        }
        case ToolIcon::Report: {
            QPen pen{colour, std::max(1.0, s / 14.0)};
            pen.setJoinStyle(Qt::RoundJoin);
            painter.setPen(pen);
            painter.setBrush(Qt::NoBrush);
            QPainterPath page;
            page.moveTo(0.24 * s, 0.12 * s);
            page.lineTo(0.60 * s, 0.12 * s);
            page.lineTo(0.78 * s, 0.30 * s);
            page.lineTo(0.78 * s, 0.88 * s);
            page.lineTo(0.24 * s, 0.88 * s);
            page.closeSubpath();
            page.moveTo(0.60 * s, 0.12 * s);
            page.lineTo(0.60 * s, 0.30 * s);
            page.lineTo(0.78 * s, 0.30 * s);
            painter.drawPath(page);
            for (const double y : {0.48, 0.60, 0.72}) {
                painter.drawLine(QPointF{0.34 * s, y * s}, QPointF{0.68 * s, y * s});
            }
            break;
        }
    }
}

} // namespace

Theme themeFor(Scheme scheme)
{
    return scheme == Scheme::Dark ? darkTheme() : lightTheme();
}

Scheme currentScheme()
{
    if (const auto forced = forcedScheme()) {
        return *forced;
    }
    return systemScheme();
}

Theme currentTheme()
{
    return themeFor(currentScheme());
}

void installTheme(QApplication& application, std::optional<Scheme> forced)
{
    forcedScheme() = forced;
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    applyScheme(application, currentScheme());

    static bool following = false;
    if (!forced && !following) {
        following = true;
        QObject::connect(QGuiApplication::styleHints(),
                         &QStyleHints::colorSchemeChanged,
                         &application,
                         [&application](Qt::ColorScheme /*scheme*/) {
                             if (!forcedScheme()) {
                                 applyScheme(application, systemScheme());
                             }
                         });
    }
}

QString styleSheetFor(const Theme& theme)
{
    QString sheet = QStringLiteral(R"(
QMainWindow::separator { background: @border; width: 1px; height: 1px; }

QMenuBar { background: @header; color: @text; border-bottom: 1px solid @border; }
QMenuBar::item { padding: 4px 10px; background: transparent; }
QMenuBar::item:selected { background: @hover; border-radius: 4px; }
QMenu { background: @panel; color: @text; border: 1px solid @border; padding: 4px 0; }
QMenu::item { padding: 5px 28px 5px 24px; }
QMenu::item:selected { background: @accent; color: @accentText; }
QMenu::item:disabled { color: @muted; }
QMenu::separator { height: 1px; background: @border; margin: 4px 8px; }

QToolBar { background: @header; border: none; border-bottom: 1px solid @border; padding: 4px 6px; spacing: 4px; }
QToolBar::separator { background: @border; width: 1px; margin: 4px 6px; }
QToolButton { color: @text; background: transparent; border: 1px solid transparent; border-radius: 5px; padding: 4px 10px; }
QToolButton:hover { background: @hover; border-color: @border; }
QToolButton:pressed, QToolButton:checked { background: @border; }
QToolButton:disabled { color: @muted; }
QToolButton#primaryToolButton { background: @accent; color: @accentText; border-color: @accent; font-weight: 600; padding: 4px 14px; }
QToolButton#primaryToolButton:hover { background: @accentHover; border-color: @accentHover; }
QToolButton#primaryToolButton:pressed { background: @accentPressed; }
QToolButton#primaryToolButton:disabled { background: @header; color: @muted; border-color: @border; }

QDockWidget { color: @text; }
QDockWidget::title { background: @header; color: @text; padding: 6px 10px; border-bottom: 1px solid @border; border-left: 3px solid @accent; text-align: left; }

QGroupBox { background: @panel; border: 1px solid @border; border-radius: 6px; margin-top: 18px; padding: 10px 8px 6px 8px; }
QGroupBox::title { subcontrol-origin: margin; subcontrol-position: top left; left: 10px; top: 2px; padding: 0 4px; color: @accent; font-weight: 600; }

QPushButton { background: @header; color: @text; border: 1px solid @border; border-radius: 5px; padding: 5px 12px; }
QPushButton:hover { background: @hover; }
QPushButton:pressed { background: @border; }
QPushButton:disabled { color: @muted; background: @panel; }
QPushButton[primary="true"] { background: @accent; color: @accentText; border-color: @accent; font-weight: 600; }
QPushButton[primary="true"]:hover { background: @accentHover; border-color: @accentHover; }
QPushButton[primary="true"]:pressed { background: @accentPressed; }
QPushButton[primary="true"]:disabled { background: @header; color: @muted; border-color: @border; font-weight: normal; }

QHeaderView::section { background: @header; color: @muted; border: none; border-right: 1px solid @border; border-bottom: 1px solid @border; padding: 4px 8px; font-weight: 600; }
QTableView, QTreeView, QListView { background: @input; alternate-background-color: @panel; border: 1px solid @border; border-radius: 4px; gridline-color: @border; selection-background-color: @accent; selection-color: @accentText; }
QPlainTextEdit, QTextEdit { background: @input; border: 1px solid @border; border-radius: 4px; selection-background-color: @accent; selection-color: @accentText; }

QTabBar::tab { background: @header; color: @muted; border: 1px solid @border; padding: 5px 16px; margin: 2px 2px 2px 0; border-radius: 4px; }
QTabBar::tab:selected { background: @input; color: @text; border-color: @accent; }
QTabBar::tab:hover:!selected { background: @hover; color: @text; }

QStatusBar { background: @header; color: @muted; border-top: 1px solid @border; }
QStatusBar::item { border: none; }
QLabel#instrumentStatus { color: @muted; padding: 0 6px; }
QLabel#instrumentStatus[state="connected"] { color: @pass; font-weight: 600; }
QLabel#phaseStatus { background: @hover; color: @text; border-radius: 9px; padding: 1px 10px; margin: 2px 4px; }
QLabel#phaseStatus[phase="running"] { background: @accent; color: @accentText; }
QLabel#phaseStatus[phase="finished"] { background: @passTint; color: @pass; font-weight: 600; }
QLabel#phaseStatus[phase="failed"] { background: @failTint; color: @fail; font-weight: 600; }
QLabel#cursorStatus { color: @muted; padding: 0 8px; }

QProgressBar { background: @input; border: 1px solid @border; border-radius: 4px; text-align: center; color: @text; max-height: 14px; }
QProgressBar::chunk { background: @accent; border-radius: 3px; }

QToolTip { background: @header; color: @text; border: 1px solid @border; padding: 4px; }

QScrollBar:vertical { background: transparent; width: 11px; margin: 0; }
QScrollBar::handle:vertical { background: @border; border-radius: 4px; min-height: 28px; margin: 2px; }
QScrollBar::handle:vertical:hover { background: @muted; }
QScrollBar:horizontal { background: transparent; height: 11px; margin: 0; }
QScrollBar::handle:horizontal { background: @border; border-radius: 4px; min-width: 28px; margin: 2px; }
QScrollBar::handle:horizontal:hover { background: @muted; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: none; }
)");

    // Longest names first, so "@accentHover" is not read as "@accent" + "Hover".
    const std::array<std::pair<const char*, QColor>, 16> tokens{{
        {"@accentPressed", theme.accentPressed},
        {"@accentHover", theme.accentHover},
        {"@accentText", theme.accentText},
        {"@passTint", tint(theme.pass, theme.header, 0.22)},
        {"@failTint", tint(theme.fail, theme.header, 0.22)},
        {"@accent", theme.accent},
        {"@border", theme.border},
        {"@header", theme.header},
        {"@hover", theme.hover},
        {"@input", theme.input},
        {"@muted", theme.mutedText},
        {"@panel", theme.panel},
        {"@window", theme.window},
        {"@text", theme.text},
        {"@pass", theme.pass},
        {"@fail", theme.fail},
    }};
    for (const auto& [token, colour] : tokens) {
        sheet.replace(QLatin1String{token}, colour.name());
    }
    return sheet;
}

QIcon toolIcon(ToolIcon icon, const QColor& colour)
{
    QIcon result;
    for (const int size : {16, 20, 24, 32, 48}) {
        QPixmap pixmap{size, size};
        pixmap.fill(Qt::transparent);
        QPainter painter{&pixmap};
        paintIcon(painter, icon, colour, static_cast<double>(size));
        painter.end();
        result.addPixmap(pixmap);
    }
    return result;
}

void repolish(QWidget* widget)
{
    if (widget == nullptr) {
        return;
    }
    widget->style()->unpolish(widget);
    widget->style()->polish(widget);
    widget->update();
}

} // namespace peakemi::ui
