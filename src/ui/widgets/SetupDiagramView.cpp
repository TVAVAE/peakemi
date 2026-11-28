#include <peakemi/ui/SetupDiagramView.h>

#include <QEvent>
#include <QHelpEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QToolTip>

#include <algorithm>

namespace peakemi::ui {
namespace {

/// Drawn no larger than this: a few blocks should not fill a wide window.
constexpr double MaximumScale = 1.15;

} // namespace

SetupDiagramView::SetupDiagramView(QWidget* parent) : QWidget{parent}
{
    setObjectName(QStringLiteral("setupDiagram"));
    setMouseTracking(true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
}

void SetupDiagramView::setSetup(const TestSetup* setup)
{
    if (setup != nullptr) {
        m_setup = *setup;
    } else {
        m_setup.reset();
    }
    rebuild();
}

void SetupDiagramView::setEditable(bool editable)
{
    m_editable = editable;
    rebuild();
}

void SetupDiagramView::setSelectedDevice(int device)
{
    m_selected = device;
    update();
}

const reporting::SetupDiagram* SetupDiagramView::diagram() const
{
    return m_diagram ? &*m_diagram : nullptr;
}

void SetupDiagramView::rebuild()
{
    m_hovered = -1;
    if (m_setup) {
        m_diagram.emplace(*m_setup, m_editable);
    } else {
        m_diagram.reset();
    }
    updateGeometry();
    update();
}

QTransform SetupDiagramView::transform() const
{
    if (!m_diagram) {
        return {};
    }
    const QSizeF natural = m_diagram->size();
    const double scale =
        std::min({MaximumScale, width() / natural.width(), height() / natural.height()});
    const double x = (width() - natural.width() * scale) / 2.0;
    const double y = (height() - natural.height() * scale) / 2.0;
    QTransform transform;
    transform.translate(std::max(0.0, x), std::max(0.0, y));
    transform.scale(scale, scale);
    return transform;
}

QPointF SetupDiagramView::toWidget(QPointF diagramPoint) const
{
    return transform().map(diagramPoint);
}

int SetupDiagramView::blockAt(QPointF widgetPoint) const
{
    if (!m_diagram) {
        return -1;
    }
    bool invertible = false;
    const QTransform inverse = transform().inverted(&invertible);
    return invertible ? m_diagram->blockAt(inverse.map(widgetPoint)) : -1;
}

QSize SetupDiagramView::sizeHint() const
{
    if (!m_diagram) {
        return QSize{640, 220};
    }
    return m_diagram->size().toSize();
}

QSize SetupDiagramView::minimumSizeHint() const
{
    return QSize{320, 140};
}

int SetupDiagramView::heightForWidth(int width) const
{
    if (!m_diagram) {
        return 220;
    }
    const QSizeF natural = m_diagram->size();
    const double scale = std::min(MaximumScale, width / natural.width());
    return static_cast<int>(natural.height() * scale) + 2;
}

void SetupDiagramView::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter{this};
    const auto colours = reporting::DiagramColours::from(palette());
    painter.fillRect(rect(), colours.background);
    if (!m_diagram) {
        painter.setPen(colours.muted);
        painter.drawText(rect(), Qt::AlignCenter, tr("No set-up selected."));
        return;
    }
    painter.setTransform(transform());
    m_diagram->paint(painter, colours, m_selected, m_hovered);
}

void SetupDiagramView::mouseMoveEvent(QMouseEvent* event)
{
    const int hovered = blockAt(event->position());
    if (hovered != m_hovered) {
        m_hovered = hovered;
        const bool clickable =
            hovered >= 0 && m_diagram->blocks()[static_cast<std::size_t>(hovered)].fixed ==
                                reporting::SetupDiagram::Fixed::None;
        setCursor(clickable && m_editable ? Qt::PointingHandCursor : Qt::ArrowCursor);
        update();
    }
    QWidget::mouseMoveEvent(event);
}

void SetupDiagramView::mousePressEvent(QMouseEvent* event)
{
    const int index = blockAt(event->position());
    if (!m_editable || index < 0 || event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }
    const auto& block = m_diagram->blocks()[static_cast<std::size_t>(index)];
    if (block.fixed != reporting::SetupDiagram::Fixed::None) {
        return;
    }
    bool invertible = false;
    const QPointF point = transform().inverted(&invertible).map(event->position());
    const bool onSwitch = m_diagram->hasSwitch(block) &&
                          m_diagram->switchRect(block).adjusted(-4, -4, 4, 4).contains(point);
    if (onSwitch || block.device < 0) {
        // The switch, or an empty block: clicking it is asking for it.
        const bool on = block.device < 0 || !block.inUse;
        if (block.slot >= 0) {
            emit slotSwitched(block.slot, on);
        } else {
            emit deviceSwitched(block.device, on);
        }
        return;
    }
    m_selected = block.device;
    update();
    emit deviceSelected(block.device);
}

void SetupDiagramView::mouseDoubleClickEvent(QMouseEvent* event)
{
    const int index = blockAt(event->position());
    if (m_editable && index >= 0) {
        const auto& block = m_diagram->blocks()[static_cast<std::size_t>(index)];
        if (block.device >= 0) {
            emit deviceActivated(block.device);
            return;
        }
    }
    QWidget::mouseDoubleClickEvent(event);
}

void SetupDiagramView::leaveEvent(QEvent* event)
{
    if (m_hovered >= 0) {
        m_hovered = -1;
        update();
    }
    QWidget::leaveEvent(event);
}

bool SetupDiagramView::event(QEvent* event)
{
    if (event->type() == QEvent::ToolTip && m_diagram) {
        auto* help = static_cast<QHelpEvent*>(event);
        const int index = blockAt(help->pos());
        if (index >= 0) {
            QToolTip::showText(help->globalPos(), m_diagram->describe(index), this);
        } else {
            QToolTip::hideText();
            event->ignore();
        }
        return true;
    }
    return QWidget::event(event);
}

void SetupDiagramView::changeEvent(QEvent* event)
{
    if (event->type() == QEvent::PaletteChange || event->type() == QEvent::StyleChange) {
        update();
    }
    QWidget::changeEvent(event);
}

} // namespace peakemi::ui
