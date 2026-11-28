#include <peakemi/reporting/SetupDiagram.h>
#include <peakemi/reporting/SetupSheetRenderer.h>

#include <QFont>
#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPolygonF>
#include <QStringList>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <optional>

namespace peakemi::reporting {
namespace {

constexpr double BlockWidth = 156.0;
constexpr double BlockHeight = 84.0;
constexpr double GapX = 36.0;
constexpr double GapY = 38.0;
constexpr double Margin = 14.0;
constexpr double MainsWidth = 74.0;
constexpr double MainsHeight = 40.0;
constexpr double GlyphSize = 24.0;

[[nodiscard]] QString qs(std::string_view text)
{
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

[[nodiscard]] QRectF cell(int column, int row)
{
    return QRectF{Margin + column * (BlockWidth + GapX),
                  Margin + row * (BlockHeight + GapY),
                  BlockWidth,
                  BlockHeight};
}

enum class Glyph : std::uint8_t
{
    Generic,
    Analyzer,
    Antenna,
    Lisn,
    Limiter,
    Attenuator,
    Amplifier,
    Cable,
    CurrentProbe,
    NearFieldProbe,
    TemCell,
    Transformer,
    Supply,
    Eut,
    Pc,
    Mains
};

/// The symbol for a device, from the words in its role.
[[nodiscard]] Glyph glyphFor(const QString& role)
{
    const auto text = role.toLower();
    const auto has = [&text](const char* word) { return text.contains(QLatin1String{word}); };
    if (has("analy") || has("receiver")) {
        return Glyph::Analyzer;
    }
    if (has("near")) {
        return Glyph::NearFieldProbe;
    }
    if (has("probe")) {
        return Glyph::CurrentProbe;
    }
    if (has("antenna")) {
        return Glyph::Antenna;
    }
    if (has("tem")) {
        return Glyph::TemCell;
    }
    if (has("lisn") || has("network")) {
        return Glyph::Lisn;
    }
    if (has("limiter")) {
        return Glyph::Limiter;
    }
    if (has("attenuator") || has("pad")) {
        return Glyph::Attenuator;
    }
    if (has("amplifier") || has("preamp")) {
        return Glyph::Amplifier;
    }
    if (has("transformer")) {
        return Glyph::Transformer;
    }
    if (has("supply")) {
        return Glyph::Supply;
    }
    if (has("cable")) {
        return Glyph::Cable;
    }
    return Glyph::Generic;
}

/// Line-art symbols, in the spirit of a schematic: what an engineer would
/// sketch for the device in a lab notebook.
void drawGlyph(QPainter& painter, const QRectF& box, Glyph glyph, const QColor& colour)
{
    painter.save();
    QPen pen{colour, 1.4};
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    const double w = box.width();
    const double h = box.height();
    const auto at = [&box, w, h](double x, double y) {
        return QPointF{box.left() + x * w, box.top() + y * h};
    };
    switch (glyph) {
        case Glyph::Analyzer: {
            painter.drawRoundedRect(QRectF{at(0.0, 0.12), at(1.0, 0.88)}, 2.0, 2.0);
            painter.drawRect(QRectF{at(0.1, 0.25), at(0.64, 0.75)});
            QPainterPath trace;
            trace.moveTo(at(0.14, 0.66));
            trace.lineTo(at(0.27, 0.64));
            trace.lineTo(at(0.32, 0.33));
            trace.lineTo(at(0.37, 0.64));
            trace.lineTo(at(0.6, 0.66));
            painter.drawPath(trace);
            painter.drawEllipse(at(0.82, 0.38), 0.07 * w, 0.07 * w);
            painter.drawEllipse(at(0.82, 0.64), 0.05 * w, 0.05 * w);
            break;
        }
        case Glyph::Antenna:
            // Log-periodic: a boom, elements shortening towards the tip.
            painter.drawLine(at(0.04, 0.5), at(0.96, 0.5));
            for (int i = 0; i < 5; ++i) {
                const double x = 0.1 + i * 0.19;
                const double half = 0.44 - i * 0.075;
                painter.drawLine(at(x, 0.5 - half), at(x, 0.5 + half));
            }
            break;
        case Glyph::Lisn: {
            painter.drawRect(QRectF{at(0.0, 0.14), at(1.0, 0.86)});
            QPainterPath coil;
            coil.moveTo(at(0.08, 0.58));
            for (int i = 0; i < 3; ++i) {
                const double x0 = 0.2 + i * 0.2;
                coil.arcTo(QRectF{at(x0, 0.4), QSizeF{0.2 * w, 0.36 * h}}, 180.0, -180.0);
            }
            coil.lineTo(at(0.92, 0.58));
            painter.drawPath(coil);
            break;
        }
        case Glyph::Limiter:
            painter.drawLine(at(0.0, 0.5), at(0.32, 0.5));
            painter.drawPolygon(QPolygonF{{at(0.32, 0.22), at(0.32, 0.78), at(0.66, 0.5)}});
            painter.drawLine(at(0.66, 0.22), at(0.66, 0.78));
            painter.drawLine(at(0.66, 0.5), at(1.0, 0.5));
            break;
        case Glyph::Attenuator: {
            QPolygonF zigzag{{at(0.0, 0.5), at(0.16, 0.5)}};
            for (int i = 0; i < 6; ++i) {
                zigzag << at(0.2 + i * 0.11, i % 2 == 0 ? 0.26 : 0.74);
            }
            zigzag << at(0.84, 0.5) << at(1.0, 0.5);
            painter.drawPolyline(zigzag);
            break;
        }
        case Glyph::Amplifier:
            painter.drawLine(at(0.0, 0.5), at(0.22, 0.5));
            painter.drawPolygon(QPolygonF{{at(0.22, 0.14), at(0.22, 0.86), at(0.84, 0.5)}});
            painter.drawLine(at(0.84, 0.5), at(1.0, 0.5));
            break;
        case Glyph::Cable: {
            // A coiled length of coax.
            QPainterPath coil;
            coil.moveTo(at(0.0, 0.62));
            coil.cubicTo(at(0.2, 0.62), at(0.2, 0.2), at(0.38, 0.2));
            coil.cubicTo(at(0.56, 0.2), at(0.56, 0.8), at(0.38, 0.8));
            coil.cubicTo(at(0.22, 0.8), at(0.4, 0.38), at(0.62, 0.38));
            coil.cubicTo(at(0.8, 0.38), at(0.8, 0.62), at(1.0, 0.62));
            painter.drawPath(coil);
            break;
        }
        case Glyph::CurrentProbe:
            // A clamp round the cable it measures.
            painter.drawLine(at(0.5, 0.0), at(0.5, 1.0));
            painter.drawEllipse(at(0.5, 0.5), 0.4 * w, 0.18 * h);
            painter.drawLine(at(0.9, 0.5), at(1.0, 0.5));
            break;
        case Glyph::NearFieldProbe:
            painter.drawEllipse(at(0.5, 0.32), 0.24 * w, 0.24 * h);
            painter.drawLine(at(0.5, 0.56), at(0.5, 1.0));
            break;
        case Glyph::TemCell:
            painter.drawPolygon(QPolygonF{{at(0.0, 0.5),
                                           at(0.24, 0.16),
                                           at(0.76, 0.16),
                                           at(1.0, 0.5),
                                           at(0.76, 0.84),
                                           at(0.24, 0.84)}});
            painter.drawLine(at(0.14, 0.5), at(0.86, 0.5));
            break;
        case Glyph::Transformer: {
            for (const double x : {0.3, 0.7}) {
                QPainterPath coil;
                coil.moveTo(at(x, 0.08));
                for (int i = 0; i < 3; ++i) {
                    const double y0 = 0.14 + i * 0.24;
                    coil.arcTo(QRectF{at(x - 0.13, y0), QSizeF{0.26 * w, 0.24 * h}},
                               90.0,
                               x < 0.5 ? 180.0 : -180.0);
                }
                painter.drawPath(coil);
            }
            painter.drawLine(at(0.47, 0.1), at(0.47, 0.9));
            painter.drawLine(at(0.53, 0.1), at(0.53, 0.9));
            break;
        }
        case Glyph::Supply:
            painter.drawLine(at(0.0, 0.5), at(0.4, 0.5));
            painter.drawLine(at(0.4, 0.14), at(0.4, 0.86));
            painter.drawLine(at(0.58, 0.3), at(0.58, 0.7));
            painter.drawLine(at(0.58, 0.5), at(1.0, 0.5));
            break;
        case Glyph::Eut:
            painter.drawRect(QRectF{at(0.18, 0.22), at(0.82, 0.78)});
            for (const double x : {0.32, 0.5, 0.68}) {
                painter.drawLine(at(x, 0.06), at(x, 0.22));
                painter.drawLine(at(x, 0.78), at(x, 0.94));
            }
            break;
        case Glyph::Pc:
            painter.drawRoundedRect(QRectF{at(0.14, 0.12), at(0.86, 0.66)}, 1.5, 1.5);
            painter.drawPolyline(
                QPolygonF{{at(0.14, 0.66), at(0.0, 0.84), at(1.0, 0.84), at(0.86, 0.66)}});
            break;
        case Glyph::Mains: {
            painter.drawEllipse(at(0.5, 0.5), 0.46 * w, 0.46 * h);
            QPainterPath wave;
            wave.moveTo(at(0.24, 0.5));
            wave.cubicTo(at(0.34, 0.24), at(0.44, 0.24), at(0.5, 0.5));
            wave.cubicTo(at(0.56, 0.76), at(0.66, 0.76), at(0.76, 0.5));
            painter.drawPath(wave);
            break;
        }
        case Glyph::Generic:
            painter.drawLine(at(0.0, 0.5), at(0.14, 0.5));
            painter.drawRect(QRectF{at(0.14, 0.26), at(0.86, 0.74)});
            painter.drawLine(at(0.86, 0.5), at(1.0, 0.5));
            break;
    }
    painter.restore();
}

[[nodiscard]] Glyph glyphOf(const SetupDiagram::Block& block, const TestSetup& setup)
{
    switch (block.fixed) {
        case SetupDiagram::Fixed::Eut:
            return Glyph::Eut;
        case SetupDiagram::Fixed::Mains:
            return Glyph::Mains;
        case SetupDiagram::Fixed::PeakEmi:
            return Glyph::Pc;
        case SetupDiagram::Fixed::None:
            break;
    }
    if (block.device >= 0) {
        const auto& role = setup.devices[static_cast<std::size_t>(block.device)].equipment.role;
        const auto glyph = glyphFor(qs(role));
        if (glyph != Glyph::Generic) {
            return glyph;
        }
    }
    return glyphFor(block.caption);
}

[[nodiscard]] bool radiates(SetupMethod method)
{
    return method == SetupMethod::Antenna || method == SetupMethod::TemCell ||
           method == SetupMethod::NearFieldProbe;
}

/// "Attenuation +10.0 dB, flat": a correction in one short line.
[[nodiscard]] QString shortCorrection(const CorrectionTable& table)
{
    return QStringLiteral("%1 %2").arg(correctionKindName(table.kind), describeCorrection(table));
}

} // namespace

DiagramColours DiagramColours::forPrint()
{
    return DiagramColours{.background = QColor{0xFF, 0xFF, 0xFF},
                          .block = QColor{0xFF, 0xFF, 0xFF},
                          .border = QColor{0x5F, 0x63, 0x68},
                          .text = QColor{0x20, 0x21, 0x24},
                          .muted = QColor{0x80, 0x86, 0x8B},
                          .power = QColor{0x5F, 0x63, 0x68},
                          .signal = QColor{0x1A, 0x73, 0xE8},
                          .accent = QColor{0x1A, 0x73, 0xE8},
                          .warning = QColor{0xC5, 0x22, 0x1F}};
}

DiagramColours DiagramColours::from(const QPalette& palette)
{
    const QColor base = palette.color(QPalette::Base);
    const QColor text = palette.color(QPalette::Text);
    const bool dark = base.lightness() < 128;
    const auto mix = [](const QColor& a, const QColor& b, double share) {
        return QColor::fromRgbF(static_cast<float>(a.redF() * (1.0 - share) + b.redF() * share),
                                static_cast<float>(a.greenF() * (1.0 - share) + b.greenF() * share),
                                static_cast<float>(a.blueF() * (1.0 - share) + b.blueF() * share));
    };
    return DiagramColours{.background = base,
                          .block = palette.color(QPalette::Window),
                          .border = mix(base, text, 0.45),
                          .text = text,
                          .muted = mix(base, text, 0.55),
                          .power = mix(base, text, 0.6),
                          .signal = palette.color(QPalette::Highlight),
                          .accent = palette.color(QPalette::Highlight),
                          .warning = dark ? QColor{0xFF, 0x8A, 0x80} : QColor{0xC5, 0x22, 0x1F}};
}

SetupDiagram::SetupDiagram(const TestSetup& setup, bool forEditing)
    : m_setup{setup}
    , m_forEditing{forEditing}
{
    layOut();
}

void SetupDiagram::layOut()
{
    const auto methodSlots = slotsFor(m_setup.method);
    std::optional<Block> coupling;
    std::optional<Block> parallel;
    std::optional<Block> analyzer;
    std::vector<Block> supply;
    std::vector<Block> chain;
    for (const auto& entry : m_setup.blocks()) {
        Block block;
        block.slot = entry.slot;
        block.device = entry.device;
        block.place = entry.place;
        const SetupDevice* device =
            entry.device >= 0 ? &m_setup.devices[static_cast<std::size_t>(entry.device)] : nullptr;
        if (entry.slot >= 0) {
            const auto& slot = methodSlots[static_cast<std::size_t>(entry.slot)];
            block.optional = slot.optional;
            block.caption = qs(slot.caption);
        } else {
            block.optional = true;
            block.caption = device->equipment.role.empty() ? QStringLiteral("Device")
                                                           : qs(device->equipment.role);
        }
        block.inUse = device != nullptr && device->inUse;
        // On paper only what is in use, and the required blocks even when
        // nothing fills them, so the drawing never loses its analyzer.
        const bool requiredButEmpty = entry.slot >= 0 && !block.optional && device == nullptr;
        if (!m_forEditing && !block.inUse && !requiredButEmpty) {
            continue;
        }
        switch (entry.place) {
            case SlotPlace::Coupling:
                coupling = block;
                break;
            case SlotPlace::Parallel:
                parallel = block;
                break;
            case SlotPlace::Supply:
                supply.push_back(block);
                break;
            case SlotPlace::Chain:
                chain.push_back(block);
                break;
            case SlotPlace::Analyzer:
                analyzer = block;
                break;
        }
    }

    // Two rows when the EUT is coupled through something: the EUT, the
    // coupling device and its supply side along the top, the measurement
    // chain below, starting under the coupling device. A third row on top
    // for what sits beside the coupling device. One row for a plain chain.
    const bool layered = coupling.has_value();
    const int topRow = layered && parallel ? 1 : 0;
    const int chainRow = layered ? topRow + 1 : 0;
    const auto add = [this](Block block, int column, int row) {
        block.rect = cell(column, row);
        m_blocks.push_back(std::move(block));
        return static_cast<int>(m_blocks.size()) - 1;
    };

    Block eut;
    eut.fixed = Fixed::Eut;
    eut.caption = QStringLiteral("EUT");
    const int eutIndex = add(eut, 0, topRow);
    int signalFrom = eutIndex;
    if (layered) {
        const int couplingIndex = add(*coupling, 1, topRow);
        m_links.push_back(
            Link{.from = eutIndex,
                 .to = couplingIndex,
                 .kind = radiates(m_setup.method) ? LinkKind::Field : LinkKind::Power});
        int powerFrom = couplingIndex;
        int firstSupply = -1;
        for (std::size_t i = 0; i < supply.size(); ++i) {
            const int index = add(supply[i], 2 + static_cast<int>(i), topRow);
            m_links.push_back(Link{.from = powerFrom, .to = index, .kind = LinkKind::Power});
            powerFrom = index;
            firstSupply = firstSupply < 0 ? index : firstSupply;
        }
        if (m_setup.method == SetupMethod::LisnAc) {
            Block mains;
            mains.fixed = Fixed::Mains;
            mains.caption = QStringLiteral("Mains");
            const QRectF space = cell(2 + static_cast<int>(supply.size()), topRow);
            mains.rect = QRectF{
                space.left(), space.center().y() - MainsHeight / 2.0, MainsWidth, MainsHeight};
            m_blocks.push_back(mains);
            const int index = static_cast<int>(m_blocks.size()) - 1;
            m_links.push_back(Link{.from = powerFrom, .to = index, .kind = LinkKind::Power});
            firstSupply = firstSupply < 0 ? index : firstSupply;
        }
        if (parallel) {
            const int index = add(*parallel, 1, 0);
            m_links.push_back(Link{.from = index, .to = eutIndex, .kind = LinkKind::Power});
            if (firstSupply >= 0) {
                m_links.push_back(Link{.from = index, .to = firstSupply, .kind = LinkKind::Power});
            }
        }
        signalFrom = couplingIndex;
    }
    int column = 1;
    if (analyzer) {
        chain.push_back(*analyzer);
    }
    for (const auto& block : chain) {
        const int index = add(block, column++, chainRow);
        m_links.push_back(Link{.from = signalFrom, .to = index, .kind = LinkKind::Signal});
        signalFrom = index;
    }
    Block pc;
    pc.fixed = Fixed::PeakEmi;
    pc.caption = QStringLiteral("PeakEmi");
    // Above the analyzer when nothing is drawn there, which keeps the diagram
    // a column narrower; otherwise after it.
    const int analyzerColumn = column - 1;
    const QRectF above = cell(analyzerColumn, chainRow - 1);
    const bool aboveIsFree =
        chainRow > 0 && std::none_of(m_blocks.begin(), m_blocks.end(), [&above](const Block& b) {
            return b.rect.intersects(above);
        });
    const int pcIndex =
        aboveIsFree ? add(pc, analyzerColumn, chainRow - 1) : add(pc, column, chainRow);
    m_links.push_back(Link{.from = signalFrom, .to = pcIndex, .kind = LinkKind::Data});

    double right = 0.0;
    double bottom = 0.0;
    for (const auto& block : m_blocks) {
        right = std::max(right, block.rect.right());
        bottom = std::max(bottom, block.rect.bottom());
    }
    m_size = QSizeF{right + Margin, bottom + Margin};
}

bool SetupDiagram::hasSwitch(const Block& block) const
{
    return m_forEditing && block.fixed == Fixed::None && block.optional;
}

QRectF SetupDiagram::switchRect(const Block& block) const
{
    return QRectF{block.rect.right() - 38.0, block.rect.top() + 10.0, 28.0, 15.0};
}

int SetupDiagram::blockAt(QPointF point) const
{
    for (int i = static_cast<int>(m_blocks.size()) - 1; i >= 0; --i) {
        if (m_blocks[static_cast<std::size_t>(i)].rect.contains(point)) {
            return i;
        }
    }
    return -1;
}

QString SetupDiagram::describe(int index) const
{
    if (index < 0 || static_cast<std::size_t>(index) >= m_blocks.size()) {
        return {};
    }
    const auto& block = m_blocks[static_cast<std::size_t>(index)];
    switch (block.fixed) {
        case Fixed::Eut:
            return QStringLiteral("The equipment under test.");
        case Fixed::Mains:
            return QStringLiteral("The mains supply.");
        case Fixed::PeakEmi:
            return QStringLiteral("PeakEmi: controls the analyzer and writes the report.");
        case Fixed::None:
            break;
    }
    if (block.device < 0) {
        return QStringLiteral("<b>%1</b><br>Not in this set-up. Click to add one.")
            .arg(block.caption.toHtmlEscaped());
    }
    const auto& device = m_setup.devices[static_cast<std::size_t>(block.device)];
    const auto& item = device.equipment;
    QStringList lines{QStringLiteral("<b>%1</b>").arg(block.caption.toHtmlEscaped())};
    const auto line = [&lines](const QString& key, const std::string& value) {
        if (!value.empty()) {
            lines << QStringLiteral("%1: %2").arg(key, qs(value).toHtmlEscaped());
        }
    };
    line(QStringLiteral("Model"), item.model);
    line(QStringLiteral("Serial number"), item.serial);
    if (!item.calibrationDue.empty()) {
        lines << QStringLiteral("Calibration due: %1%2")
                     .arg(qs(item.calibrationDue).toHtmlEscaped(),
                          item.calibrationExpired(std::chrono::system_clock::now())
                              ? QStringLiteral(" <b>(expired)</b>")
                              : QString{});
    }
    if (device.correction) {
        lines << QStringLiteral("Correction: %1").arg(shortCorrection(*device.correction));
    }
    line(QStringLiteral("Notes"), device.notes);
    if (!device.inUse) {
        lines << QStringLiteral(
            "<i>Switched off: not in the equipment list, corrects nothing.</i>");
    }
    return lines.join(QStringLiteral("<br>"));
}

void SetupDiagram::paint(QPainter& painter,
                         const DiagramColours& colours,
                         int selectedDevice,
                         int hoveredBlock) const
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    for (const auto& link : m_links) {
        paintLink(painter, link, colours);
    }
    // The signal passes a block that is off by: drawn through it, faintly.
    for (const auto& block : m_blocks) {
        if (block.fixed == Fixed::None && !block.inUse && block.place == SlotPlace::Chain) {
            QPen pen{colours.signal, 1.4, Qt::DashLine};
            painter.setPen(pen);
            painter.drawLine(QPointF{block.rect.left(), block.rect.center().y()},
                             QPointF{block.rect.right(), block.rect.center().y()});
        }
    }
    for (std::size_t i = 0; i < m_blocks.size(); ++i) {
        const auto& block = m_blocks[i];
        paintBlock(painter,
                   static_cast<int>(i),
                   colours,
                   block.device >= 0 && block.device == selectedDevice,
                   static_cast<int>(i) == hoveredBlock);
    }
    painter.restore();
}

void SetupDiagram::paintLink(QPainter& painter,
                             const Link& link,
                             const DiagramColours& colours) const
{
    const QRectF a = m_blocks[static_cast<std::size_t>(link.from)].rect;
    const QRectF b = m_blocks[static_cast<std::size_t>(link.to)].rect;
    QPen pen{colours.power, 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin};
    switch (link.kind) {
        case LinkKind::Power:
            break;
        case LinkKind::Signal:
        case LinkKind::Field:
            pen.setColor(colours.signal);
            pen.setWidthF(2.2);
            break;
        case LinkKind::Data:
            pen.setColor(colours.muted);
            pen.setWidthF(1.5);
            pen.setStyle(Qt::DashLine);
            break;
    }
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);

