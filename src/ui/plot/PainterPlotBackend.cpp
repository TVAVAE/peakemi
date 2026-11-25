#include <peakemi/ui/PainterPlotBackend.h>

#include <QFontMetricsF>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QSvgGenerator>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>

namespace peakemi::ui {
namespace {

constexpr double LeftMargin = 62.0;
constexpr double RightMargin = 16.0;
/// Room above the display for the amplitude unit.
constexpr double TopMargin = 18.0;
/// Room below it for the frequency labels and the start/span/stop line.
constexpr double BottomMargin = 58.0;
/// The settings bar along the top, drawn when there are settings to show.
constexpr double Padding = 8.0;
constexpr double BarHeight = 24.0;
constexpr double MinimumSpanHz = 1000.0;

/// @p colour at @p alpha, for the boxes drawn over the trace.
[[nodiscard]] QColor translucent(QColor colour, int alpha)
{
    colour.setAlpha(alpha);
    return colour;
}

[[nodiscard]] QString formatFrequency(double hertz)
{
    if (hertz >= 1e9) {
        return QStringLiteral("%1 GHz").arg(hertz / 1e9, 0, 'g', 4);
    }
    if (hertz >= 1e6) {
        return QStringLiteral("%1 MHz").arg(hertz / 1e6, 0, 'g', 4);
    }
    if (hertz >= 1e3) {
        return QStringLiteral("%1 kHz").arg(hertz / 1e3, 0, 'g', 4);
    }
    return QStringLiteral("%1 Hz").arg(hertz, 0, 'g', 4);
}

/// "Nice" tick step: 1, 2, 5 times a power of ten.
[[nodiscard]] double niceStep(double range, int targetTicks)
{
    if (range <= 0.0 || targetTicks <= 0) {
        return 1.0;
    }
    const double rough = range / targetTicks;
    const double magnitude = std::pow(10.0, std::floor(std::log10(rough)));
    const double normalised = rough / magnitude;
    if (normalised <= 1.5) {
        return magnitude;
    }
    if (normalised <= 3.0) {
        return 2.0 * magnitude;
    }
    if (normalised <= 7.0) {
        return 5.0 * magnitude;
    }
    return 10.0 * magnitude;
}

} // namespace

PainterPlotBackend::PainterPlotBackend(QWidget* parent) : QWidget{parent}
{
    setMinimumSize(320, 220);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setAutoFillBackground(true);
    setObjectName(QStringLiteral("spectrumPlot"));
    setAccessibleName(tr("Spectrum plot"));
}

PainterPlotBackend::~PainterPlotBackend() = default;

void PainterPlotBackend::setSeries(const PlotSeries& series)
{
    const auto existing =
        std::find_if(m_series.begin(), m_series.end(), [&](const PlotSeries& stored) {
            return stored.id == series.id;
        });
    if (existing != m_series.end()) {
        *existing = series;
    } else {
        m_series.push_back(series);
    }
    update();
}

void PainterPlotBackend::removeSeries(const QString& id)
{
    std::erase_if(m_series, [&](const PlotSeries& series) { return series.id == id; });
    update();
}

void PainterPlotBackend::clearSeries()
{
    m_series.clear();
    m_markers.clear();
    update();
}

void PainterPlotBackend::setSeriesVisible(const QString& id, bool visible)
{
    for (auto& series : m_series) {
        if (series.id == id) {
            series.visible = visible;
        }
    }
    update();
}

void PainterPlotBackend::setFrequencyRange(FrequencyRange range)
{
    m_frequencyStart = static_cast<double>(range.start.value());
    m_frequencyStop = static_cast<double>(range.stop.value());
    if (m_frequencyStop <= m_frequencyStart) {
        m_frequencyStop = m_frequencyStart + MinimumSpanHz;
    }
    emit viewChanged();
    update();
}

void PainterPlotBackend::setAmplitudeRange(double minimum, double maximum)
{
    if (maximum <= minimum) {
        maximum = minimum + 1.0;
    }
    m_amplitudeMinimum = minimum;
    m_amplitudeMaximum = maximum;
    emit viewChanged();
    update();
}

void PainterPlotBackend::setLogarithmicFrequency(bool logarithmic)
{
    m_logarithmic = logarithmic;
    if (m_logarithmic && m_frequencyStart <= 0.0) {
        m_frequencyStart = std::max(1.0, m_frequencyStop / 1e6);
    }
    update();
}

void PainterPlotBackend::setAmplitudeLabel(const QString& label)
{
    m_amplitudeLabel = label;
    update();
}

void PainterPlotBackend::autoScale()
{
    double minimumFrequency = std::numeric_limits<double>::max();
    double maximumFrequency = std::numeric_limits<double>::lowest();
    double minimumAmplitude = std::numeric_limits<double>::max();
    double maximumAmplitude = std::numeric_limits<double>::lowest();
    bool any = false;

    for (const auto& series : m_series) {
        if (!series.visible) {
            continue;
        }
        for (const auto& point : series.points) {
            if (!std::isfinite(point.x()) || !std::isfinite(point.y())) {
                continue;
            }
            any = true;
            minimumFrequency = std::min(minimumFrequency, point.x());
            maximumFrequency = std::max(maximumFrequency, point.x());
            minimumAmplitude = std::min(minimumAmplitude, point.y());
            maximumAmplitude = std::max(maximumAmplitude, point.y());
        }
    }
    if (!any) {
        return;
    }

    const double headroom = std::max(2.0, (maximumAmplitude - minimumAmplitude) * 0.1);
    m_frequencyStart = minimumFrequency;
    m_frequencyStop = std::max(maximumFrequency, minimumFrequency + MinimumSpanHz);
    m_amplitudeMinimum = minimumAmplitude - headroom;
    m_amplitudeMaximum = maximumAmplitude + headroom;
    emit viewChanged();
    update();
}

void PainterPlotBackend::setMarkers(const QVector<PlotMarker>& markers)
{
    m_markers = markers;
    update();
}

void PainterPlotBackend::setSettingsSummary(const QStringList& items)
{
    m_settings = items;
    update();
}

void PainterPlotBackend::setDisplayScheme(std::optional<Scheme> scheme)
{
    m_scheme = scheme;
    update();
}

DisplayColours PainterPlotBackend::colours() const
{
    return themeFor(m_scheme.value_or(currentScheme())).display;
}

QColor PainterPlotBackend::colourFor(PlotRole role, const QColor& fallback) const
{
    const auto display = colours();
    switch (role) {
        case PlotRole::Trace:
            return display.trace;
        case PlotRole::Limit:
            return display.limit;
        case PlotRole::Verified:
            return display.verified;
        case PlotRole::Marker:
            return display.marker;
        case PlotRole::MarkerFail:
            return display.markerFail;
        case PlotRole::MarkerMarginal:
            return display.markerMarginal;
        case PlotRole::Custom:
            break;
    }
    return fallback;
}

PainterPlotBackend::Layout PainterPlotBackend::layoutFor(QSize size) const
{
    const double top = TopMargin + (m_settings.isEmpty() ? 0.0 : Padding + BarHeight + 4.0);
    Layout layout;
    layout.plotArea = QRectF{LeftMargin,
                             top,
                             std::max(10.0, size.width() - LeftMargin - RightMargin),
                             std::max(10.0, size.height() - top - BottomMargin)};
    layout.frequencyStart = m_frequencyStart;
    layout.frequencyStop = m_frequencyStop;
    layout.amplitudeMinimum = m_amplitudeMinimum;
    layout.amplitudeMaximum = m_amplitudeMaximum;
    return layout;
}

double PainterPlotBackend::toX(const Layout& layout, double frequency) const
{
    const double left = layout.plotArea.left();
    const double width = layout.plotArea.width();
    if (m_logarithmic && layout.frequencyStart > 0.0 && frequency > 0.0) {
        const double start = std::log10(layout.frequencyStart);
        const double stop = std::log10(layout.frequencyStop);
        if (stop <= start) {
            return left;
        }
        return left + width * (std::log10(frequency) - start) / (stop - start);
    }
    const double span = layout.frequencyStop - layout.frequencyStart;
    if (span <= 0.0) {
        return left;
    }
    return left + width * (frequency - layout.frequencyStart) / span;
}

double PainterPlotBackend::toY(const Layout& layout, double amplitude) const
{
    const double span = layout.amplitudeMaximum - layout.amplitudeMinimum;
    if (span <= 0.0) {
        return layout.plotArea.bottom();
    }
    const double fraction = (amplitude - layout.amplitudeMinimum) / span;
    return layout.plotArea.bottom() - layout.plotArea.height() * fraction;
}

double PainterPlotBackend::fromX(const Layout& layout, double x) const
{
    const double fraction = (x - layout.plotArea.left()) / layout.plotArea.width();
    if (m_logarithmic && layout.frequencyStart > 0.0) {
        const double start = std::log10(layout.frequencyStart);
        const double stop = std::log10(layout.frequencyStop);
        return std::pow(10.0, start + fraction * (stop - start));
    }
    return layout.frequencyStart + fraction * (layout.frequencyStop - layout.frequencyStart);
}

double PainterPlotBackend::fromY(const Layout& layout, double y) const
{
    const double fraction = (layout.plotArea.bottom() - y) / layout.plotArea.height();
    return layout.amplitudeMinimum + fraction * (layout.amplitudeMaximum - layout.amplitudeMinimum);
}

void PainterPlotBackend::drawSettingsBar(QPainter& painter, QSize size) const
{
    if (m_settings.isEmpty()) {
        return;
    }
    const auto display = colours();
    const QRectF bar{Padding, Padding, size.width() - 2.0 * Padding, BarHeight};
    painter.setPen(Qt::NoPen);
    painter.setBrush(display.barBackground);
    painter.drawRoundedRect(bar, 4.0, 4.0);
    painter.setBrush(Qt::NoBrush);

    QFont valueFont = painter.font();
    valueFont.setBold(true);
    const QFontMetricsF keyMetrics{painter.font()};
    const QFontMetricsF valueMetrics{valueFont};
    const double baseline =
        bar.top() + (bar.height() + keyMetrics.ascent() - keyMetrics.descent()) / 2.0;

    // "RBW 120 kHz": the key muted, the value emphasised, as an analyzer
    // labels the settings above its display.
    double x = bar.left() + 10.0;
    for (int i = 0; i < m_settings.size(); ++i) {
        const QString& item = m_settings[i];
        const auto space = item.indexOf(QLatin1Char(' '));
        const QString key = space < 0 ? QString{} : item.left(space);
        const QString value = space < 0 ? item : item.mid(space + 1);
        const double width = (key.isEmpty() ? 0.0 : keyMetrics.horizontalAdvance(key) + 5.0) +
                             valueMetrics.horizontalAdvance(value);
        if (x + width > bar.right() - 6.0) {
            break;
        }
        if (i > 0) {
            painter.setPen(QPen{display.frame, 1.0});
            painter.drawLine(QPointF{x - 9.0, bar.top() + 6.0},
                             QPointF{x - 9.0, bar.bottom() - 6.0});
        }
        if (!key.isEmpty()) {
            painter.setPen(display.mutedText);
            painter.drawText(QPointF{x, baseline}, key);
            x += keyMetrics.horizontalAdvance(key) + 5.0;
        }
        painter.save();
        painter.setFont(valueFont);
        painter.setPen(display.barText);
        painter.drawText(QPointF{x, baseline}, value);
        painter.restore();
        x += valueMetrics.horizontalAdvance(value) + 18.0;
    }
}

void PainterPlotBackend::drawFooter(QPainter& painter, const Layout& layout) const
{
    const auto display = colours();
    const QFontMetricsF metrics{painter.font()};
    const double y = layout.plotArea.bottom() + 2.0 * metrics.height() + 10.0;
    const auto start = tr("Start %1").arg(formatFrequency(layout.frequencyStart));
    const auto stop = tr("Stop %1").arg(formatFrequency(layout.frequencyStop));
    const auto span =
        tr("Span %1").arg(formatFrequency(layout.frequencyStop - layout.frequencyStart));

    painter.setPen(display.mutedText);
    painter.drawText(QPointF{layout.plotArea.left(), y}, start);
    painter.drawText(
        QPointF{layout.plotArea.center().x() - metrics.horizontalAdvance(span) / 2.0, y}, span);
    painter.drawText(QPointF{layout.plotArea.right() - metrics.horizontalAdvance(stop), y}, stop);
}

void PainterPlotBackend::drawGrid(QPainter& painter, const Layout& layout) const
{
    const auto display = colours();
    const QColor gridColour = display.graticule;
    const QColor textColour = display.text;
    painter.setPen(QPen{display.frame, 1.0});
    painter.drawRect(layout.plotArea);

    const QFontMetricsF metrics{painter.font()};

    // Frequency axis.
    painter.setPen(QPen{gridColour, 1.0, Qt::DotLine});
    if (m_logarithmic && layout.frequencyStart > 0.0) {
        const int firstDecade = static_cast<int>(std::floor(std::log10(layout.frequencyStart)));
        const int lastDecade = static_cast<int>(std::ceil(std::log10(layout.frequencyStop)));
        for (int decade = firstDecade; decade <= lastDecade; ++decade) {
            const double base = std::pow(10.0, decade);
            for (int step = 1; step <= 9; ++step) {
                const double frequency = base * step;
                if (frequency < layout.frequencyStart || frequency > layout.frequencyStop) {
                    continue;
                }
                const double x = toX(layout, frequency);
                painter.setPen(QPen{gridColour, 1.0, step == 1 ? Qt::SolidLine : Qt::DotLine});
                painter.drawLine(QPointF{x, layout.plotArea.top()},
                                 QPointF{x, layout.plotArea.bottom()});
                if (step == 1 || step == 3) {
                    painter.setPen(textColour);
                    const auto label = formatFrequency(frequency);
                    painter.drawText(QPointF{x - metrics.horizontalAdvance(label) / 2.0,
                                             layout.plotArea.bottom() + metrics.height() + 2.0},
                                     label);
                }
            }
        }
    } else {
        const double step = niceStep(layout.frequencyStop - layout.frequencyStart, 8);
        const double first = std::ceil(layout.frequencyStart / step) * step;
        for (double frequency = first; frequency <= layout.frequencyStop; frequency += step) {
            const double x = toX(layout, frequency);
            painter.setPen(QPen{gridColour, 1.0, Qt::DotLine});
            painter.drawLine(QPointF{x, layout.plotArea.top()},
                             QPointF{x, layout.plotArea.bottom()});
            painter.setPen(textColour);
            const auto label = formatFrequency(frequency);
            painter.drawText(QPointF{x - metrics.horizontalAdvance(label) / 2.0,
                                     layout.plotArea.bottom() + metrics.height() + 2.0},
                             label);
        }
    }

    // Amplitude axis.
    const double amplitudeStep = niceStep(layout.amplitudeMaximum - layout.amplitudeMinimum, 8);
    const double firstAmplitude =
        std::ceil(layout.amplitudeMinimum / amplitudeStep) * amplitudeStep;
    for (double amplitude = firstAmplitude; amplitude <= layout.amplitudeMaximum;
         amplitude += amplitudeStep)
    {
        const double y = toY(layout, amplitude);
        painter.setPen(QPen{gridColour, 1.0, Qt::DotLine});
        painter.drawLine(QPointF{layout.plotArea.left(), y}, QPointF{layout.plotArea.right(), y});
        painter.setPen(textColour);
        const auto label = QString::number(amplitude, 'f', 0);
        painter.drawText(QPointF{layout.plotArea.left() - metrics.horizontalAdvance(label) - 6.0,
                                 y + metrics.height() / 3.0},
                         label);
    }

    painter.setPen(display.mutedText);
    painter.drawText(QPointF{6.0, layout.plotArea.top() - 4.0}, m_amplitudeLabel);
}

void PainterPlotBackend::drawSeries(QPainter& painter,
                                    const Layout& layout,
                                    const PlotSeries& series) const
{
    if (!series.visible || series.points.isEmpty()) {
        return;
    }

    const QColor colour = colourFor(series.role, series.colour);
    QPen pen{colour, series.width};
    if (series.style == PlotStyle::Dashed) {
        pen.setStyle(Qt::DashLine);
    }
    painter.setPen(pen);

    const int columns = static_cast<int>(std::lround(layout.plotArea.width()));
    if (columns <= 0) {
        return;
    }

    // One vertical min/max bar per pixel column keeps redraw cost proportional
    // to the widget width rather than to the trace length (NFR-PERF-2).
    if (series.points.size() > static_cast<qsizetype>(columns) * 2 &&
        series.style != PlotStyle::Points)
    {
        std::vector<double> minima(static_cast<std::size_t>(columns),
                                   std::numeric_limits<double>::max());
        std::vector<double> maxima(static_cast<std::size_t>(columns),
                                   std::numeric_limits<double>::lowest());
        for (const auto& point : series.points) {
            if (!std::isfinite(point.y())) {
                continue;
            }
            const double x = toX(layout, point.x());
            const int column = static_cast<int>(std::lround(x - layout.plotArea.left()));
            if (column < 0 || column >= columns) {
                continue;
            }
            const auto index = static_cast<std::size_t>(column);
            minima[index] = std::min(minima[index], point.y());
            maxima[index] = std::max(maxima[index], point.y());
        }

        QPainterPath path;
        bool open = false;
        for (int column = 0; column < columns; ++column) {
            const auto index = static_cast<std::size_t>(column);
            if (maxima[index] < minima[index]) {
                open = false;
                continue;
            }
            const double x = layout.plotArea.left() + column;
            const QPointF top{x, toY(layout, maxima[index])};
            const QPointF bottom{x, toY(layout, minima[index])};
            if (!open) {
                path.moveTo(bottom);
                open = true;
            } else {
                path.lineTo(bottom);
            }
            path.lineTo(top);
        }
        painter.setClipRect(layout.plotArea);
        painter.drawPath(path);
        painter.setClipping(false);
        return;
    }

    painter.setClipRect(layout.plotArea);
    if (series.style == PlotStyle::Points) {
        // A ring filled with the display colour, so it reads on top of the trace.
        painter.setPen(QPen{colour, 1.8});
        painter.setBrush(colours().background);
        for (const auto& point : series.points) {
            const QPointF position{toX(layout, point.x()), toY(layout, point.y())};
            painter.drawEllipse(position, 3.5, 3.5);
        }
        painter.setBrush(Qt::NoBrush);
    } else {
        QPainterPath path;
        bool started = false;
        for (const auto& point : series.points) {
            if (!std::isfinite(point.y())) {
                started = false;
                continue;
            }
            const QPointF position{toX(layout, point.x()), toY(layout, point.y())};
            if (!started) {
                path.moveTo(position);
                started = true;
            } else {
                path.lineTo(position);
            }
        }
        painter.drawPath(path);
    }
    painter.setClipping(false);
}

void PainterPlotBackend::drawMarkers(QPainter& painter, const Layout& layout) const
{
    const auto display = colours();
    const QFontMetricsF metrics{painter.font()};
    for (const auto& marker : m_markers) {
        const QPointF position{toX(layout, marker.position.x()), toY(layout, marker.position.y())};
        if (!layout.plotArea.contains(position)) {
            continue;
        }
        const QColor colour = colourFor(marker.role, marker.colour);
        painter.setPen(QPen{colour, marker.selected ? 2.0 : 1.2});
        painter.setBrush(marker.selected ? QBrush{colour} : QBrush{display.background});
        const QPolygonF flag{
            {position, position + QPointF{-5.0, -9.0}, position + QPointF{5.0, -9.0}}};
        painter.drawPolygon(flag);
        if (!marker.label.isEmpty()) {
            // A small boxed readout, so the label stays legible where it lands
            // on the trace or on a neighbouring marker.
            const double width = metrics.horizontalAdvance(marker.label);
            const QPointF text = position + QPointF{-width / 2.0, -14.0};
            const QRectF box{text.x() - 3.0,
                             text.y() - metrics.ascent() - 1.0,
                             width + 6.0,
                             metrics.height() + 2.0};
            painter.setPen(QPen{translucent(colour, 160), 1.0});
            painter.setBrush(translucent(display.background, 215));
            painter.drawRoundedRect(box, 3.0, 3.0);
            painter.setPen(colour);
            painter.drawText(text, marker.label);
        }
    }
    painter.setBrush(Qt::NoBrush);
}

void PainterPlotBackend::drawLegend(QPainter& painter, const Layout& layout) const
{
    const auto display = colours();
    const QFontMetricsF metrics{painter.font()};

    double widest = 0.0;
    int rows = 0;
    for (const auto& series : m_series) {
        if (series.visible && !series.label.isEmpty()) {
            widest = std::max(widest, metrics.horizontalAdvance(series.label));
            ++rows;
        }
    }
    if (rows == 0) {
        return;
    }

    const double rowHeight = metrics.height() + 3.0;
    const QRectF box{layout.plotArea.right() - widest - 44.0,
                     layout.plotArea.top() + 8.0,
                     widest + 36.0,
                     rows * rowHeight + 8.0};
    painter.setPen(QPen{display.frame, 1.0});
    painter.setBrush(translucent(display.background, 225));
    painter.drawRoundedRect(box, 4.0, 4.0);
    painter.setBrush(Qt::NoBrush);

    double y = box.top() + 4.0;
    for (const auto& series : m_series) {
        if (!series.visible || series.label.isEmpty()) {
            continue;
        }
        const double x = box.left() + 8.0;
        QPen pen{colourFor(series.role, series.colour), 2.0};
        if (series.style == PlotStyle::Dashed) {
            pen.setStyle(Qt::DashLine);
        }
        painter.setPen(pen);
        painter.drawLine(QPointF{x, y + rowHeight / 2.0}, QPointF{x + 16.0, y + rowHeight / 2.0});
        painter.setPen(display.text);
        painter.drawText(QPointF{x + 22.0, y + rowHeight * 0.75}, series.label);
        y += rowHeight;
    }
}

void PainterPlotBackend::drawCursor(QPainter& painter, const Layout& layout) const
{
    if (!m_cursor || !layout.plotArea.contains(*m_cursor)) {
        return;
    }
    const auto display = colours();
    painter.setPen(QPen{display.cursor, 1.0, Qt::DashLine});
    painter.drawLine(QPointF{m_cursor->x(), layout.plotArea.top()},
                     QPointF{m_cursor->x(), layout.plotArea.bottom()});
    painter.drawLine(QPointF{layout.plotArea.left(), m_cursor->y()},
                     QPointF{layout.plotArea.right(), m_cursor->y()});

    const auto label = QStringLiteral("%1   %2 %3")
                           .arg(formatFrequency(fromX(layout, m_cursor->x())),
                                QString::number(fromY(layout, m_cursor->y()), 'f', 1),
                                m_amplitudeLabel);
    const QFontMetricsF metrics{painter.font()};
    const QRectF box{layout.plotArea.left() + 8.0,
                     layout.plotArea.top() + 8.0,
                     metrics.horizontalAdvance(label) + 14.0,
                     metrics.height() + 8.0};
    painter.setPen(QPen{display.frame, 1.0});
    painter.setBrush(translucent(display.background, 225));
    painter.drawRoundedRect(box, 4.0, 4.0);
    painter.setBrush(Qt::NoBrush);
    painter.setPen(display.text);
    painter.drawText(QPointF{box.left() + 7.0, box.top() + 4.0 + metrics.ascent()}, label);
}

void PainterPlotBackend::render(QPainter& painter, QSize size) const
{
    const auto display = colours();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(QRectF{QPointF{0.0, 0.0}, QSizeF{size}}, display.surround);

    const Layout layout = layoutFor(size);
    drawSettingsBar(painter, size);
    painter.fillRect(layout.plotArea, display.background);
    drawGrid(painter, layout);
    for (const auto& series : m_series) {
        drawSeries(painter, layout, series);
    }
    drawMarkers(painter, layout);
    drawLegend(painter, layout);
    drawFooter(painter, layout);
}

void PainterPlotBackend::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter{this};
    render(painter, size());
    drawCursor(painter, layoutFor(size()));
}

