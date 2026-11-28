#pragma once

#include <peakemi/core/TestSetup.h>
#include <peakemi/reporting/SetupDiagram.h>

#include <QTransform>
#include <QWidget>

#include <optional>

namespace peakemi::ui {

/// A set-up's block diagram, to configure the bench by: click a block to pick
/// its device, flip a block's switch to take it in or out of the set-up.
///
/// The view does not change the set-up itself; it says what was asked, and
/// the owner changes the set-up and hands it back with setSetup().
class SetupDiagramView : public QWidget
{
    Q_OBJECT

public:
    explicit SetupDiagramView(QWidget* parent = nullptr);

    /// Show @p setup; nullptr shows nothing.
    void setSetup(const TestSetup* setup);
    /// Editing shows the blocks that are off, with switches; otherwise the
    /// diagram is drawn as it is printed.
    void setEditable(bool editable);

    void setSelectedDevice(int device);

    [[nodiscard]] int selectedDevice() const { return m_selected; }

    /// The diagram as laid out now, if a set-up is shown.
    [[nodiscard]] const reporting::SetupDiagram* diagram() const;
    /// Where a point of the diagram is on the widget.
    [[nodiscard]] QPointF toWidget(QPointF diagramPoint) const;

    [[nodiscard]] QSize sizeHint() const override;
    [[nodiscard]] QSize minimumSizeHint() const override;

    [[nodiscard]] bool hasHeightForWidth() const override { return true; }

    [[nodiscard]] int heightForWidth(int width) const override;

signals:
    /// A device's block was clicked.
    void deviceSelected(int device);
    /// A device's block was double-clicked, to edit it.
    void deviceActivated(int device);
    /// A slot's switch was flipped, or its empty block clicked to add it.
    void slotSwitched(int slot, bool inUse);
    /// The switch of a device no slot names was flipped.
    void deviceSwitched(int device, bool inUse);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
    bool event(QEvent* event) override;
    void changeEvent(QEvent* event) override;

private:
    void rebuild();
    [[nodiscard]] QTransform transform() const;
    [[nodiscard]] int blockAt(QPointF widgetPoint) const;

    std::optional<TestSetup> m_setup;
    std::optional<reporting::SetupDiagram> m_diagram;
    bool m_editable{true};
    int m_selected{-1};
    int m_hovered{-1};
};

} // namespace peakemi::ui