    if (link.kind == LinkKind::Field) {
        // Wavefronts leaving the EUT towards what picks them up.
        const QPointF origin{a.right() + 4.0, a.center().y()};
        const double reach = b.left() - a.right() - 8.0;
        for (int i = 1; i <= 3; ++i) {
            const double radius = reach * (0.3 * i);
            painter.drawArc(
                QRectF{origin.x() - radius, origin.y() - radius, 2.0 * radius, 2.0 * radius},
                -40 * 16,
                80 * 16);
        }
        return;
    }

    QPolygonF path;
    const bool sameRow = std::abs(a.center().y() - b.center().y()) < 1.0;
    const bool sameColumn = std::abs(a.center().x() - b.center().x()) < 1.0;
    if (sameRow) {
        const bool forward = b.center().x() > a.center().x();
        path << QPointF{forward ? a.right() : a.left(), a.center().y()}
             << QPointF{forward ? b.left() : b.right(), b.center().y()};
    } else if (sameColumn) {
        const bool down = b.center().y() > a.center().y();
        path << QPointF{a.center().x(), down ? a.bottom() : a.top()}
             << QPointF{b.center().x(), down ? b.top() : b.bottom()};
    } else {
        // Out of the side, along, and into the other block from above or below.
        const bool right = b.center().x() > a.center().x();
        const bool down = b.center().y() > a.center().y();
        path << QPointF{right ? a.right() : a.left(), a.center().y()}
             << QPointF{b.center().x(), a.center().y()}
             << QPointF{b.center().x(), down ? b.top() : b.bottom()};
    }
    painter.drawPolyline(path);

