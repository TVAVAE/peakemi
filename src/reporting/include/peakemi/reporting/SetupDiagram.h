#pragma once

#include <peakemi/core/TestSetup.h>

#include <QColor>
#include <QImage>
#include <QRectF>
#include <QSizeF>
#include <QString>

#include <cstdint>
#include <vector>

class QPainter;
class QPalette;

namespace peakemi::reporting {

/// The colours a set-up diagram is drawn in.
struct DiagramColours
{
    QColor background;
    QColor block;
    QColor border;
    QColor text;
    QColor muted;
    QColor power;  ///< supply and mains wiring
    QColor signal; ///< the measured signal, from the EUT to the analyzer
    QColor accent; ///< the selected block and switches that are on
    QColor warning;

    /// Black on white, for paper.
    [[nodiscard]] static DiagramColours forPrint();
    /// From the application's palette, light or dark.
    [[nodiscard]] static DiagramColours from(const QPalette& palette);
};

/// The block diagram of a test set-up, as a lab draws one: the equipment under
/// test and how it is coupled -- through a LISN, a current probe, an antenna, a
/// TEM cell -- its supply side, and the measurement chain to the analyzer and
/// the PC running PeakEmi.
///
/// The blocks come from the set-up's method (TestSetup::blocks()). Drawn for
/// editing, the optional blocks that are off are shown dimmed with a switch;
/// drawn for paper, they are left out.
class SetupDiagram
{
public:
    /// Blocks that are part of every diagram rather than a device.
    enum class Fixed : std::uint8_t
    {
        None,
        Eut,
        Mains,
        PeakEmi
    };

    enum class LinkKind : std::uint8_t
    {
        Power,  ///< wiring between the supply, the coupling device and the EUT
        Field,  ///< the EUT radiating to an antenna, a probe or a cell
        Signal, ///< the measured signal on its way to the analyzer
        Data    ///< analyzer to PC
    };

    struct Block
    {
        QRectF rect;
        Fixed fixed{Fixed::None};
        int slot{-1};   ///< into slotsFor(method), -1 for a device no slot names
        int device{-1}; ///< into the set-up's devices, -1 for an empty slot
        SlotPlace place{SlotPlace::Chain};
        bool optional{false};
        bool inUse{true};
        QString caption;
    };

    struct Link
    {
        int from{0}; ///< into blocks()
        int to{0};
        LinkKind kind{LinkKind::Signal};
    };

    /// @p forEditing draws the optional blocks that are off, with switches.
    SetupDiagram(const TestSetup& setup, bool forEditing);

    /// The size it is drawn at, before any scaling.
    [[nodiscard]] QSizeF size() const { return m_size; }

    [[nodiscard]] const std::vector<Block>& blocks() const { return m_blocks; }

    [[nodiscard]] const std::vector<Link>& links() const { return m_links; }

    /// Where an optional block's on/off switch is drawn, when editing.
    [[nodiscard]] QRectF switchRect(const Block& block) const;
    /// Whether @p block can be switched on or off here.
    [[nodiscard]] bool hasSwitch(const Block& block) const;

    /// Index of the block at @p point (diagram coordinates), or -1.
    [[nodiscard]] int blockAt(QPointF point) const;

    /// What a block is, in full: for a tooltip.
    [[nodiscard]] QString describe(int block) const;

    /// Draw at size(); scale the painter to draw it larger or smaller.
    void paint(QPainter& painter,
               const DiagramColours& colours,
               int selectedDevice = -1,
               int hoveredBlock = -1) const;

    /// The diagram as an image, @p scale times its size.
    [[nodiscard]] QImage toImage(double scale, const DiagramColours& colours) const;

private:
    void layOut();
    void paintLink(QPainter& painter, const Link& link, const DiagramColours& colours) const;
    void paintBlock(QPainter& painter,
                    int index,
                    const DiagramColours& colours,
                    bool selected,
                    bool hovered) const;

    TestSetup m_setup;
    bool m_forEditing{false};
    std::vector<Block> m_blocks;
    std::vector<Link> m_links;
    QSizeF m_size;
};

} // namespace peakemi::reporting