void PainterPlotBackend::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    update();
}

QImage PainterPlotBackend::renderToImage(QSize size) const
{
    QImage image{size, QImage::Format_ARGB32_Premultiplied};
    image.fill(colours().surround);
    QPainter painter{&image};
    render(painter, size);
    return image;
}

bool PainterPlotBackend::exportSvg(const QString& path, QSize size) const
{
    QSvgGenerator generator;
    generator.setFileName(path);
    generator.setSize(size);
    generator.setViewBox(QRect{QPoint{0, 0}, size});
    generator.setTitle(tr("PeakEmi spectrum"));
    generator.setDescription(tr("EMI pre-compliance measurement trace"));

    QPainter painter;
    if (!painter.begin(&generator)) {
        return false;
    }
    render(painter, size);
    painter.end();
    return true;
}

void PainterPlotBackend::zoomFrequency(double factor, double anchorFrequency)
{
    if (m_logarithmic && m_frequencyStart > 0.0) {
        const double start = std::log10(m_frequencyStart);
        const double stop = std::log10(m_frequencyStop);
        const double anchor = std::log10(std::max(1.0, anchorFrequency));
        m_frequencyStart = std::pow(10.0, anchor + (start - anchor) * factor);
        m_frequencyStop = std::pow(10.0, anchor + (stop - anchor) * factor);
    } else {
        m_frequencyStart = anchorFrequency + (m_frequencyStart - anchorFrequency) * factor;
        m_frequencyStop = anchorFrequency + (m_frequencyStop - anchorFrequency) * factor;
    }
    if (m_frequencyStop - m_frequencyStart < MinimumSpanHz) {
        const double centre = (m_frequencyStart + m_frequencyStop) / 2.0;
        m_frequencyStart = centre - MinimumSpanHz / 2.0;
        m_frequencyStop = centre + MinimumSpanHz / 2.0;
    }
    m_frequencyStart = std::max(0.0, m_frequencyStart);
    emit viewChanged();
    update();
}