    if (link.kind == LinkKind::Signal && path.size() >= 2) {
        // An arrowhead where the signal enters the next block.
        const QPointF tip = path.back();
        const QPointF from = path[path.size() - 2];
        const double length = std::hypot(tip.x() - from.x(), tip.y() - from.y());
        if (length > 0.0) {
            const QPointF unit{(tip.x() - from.x()) / length, (tip.y() - from.y()) / length};
            const QPointF normal{-unit.y(), unit.x()};
            const QPointF base = tip - unit * 8.0;
            painter.setBrush(colours.signal);
            painter.setPen(Qt::NoPen);
            painter.drawPolygon(QPolygonF{{tip, base + normal * 4.0, base - normal * 4.0}});
        }
    }
}

void SetupDiagram::paintBlock(
    QPainter& painter, int index, const DiagramColours& colours, bool selected, bool hovered) const
{
    const auto& block = m_blocks[static_cast<std::size_t>(index)];
    const bool off = block.fixed == Fixed::None && !block.inUse;
    const QRectF rect = block.rect;
    painter.save();

    QColor fill = colours.block;
    if (off) {
        fill.setAlphaF(0.6F);
    }
    QPen pen{off ? colours.muted : colours.border, 1.2};
    if (off) {
        pen.setStyle(Qt::DashLine);
    }
    if (hovered) {
        pen = QPen{colours.accent, 1.6};
    }
    if (selected) {
        pen = QPen{colours.accent, 2.6};
    }
    painter.setPen(pen);
    painter.setBrush(fill);
    painter.drawRoundedRect(rect, 6.0, 6.0);
    painter.setBrush(Qt::NoBrush);

    const QColor ink = off ? colours.muted : colours.text;
    QFont captionFont = painter.font();
    captionFont.setPointSizeF(8.5);
    captionFont.setBold(true);
    QFont bodyFont = painter.font();
    bodyFont.setPointSizeF(7.8);
    bodyFont.setBold(false);

    if (block.fixed == Fixed::Mains) {
        drawGlyph(painter,
                  QRectF{rect.left() + 8.0, rect.center().y() - 10.0, 20.0, 20.0},
                  Glyph::Mains,
                  ink);
        painter.setFont(captionFont);
        painter.setPen(ink);
        painter.drawText(
            rect.adjusted(34.0, 0.0, -4.0, 0.0), Qt::AlignVCenter | Qt::AlignLeft, block.caption);
        painter.restore();
        return;
    }

    drawGlyph(painter,
              QRectF{rect.left() + 10.0, rect.top() + 10.0, GlyphSize, GlyphSize},
              glyphOf(block, m_setup),
              ink);

    // The caption beside the symbol, on two lines if it needs them.
    const double captionLeft = rect.left() + 44.0;
    const double captionRight = rect.right() - (hasSwitch(block) ? 44.0 : 8.0);
    const QFontMetricsF captionMetrics{captionFont};
    const QRectF captionArea{captionLeft,
                             rect.top() + 9.0,
                             captionRight - captionLeft,
                             2.0 * captionMetrics.lineSpacing()};
    const QRectF used =
        captionMetrics.boundingRect(captionArea, Qt::TextWordWrap | Qt::AlignLeft, block.caption);
    painter.setFont(captionFont);
    painter.setPen(ink);
    painter.drawText(captionArea, Qt::TextWordWrap | Qt::AlignLeft, block.caption);

    // Below the symbol, across the block: what is in it.
    struct Line
    {
        QString text;
        QColor colour;
        bool italic{false};
        bool bold{false};
    };

    std::vector<Line> lines;
    const auto now = std::chrono::system_clock::now();
    if (block.fixed == Fixed::Eut) {
        lines.push_back({QStringLiteral("Equipment under test"), colours.muted});
    } else if (block.fixed == Fixed::PeakEmi) {
        lines.push_back({QStringLiteral("Measurement and report"), colours.muted});
    } else if (block.device < 0) {
        lines.push_back(
            {m_forEditing ? QStringLiteral("Not in this set-up") : QStringLiteral("Not entered"),
             colours.muted,
             true});
    } else {
        const auto& device = m_setup.devices[static_cast<std::size_t>(block.device)];
        const auto& item = device.equipment;
        if (item.model.empty()) {
            lines.push_back({QStringLiteral("Model not entered"), colours.muted, true});
        } else {
            lines.push_back({qs(item.model), ink});
        }
        if (device.correction) {
            lines.push_back({shortCorrection(*device.correction), colours.muted});
        } else if (!item.serial.empty()) {
            lines.push_back({QStringLiteral("S/N %1").arg(qs(item.serial)), colours.muted});
        }
        if (!device.inUse) {
            lines.push_back({QStringLiteral("Switched off"), colours.muted, true});
        } else if (item.calibrationExpired(now)) {
            lines.push_back({QStringLiteral("Calibration expired"), colours.warning, false, true});
        } else if (!item.calibrationDue.empty()) {
            lines.push_back(
                {QStringLiteral("Cal. due %1").arg(qs(item.calibrationDue)), colours.muted});
        }
    }
    const QFontMetricsF bodyMetrics{bodyFont};
    double y = std::max(rect.top() + 10.0 + GlyphSize + 4.0, used.bottom() + 3.0);
    const double left = rect.left() + 10.0;
    const double width = rect.width() - 18.0;
    for (const auto& entry : lines) {
        if (y + bodyMetrics.height() > rect.bottom() - 3.0) {
            break;
        }
        QFont font = bodyFont;
        font.setItalic(entry.italic);
        font.setBold(entry.bold);
        painter.setFont(font);
        painter.setPen(entry.colour);
        const QFontMetricsF metrics{font};
        painter.drawText(QPointF{left, y + metrics.ascent()},
                         metrics.elidedText(entry.text, Qt::ElideRight, width));
        y += bodyMetrics.lineSpacing();
    }

    if (hasSwitch(block)) {
        const QRectF track = switchRect(block);
        const double radius = track.height() / 2.0;
        painter.setPen(QPen{block.inUse ? colours.accent : colours.muted, 1.2});
        painter.setBrush(block.inUse ? colours.accent : colours.background);
        painter.drawRoundedRect(track, radius, radius);
        const double knob = track.height() - 5.0;
        const QPointF centre{block.inUse ? track.right() - radius : track.left() + radius,
                             track.center().y()};
        painter.setPen(Qt::NoPen);
        painter.setBrush(block.inUse ? colours.background : colours.muted);
        painter.drawEllipse(centre, knob / 2.0, knob / 2.0);
    }
    painter.restore();
}

QImage SetupDiagram::toImage(double scale, const DiagramColours& colours) const
{
    QImage image{(m_size * scale).toSize(), QImage::Format_ARGB32_Premultiplied};
    image.fill(colours.background);
    QPainter painter{&image};
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    painter.scale(scale, scale);
    paint(painter, colours);
    return image;
}

} // namespace peakemi::reporting
