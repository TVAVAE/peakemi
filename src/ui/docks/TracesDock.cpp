#include <peakemi/core/Session.h>
#include <peakemi/core/TraceMath.h>
#include <peakemi/reporting/CsvExporter.h>
#include <peakemi/ui/SessionPlot.h>
#include <peakemi/ui/TracesDock.h>

#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QStringList>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <chrono>

namespace peakemi::ui {
namespace {

/// Colours for references: apart from the trace (blue or yellow), the limit
/// lines (red and orange), the verified points (green) and the markers
/// (purple or sky blue) in both schemes, and legible on black and on white.
const std::array<QColor, 5> Palette{QColor{0xE0, 0x40, 0xFB},
                                    QColor{0x78, 0x90, 0x9C},
                                    QColor{0x00, 0x97, 0xA7},
                                    QColor{0x8D, 0x6E, 0x63},
                                    QColor{0x7C, 0xB3, 0x42}};

[[nodiscard]] QString qs(const std::string& text)
{
    return QString::fromStdString(text);
}

[[nodiscard]] QIcon swatch(const QColor& colour)
{
    QPixmap pixmap{14, 14};
    pixmap.fill(Qt::transparent);
    QPainter painter{&pixmap};
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(colour);
    painter.drawRoundedRect(QRectF{1.0, 4.0, 12.0, 6.0}, 2.0, 2.0);
    return QIcon{pixmap};
}

[[nodiscard]] QString operationName(TraceOperation operation)
{
    switch (operation) {
        case TraceOperation::Maximum:
            return QObject::tr("Worst case (maximum)");
        case TraceOperation::Minimum:
            return QObject::tr("Minimum");
        case TraceOperation::Average:
            return QObject::tr("Average");
        case TraceOperation::Difference:
            return QObject::tr("Difference A − B");
    }
    return {};
}

} // namespace

TracesDock::TracesDock(QWidget* parent) : QDockWidget{tr("Traces"), parent}
{
    setObjectName(QStringLiteral("tracesDock"));
    auto* content = new QWidget{this};
    auto* layout = new QVBoxLayout{content};

    auto* hint = new QLabel{tr("Reference traces are drawn under the measurement for comparison: "
                               "earlier sessions, exported traces, or the measurement as it is "
                               "now. Tick to show, untick to hide."),
                            content};
    hint->setWordWrap(true);
    hint->setEnabled(false);
    layout->addWidget(hint);

    m_list = new QListWidget{content};
    m_list->setObjectName(QStringLiteral("referenceTraces"));
    m_list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    layout->addWidget(m_list, 1);
    connect(m_list, &QListWidget::itemChanged, this, [this](QListWidgetItem* item) {
        if (m_updating || item == nullptr) {
            return;
        }
        const auto row = static_cast<std::size_t>(m_list->row(item));
        if (row < m_entries.size()) {
            m_entries[row].visible = item->checkState() == Qt::Checked;
            emit overlaysChanged();
        }
    });
    connect(m_list, &QListWidget::itemSelectionChanged, this, &TracesDock::updateEnabledState);

    auto* buttons = new QHBoxLayout;
    auto* load = new QPushButton{tr("Load…"), content};
    load->setToolTip(tr("A session file, or a trace exported as CSV"));
    m_keep = new QPushButton{tr("Keep current"), content};
    m_keep->setToolTip(tr("Keep the measurement on screen as a reference, e.g. before a fix"));
    m_math = new QPushButton{tr("Math…"), content};
    m_math->setToolTip(tr("Combine two traces: worst case, minimum, average or difference"));
    m_export = new QPushButton{tr("Export CSV…"), content};
    m_remove = new QPushButton{tr("Remove"), content};
    buttons->addWidget(load);
    buttons->addWidget(m_keep);
    buttons->addWidget(m_math);
    buttons->addStretch(1);
    buttons->addWidget(m_export);
    buttons->addWidget(m_remove);
    layout->addLayout(buttons);
    connect(load, &QPushButton::clicked, this, &TracesDock::loadReferences);
    connect(m_keep, &QPushButton::clicked, this, &TracesDock::keepCurrent);
    connect(m_math, &QPushButton::clicked, this, &TracesDock::combine);
    connect(m_export, &QPushButton::clicked, this, &TracesDock::exportSelected);
    connect(m_remove, &QPushButton::clicked, this, &TracesDock::removeSelected);

    setWidget(content);
    updateEnabledState();
}

void TracesDock::setCurrentTrace(const std::optional<Trace>& trace)
{
    m_current = trace;
    updateEnabledState();
}

void TracesDock::addReference(Trace trace, const QString& source)
{
    m_entries.push_back(Entry{.trace = std::move(trace), .source = source, .colour = nextColour()});
    refreshList();
    emit overlaysChanged();
}

QVector<PlotSeries> TracesDock::overlaySeries() const
{
    QVector<PlotSeries> series;
    for (std::size_t i = 0; i < m_entries.size(); ++i) {
        const auto& entry = m_entries[i];
        if (!entry.visible) {
            continue;
        }
        PlotSeries overlay;
        overlay.id = QStringLiteral("reference:%1").arg(i);
        overlay.label = qs(entry.trace.label);
        overlay.colour = entry.colour;
        overlay.width = 1.0;
        overlay.points = tracePoints(entry.trace);
        series.append(overlay);
    }
    return series;
}

QColor TracesDock::nextColour() const
{
    return Palette[m_entries.size() % Palette.size()];
}

void TracesDock::refreshList()
{
    m_updating = true;
    m_list->clear();
    for (const auto& entry : m_entries) {
        auto* item = new QListWidgetItem{swatch(entry.colour), qs(entry.trace.label), m_list};
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(entry.visible ? Qt::Checked : Qt::Unchecked);
        item->setToolTip(entry.source);
    }
    m_updating = false;
    updateEnabledState();
}

void TracesDock::updateEnabledState()
{
    const int selected = static_cast<int>(m_list->selectedItems().size());
    m_keep->setEnabled(m_current.has_value() && !m_current->isEmpty());
    m_math->setEnabled(m_entries.size() + (m_current ? 1 : 0) >= 2);
    m_export->setEnabled(selected == 1);
    m_remove->setEnabled(selected > 0);
}

void TracesDock::loadReferences()
{
    const auto paths = QFileDialog::getOpenFileNames(
        this,
        tr("Load reference traces"),
        QString{},
        tr("Sessions and traces (*.json *.csv);;PeakEmi sessions (*.json);;Trace CSV (*.csv)"));
    QStringList problems;
    for (const auto& path : paths) {
        auto trace = trace_io::load(path);
        if (!trace) {
            problems << QStringLiteral("%1: %2").arg(
                QFileInfo{path}.fileName(), QString::fromStdString(trace.error().message()));
            continue;
        }
        if (trace->label.rfind("Phase 1", 0) == 0) {
            // A session's own label says nothing once it is one of several.
            trace->label = QFileInfo{path}.completeBaseName().toStdString();
        }
        m_entries.push_back(
            Entry{.trace = std::move(*trace), .source = path, .colour = nextColour()});
    }
    if (!problems.isEmpty()) {
        QMessageBox::warning(this, tr("Load reference traces"), problems.join(QLatin1Char('\n')));
    }
    if (!paths.isEmpty()) {
        refreshList();
        emit overlaysChanged();
    }
}

void TracesDock::keepCurrent()
{
    if (!m_current || m_current->isEmpty()) {
        return;
    }
    auto trace = *m_current;
    trace.label = tr("Reference %1")
                      .arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss")))
                      .toStdString();
    addReference(std::move(trace), tr("Kept from the measurement on screen"));
    emit statusMessage(tr("The measurement on screen is kept as a reference."));
}

void TracesDock::combine()
{
    // The operands: the measurement on screen, then every reference.
    std::vector<const Trace*> operands;
    QStringList names;
    if (m_current && !m_current->isEmpty()) {
        operands.push_back(&*m_current);
        names << tr("Measurement on screen");
    }
    for (const auto& entry : m_entries) {
        operands.push_back(&entry.trace);
        names << qs(entry.trace.label);
    }
    if (operands.size() < 2) {
        return;
    }

    QDialog dialog{this};
    dialog.setWindowTitle(tr("Trace math"));
    auto* form = new QFormLayout{&dialog};
    auto* first = new QComboBox{&dialog};
    auto* second = new QComboBox{&dialog};
    first->addItems(names);
    second->addItems(names);
    second->setCurrentIndex(1);
    auto* operation = new QComboBox{&dialog};
    for (const auto value : {TraceOperation::Maximum,
                             TraceOperation::Minimum,
                             TraceOperation::Average,
                             TraceOperation::Difference})
    {
        operation->addItem(operationName(value), static_cast<int>(value));
    }
    form->addRow(tr("Trace A"), first);
    form->addRow(tr("Operation"), operation);
    form->addRow(tr("Trace B"), second);
    auto* hint = new QLabel{tr("The result is computed on A's frequency points, where both "
                               "traces have data. The worst case of L and N is the maximum."),
                            &dialog};
    hint->setWordWrap(true);
    form->addRow(hint);
    auto* buttons = new QDialogButtonBox{QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog};
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    const auto& a = *operands[static_cast<std::size_t>(first->currentIndex())];
    const auto& b = *operands[static_cast<std::size_t>(second->currentIndex())];
    auto result =
        combineTraces(a, b, static_cast<TraceOperation>(operation->currentData().toInt()));
    if (!result) {
        QMessageBox::warning(
            this, tr("Trace math"), QString::fromStdString(result.error().message()));
        return;
    }
    addReference(std::move(*result),
                 tr("%1 of '%2' and '%3'")
                     .arg(operation->currentText(), first->currentText(), second->currentText()));
}

void TracesDock::exportSelected()
{
    const auto selected = m_list->selectedItems();
    if (selected.size() != 1) {
        return;
    }
    const auto row = static_cast<std::size_t>(m_list->row(selected.front()));
    if (row >= m_entries.size()) {
        return;
    }
    const auto& trace = m_entries[row].trace;
    auto path = QFileDialog::getSaveFileName(this,
                                             tr("Export trace"),
                                             qs(trace.label) + QStringLiteral(".csv"),
                                             tr("CSV files (*.csv)"));
    if (path.isEmpty()) {
        return;
    }
    if (auto status = reporting::csv::writeTrace(Session{}, trace, path); !status) {
        QMessageBox::warning(
            this, tr("Export trace"), QString::fromStdString(status.error().message()));
        return;
    }
    emit statusMessage(tr("Trace exported to %1").arg(path));
}

void TracesDock::removeSelected()
{
    std::vector<int> rows;
    for (auto* item : m_list->selectedItems()) {
        rows.push_back(m_list->row(item));
    }
    std::sort(rows.rbegin(), rows.rend());
    for (const int row : rows) {
        if (row >= 0 && static_cast<std::size_t>(row) < m_entries.size()) {
            m_entries.erase(m_entries.begin() + row);
        }
    }
    refreshList();
    emit overlaysChanged();
}

} // namespace peakemi::ui