void PainterPlotBackend::wheelEvent(QWheelEvent* event)
{
    const Layout layout = layoutFor(size());
    const double steps = event->angleDelta().y() / 120.0;
    if (steps == 0.0) {
        QWidget::wheelEvent(event);
        return;
    }
    const double factor = std::pow(0.8, steps);

    if (event->modifiers().testFlag(Qt::ControlModifier)) {
        const double anchor = fromY(layout, event->position().y());
        m_amplitudeMinimum = anchor + (m_amplitudeMinimum - anchor) * factor;
        m_amplitudeMaximum = anchor + (m_amplitudeMaximum - anchor) * factor;
        emit viewChanged();
        update();
    } else {
        zoomFrequency(factor, fromX(layout, event->position().x()));
    }
    event->accept();
}

void PainterPlotBackend::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }

    const Layout layout = layoutFor(size());
    for (int i = 0; i < m_markers.size(); ++i) {
        const QPointF position{toX(layout, m_markers[i].position.x()),
                               toY(layout, m_markers[i].position.y())};
        if (QLineF{position, event->position()}.length() < 10.0) {
            emit markerActivated(i);
            return;
        }
    }

    m_dragOrigin = event->pos();
    m_dragStartFrequency = m_frequencyStart;
    m_dragStopFrequency = m_frequencyStop;
    setCursor(Qt::ClosedHandCursor);
}

