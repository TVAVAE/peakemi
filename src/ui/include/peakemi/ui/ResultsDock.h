#pragma once

#include <peakemi/core/MeasurementPoint.h>

#include <QDockWidget>

class QLabel;
class QPushButton;
class QSortFilterProxyModel;
class QTableView;

namespace peakemi::ui {

class ResultsTableModel;

/// Sortable, verdict-coloured table of the Phase 2 results, kept in sync with
/// the plot selection (architecture.md 7).
class ResultsDock : public QDockWidget
{
    Q_OBJECT

public:
    explicit ResultsDock(QWidget* parent = nullptr);

    void appendPoint(const MeasurementPoint& point);
    void setPoints(std::vector<MeasurementPoint> points);
    void clear();

    [[nodiscard]] const std::vector<MeasurementPoint>& points() const;

    /// Select the row whose frequency matches, e.g. after a plot marker click.
    void selectFrequency(Hertz frequency);

    /// Whether re-measuring is possible now: an instrument is connected and
    /// no run is in progress.
    void setRemeasureEnabled(bool enabled);

    /// Put re-measured points in place of the ones they re-measure -- the
    /// stored point within @p tolerance of each new one -- and add the rest.
    void mergePoints(const std::vector<MeasurementPoint>& measured, Hertz tolerance);

signals:
    /// The user selected a row; the plot highlights the matching marker.
    void pointSelected(peakemi::Hertz frequency);
    /// The user asked for the selected points to be measured again.
    void remeasureRequested(std::vector<peakemi::Hertz> frequencies);

private:
    void updateSummary();
    void updateRemeasureButton();

    ResultsTableModel* m_model{nullptr};
    QSortFilterProxyModel* m_proxy{nullptr};
    QTableView* m_view{nullptr};
    QLabel* m_summary{nullptr};
    QPushButton* m_remeasure{nullptr};
    bool m_remeasureEnabled{false};
};

} // namespace peakemi::ui
