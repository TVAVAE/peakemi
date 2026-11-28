#pragma once

#include <peakemi/ui/IPlotBackend.h>
#include <peakemi/ui/Theme.h>

#include <QPoint>
#include <QWidget>

#include <optional>
#include <vector>

class QColor;
class QFontMetricsF;
class QPainter;
class QRectF;

namespace peakemi::ui {

/// QPainter implementation of IPlotBackend (path B of architecture.md 7.1).
///
/// Traces are decimated to a min/max envelope per pixel column before drawing,
/// which is what keeps a 40,001-point trace interactive: the cost of a repaint
/// depends on the widget width, not on the trace length (NFR-PERF-2). The full
/// arrays stay untouched in the session.
class PainterPlotBackend
    : public QWidget
    , public IPlotBackend
{
    Q_OBJECT

public:
    explicit PainterPlotBackend(QWidget* parent = nullptr);
    ~PainterPlotBackend() override;

    void setSeries(const PlotSeries& series) override;
    void removeSeries(const QString& id) override;
    void clearSeries() override;
    void setSeriesVisible(const QString& id, bool visible) override;

    void setFrequencyRange(FrequencyRange range) override;
    void setAmplitudeRange(double minimum, double maximum) override;
    void setLogarithmicFrequency(bool logarithmic) override;
    void setAmplitudeLabel(const QString& label) override;
    void autoScale() override;

    void setMarkers(const QVector<PlotMarker>& markers) override;
    void setSettingsSummary(const QStringList& items) override;
    void setVerdict(const PlotVerdict& verdict) override;

    /// Series drawn under the measurement and kept when it is replaced or
    /// cleared: reference traces and the results of trace math.
    void setOverlaySeries(const QVector<PlotSeries>& overlays);

    /// A marker the user placed, read off the measurement at its frequency.
    struct UserMarker
    {
        int number{1};
        double frequency{0.0};
    };

    /// Put marker @p number (1 or 2) at the highest point of the measurement
    /// within a few pixels of @p frequency, as an analyzer's peak search does.
    void placeUserMarker(int number, double frequency);
    void clearUserMarkers();

    [[nodiscard]] const QVector<UserMarker>& userMarkers() const { return m_userMarkers; }

    /// The measurement's level at @p frequency, or NaN without a measurement.
    [[nodiscard]] double measuredAmplitudeAt(double frequency) const;

    /// Draw the display in @p scheme rather than in the interface's current
    /// one. The report sets Light: it is printed on white paper whatever the
    /// operator's screen looked like.
    void setDisplayScheme(std::optional<Scheme> scheme);

    [[nodiscard]] QImage renderToImage(QSize size) const override;
    [[nodiscard]] bool exportSvg(const QString& path, QSize size) const override;

    [[nodiscard]] QWidget* widget() override { return this; }

    [[nodiscard]] bool isLogarithmicFrequency() const { return m_logarithmic; }

signals:
    /// Cursor position in data coordinates, for the status bar readout.
    void cursorMoved(double frequencyHz, double amplitude);
    /// The user clicked near a marker; the index refers to setMarkers().
    void markerActivated(int index);
    void viewChanged();
    /// The user asked, from the plot's context menu, for the peak at this
    /// frequency to be measured now with the verification detector.
    void measurePeakRequested(double frequencyHz);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;

private:
    struct Layout
    {
        QRectF plotArea;
        double frequencyStart{0.0};
        double frequencyStop{1.0};
        double amplitudeMinimum{0.0};
        double amplitudeMaximum{1.0};
    };

    [[nodiscard]] Layout layoutFor(QSize size) const;
    [[nodiscard]] double toX(const Layout& layout, double frequency) const;
    [[nodiscard]] double toY(const Layout& layout, double amplitude) const;
    [[nodiscard]] double fromX(const Layout& layout, double x) const;
    [[nodiscard]] double fromY(const Layout& layout, double y) const;

    [[nodiscard]] DisplayColours colours() const;
    [[nodiscard]] QColor colourFor(PlotRole role, const QColor& fallback) const;

    void render(QPainter& painter, QSize size) const;
    void drawSettingsBar(QPainter& painter, QSize size) const;
    /// A strip of "key value" items, the key in @p keyColour.
    void drawBar(QPainter& painter,
                 const QRectF& bar,
                 const QStringList& items,
                 const QColor& keyColour) const;
    /// Height of the legend box and its offset from the top of the display.
    [[nodiscard]] double legendHeight(const QFontMetricsF& metrics) const;
    void drawGrid(QPainter& painter, const Layout& layout) const;
    void drawFooter(QPainter& painter, const Layout& layout) const;
    void drawBands(QPainter& painter, const Layout& layout) const;
    void drawVerdict(QPainter& painter, const Layout& layout) const;
    [[nodiscard]] QColor outcomeColour(PlotOutcome outcome) const;
    void drawSeries(QPainter& painter, const Layout& layout, const PlotSeries& series) const;
    void drawMarkers(QPainter& painter, const Layout& layout) const;
    void drawLegend(QPainter& painter, const Layout& layout) const;
    void drawCursor(QPainter& painter, const Layout& layout) const;
    void drawUserMarkers(QPainter& painter, const Layout& layout, QSize size) const;
    /// The measurement: the trace series, or else the first line series.
    [[nodiscard]] const PlotSeries* measurement() const;
    /// The overlays and the series; overlays first when drawing, so the
    /// measurement stays on top, and last in the legend.
    [[nodiscard]] std::vector<const PlotSeries*> allSeries(bool overlaysFirst) const;
    /// Frequency of the measurement's highest point within @p pixels of @p x.
    [[nodiscard]] double peakNear(const Layout& layout, double x, double pixels) const;

    void zoomFrequency(double factor, double anchorFrequency);

    std::vector<PlotSeries> m_series;
    QVector<PlotMarker> m_markers;
    QStringList m_settings;
    PlotVerdict m_verdict;
    QVector<PlotSeries> m_overlays;
    QVector<UserMarker> m_userMarkers;
    std::optional<Scheme> m_scheme;
    QString m_amplitudeLabel{QStringLiteral("dBuV")};

    double m_frequencyStart{30e6};
    double m_frequencyStop{1e9};
    double m_amplitudeMinimum{0.0};
    double m_amplitudeMaximum{80.0};
    bool m_logarithmic{false};

    std::optional<QPointF> m_cursor;
    std::optional<QPoint> m_dragOrigin;
    double m_dragStartFrequency{0.0};
    double m_dragStopFrequency{0.0};
};

} // namespace peakemi::ui
