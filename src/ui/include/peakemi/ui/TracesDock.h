#pragma once

#include <peakemi/core/Trace.h>
#include <peakemi/ui/IPlotBackend.h>

#include <QColor>
#include <QDockWidget>
#include <QString>
#include <QVector>

#include <optional>
#include <vector>

class QListWidget;
class QPushButton;

namespace peakemi::ui {

/// Reference traces and trace math, overlaid on the measurement.
///
/// Earlier measurements -- a session file or a trace exported as CSV -- or
/// the measurement on screen kept as it is now, are drawn under the live
/// trace for comparison: before and after a fix, L against N. Two traces can
/// be combined into a third: their worst case, their common floor, their
/// average, or their difference.
class TracesDock : public QDockWidget
{
    Q_OBJECT

public:
    explicit TracesDock(QWidget* parent = nullptr);

    /// The measurement on screen, offered for snapshots and as an operand.
    void setCurrentTrace(const std::optional<Trace>& trace);

    /// Add @p trace as a reference, e.g. from a file loaded elsewhere.
    void addReference(Trace trace, const QString& source);

    /// The visible references, as plot series in their own colours.
    [[nodiscard]] QVector<PlotSeries> overlaySeries() const;

    [[nodiscard]] int referenceCount() const { return static_cast<int>(m_entries.size()); }

signals:
    void overlaysChanged();
    void statusMessage(const QString& message);

private:
    struct Entry
    {
        Trace trace;
        QString source;
        QColor colour;
        bool visible{true};
    };

    void loadReferences();
    void keepCurrent();
    void combine();
    void exportSelected();
    void removeSelected();
    void refreshList();
    void updateEnabledState();
    [[nodiscard]] QColor nextColour() const;

    std::vector<Entry> m_entries;
    std::optional<Trace> m_current;
    bool m_updating{false};

    QListWidget* m_list{nullptr};
    QPushButton* m_keep{nullptr};
    QPushButton* m_math{nullptr};
    QPushButton* m_export{nullptr};
    QPushButton* m_remove{nullptr};
};

} // namespace peakemi::ui