void PainterPlotBackend::mouseMoveEvent(QMouseEvent* event)
{
    const Layout layout = layoutFor(size());
    m_cursor = event->position();
    emit cursorMoved(fromX(layout, event->position().x()), fromY(layout, event->position().y()));

    if (m_dragOrigin) {
        const double deltaPixels = event->position().x() - m_dragOrigin->x();
        if (m_logarithmic && m_dragStartFrequency > 0.0) {
            const double decades =
                (std::log10(m_dragStopFrequency) - std::log10(m_dragStartFrequency)) * deltaPixels /
                layout.plotArea.width();
            m_frequencyStart = std::pow(10.0, std::log10(m_dragStartFrequency) - decades);
            m_frequencyStop = std::pow(10.0, std::log10(m_dragStopFrequency) - decades);
        } else {
            const double span = m_dragStopFrequency - m_dragStartFrequency;
            const double shift = span * deltaPixels / layout.plotArea.width();
            m_frequencyStart = m_dragStartFrequency - shift;
            m_frequencyStop = m_dragStopFrequency - shift;
        }
        emit viewChanged();
    }
    update();
}

void PainterPlotBackend::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && m_dragOrigin) {
        m_dragOrigin.reset();
        unsetCursor();
    }
    QWidget::mouseReleaseEvent(event);
}

void PainterPlotBackend::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        autoScale();
        event->accept();
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}

void PainterPlotBackend::keyPressEvent(QKeyEvent* event)
{
    // Keyboard equivalents for every mouse gesture (NFR-UX-3).
    const double span = m_frequencyStop - m_frequencyStart;
    const double centre = (m_frequencyStart + m_frequencyStop) / 2.0;
    switch (event->key()) {
        case Qt::Key_Plus:
        case Qt::Key_Equal:
            zoomFrequency(0.8, centre);
            return;
        case Qt::Key_Minus:
            zoomFrequency(1.25, centre);
            return;
        case Qt::Key_Left:
            m_frequencyStart -= span * 0.1;
            m_frequencyStop -= span * 0.1;
            emit viewChanged();
            update();
            return;
        case Qt::Key_Right:
            m_frequencyStart += span * 0.1;
            m_frequencyStop += span * 0.1;
            emit viewChanged();
            update();
            return;
        case Qt::Key_Home:
            autoScale();
            return;
        default:
            break;
    }
    QWidget::keyPressEvent(event);
}

void PainterPlotBackend::leaveEvent(QEvent* event)
{
    m_cursor.reset();
    update();
    QWidget::leaveEvent(event);
}

} // namespace peakemi::ui
