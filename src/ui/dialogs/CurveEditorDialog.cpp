#include <peakemi/core/LimitLineIo.h>
#include <peakemi/ui/CurveEditorDialog.h>
#include <peakemi/ui/PainterPlotBackend.h>
#include <peakemi/ui/SessionPlot.h>

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <optional>
#include <tuple>
#include <vector>

namespace peakemi::ui {
namespace {

enum Column
{
    ColumnFrequency,
    ColumnValue,
    ColumnInterpolation
};

[[nodiscard]] QString qs(std::string_view text)
{
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

/// A number typed with a point or a comma.
[[nodiscard]] std::optional<double> parse(const QString& text)
{
    QString normalised = text.trimmed();
    normalised.replace(QLatin1Char(','), QLatin1Char('.'));
    bool ok = false;
    const double value = normalised.toDouble(&ok);
    if (!ok || !std::isfinite(value)) {
        return std::nullopt;
    }
    return value;
}

[[nodiscard]] QString interpolationName(Interpolation interpolation)
{
    switch (interpolation) {
        case Interpolation::LogFrequency:
            return QObject::tr("Log frequency");
        case Interpolation::Linear:
            return QObject::tr("Linear");
        case Interpolation::Step:
            return QObject::tr("Step");
    }
    return {};
}

[[nodiscard]] QString kindName(CorrectionKind kind)
{
    switch (kind) {
        case CorrectionKind::AntennaFactor:
            return QObject::tr("Antenna factor");
        case CorrectionKind::CableLoss:
            return QObject::tr("Cable loss");
        case CorrectionKind::LisnFactor:
            return QObject::tr("LISN factor");
        case CorrectionKind::Attenuator:
            return QObject::tr("Attenuation");
        case CorrectionKind::AmplifierGain:
            return QObject::tr("Amplifier gain");
        case CorrectionKind::Other:
            break;
    }
    return QObject::tr("Other");
}

struct Row
{
    Hertz frequency;
    double value;
    Interpolation interpolation;
};

} // namespace

CurveEditorDialog::CurveEditorDialog(Mode mode, QWidget* parent) : QDialog{parent}, m_mode{mode}
{
    setWindowTitle(mode == Mode::Limit ? tr("Limit line") : tr("Correction table"));
    setObjectName(QStringLiteral("curveEditor"));
    resize(980, 600);
    buildUi();
}

void CurveEditorDialog::buildUi()
{
    auto* layout = new QVBoxLayout{this};
    auto* form = new QFormLayout;
    m_name = new QLineEdit{this};
    form->addRow(tr("Name"), m_name);
    connect(m_name, &QLineEdit::textEdited, this, &CurveEditorDialog::refreshPreview);

    if (m_mode == Mode::Limit) {
        m_standard = new QLineEdit{this};
        m_standard->setPlaceholderText(tr("e.g. CISPR 32:2015, Table A.4"));
        m_detector = new QComboBox{this};
        for (const auto detector :
             {Detector::QuasiPeak, Detector::Average, Detector::Peak, Detector::Rms})
        {
            m_detector->addItem(qs(detectorKey(detector)), static_cast<int>(detector));
        }
        m_unit = new QComboBox{this};
        for (const auto unit : {AmplitudeUnit::dBuV,
                                AmplitudeUnit::dBuV_per_m,
                                AmplitudeUnit::dBuA,
                                AmplitudeUnit::dBm})
        {
            m_unit->addItem(qs(amplitudeUnitKey(unit)), static_cast<int>(unit));
        }
        form->addRow(tr("Standard"), m_standard);
        form->addRow(tr("Detector"), m_detector);
        form->addRow(tr("Unit"), m_unit);
        connect(
            m_detector, &QComboBox::currentIndexChanged, this, &CurveEditorDialog::refreshPreview);
        connect(m_unit, &QComboBox::currentIndexChanged, this, [this] {
            m_points->horizontalHeaderItem(ColumnValue)
                ->setText(tr("Limit [%1]").arg(m_unit->currentText()));
            refreshPreview();
        });
    } else {
        m_kind = new QComboBox{this};
        for (const auto kind : {CorrectionKind::AntennaFactor,
                                CorrectionKind::CableLoss,
                                CorrectionKind::LisnFactor,
                                CorrectionKind::Attenuator,
                                CorrectionKind::AmplifierGain,
                                CorrectionKind::Other})
        {
            m_kind->addItem(kindName(kind), static_cast<int>(kind));
        }
        m_enabled = new QCheckBox{tr("Apply this correction"), this};
        m_enabled->setChecked(true);
        form->addRow(tr("Type"), m_kind);
        form->addRow(QString{}, m_enabled);
        auto* sign = new QLabel{tr("Enter losses, factors and attenuation as positive numbers; "
                                   "they are added to the reading. A gain is also positive and "
                                   "is subtracted."),
                                this};
        sign->setWordWrap(true);
        sign->setEnabled(false);
        form->addRow(sign);
    }
    layout->addLayout(form);

    // The points on the left, what they draw on the right.
    auto* body = new QHBoxLayout;
    auto* table = new QVBoxLayout;
    m_points = new QTableWidget{0, m_mode == Mode::Limit ? 3 : 2, this};
    m_points->setMinimumWidth(m_mode == Mode::Limit ? 430 : 300);
    QStringList headers{tr("Frequency [MHz]"),
                        m_mode == Mode::Limit
                            ? tr("Limit [%1]").arg(qs(amplitudeUnitKey(AmplitudeUnit::dBuV)))
                            : tr("Value [dB]")};
    if (m_mode == Mode::Limit) {
        headers << tr("To the next point");
    }
    m_points->setHorizontalHeaderLabels(headers);
    m_points->verticalHeader()->setVisible(false);
    m_points->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_points->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->addWidget(m_points, 1);
    connect(m_points, &QTableWidget::itemChanged, this, [this] {
        if (!m_updating) {
            refreshPreview();
        }
    });

    auto* tableButtons = new QHBoxLayout;
    auto* add = new QPushButton{tr("Add point"), this};
    auto* remove = new QPushButton{tr("Remove"), this};
    auto* sort = new QPushButton{tr("Sort"), this};
    auto* import = new QPushButton{tr("Import…"), this};
    auto* save = new QPushButton{tr("Save as…"), this};
    tableButtons->addWidget(add);
    tableButtons->addWidget(remove);
    tableButtons->addWidget(sort);
    tableButtons->addStretch(1);
    table->addLayout(tableButtons);
    body->addLayout(table, 0);
    connect(add, &QPushButton::clicked, this, [this] {
        // After the last point, a decade further, as a starting value.
        double frequency = 30.0;
        double value = 0.0;
        const int last = m_points->rowCount() - 1;
        if (last >= 0) {
            frequency = parse(m_points->item(last, ColumnFrequency)->text()).value_or(30.0) * 2.0;
            value = parse(m_points->item(last, ColumnValue)->text()).value_or(0.0);
        }
        addRow(frequency, value, Interpolation::LogFrequency);
        m_points->editItem(m_points->item(m_points->rowCount() - 1, ColumnFrequency));
        refreshPreview();
    });
    connect(remove, &QPushButton::clicked, this, &CurveEditorDialog::removeSelectedRows);
    connect(sort, &QPushButton::clicked, this, &CurveEditorDialog::sortRows);
    connect(import, &QPushButton::clicked, this, &CurveEditorDialog::importFile);
    connect(save, &QPushButton::clicked, this, &CurveEditorDialog::saveFile);

    m_preview = new PainterPlotBackend{this};
    m_preview->setMinimumSize(360, 260);
    m_preview->setLogarithmicFrequency(true);
    body->addWidget(m_preview, 1);
    layout->addLayout(body, 1);

    m_problem = new QLabel{this};
    m_problem->setWordWrap(true);
    layout->addWidget(m_problem);

    auto* bottom = new QHBoxLayout;
    bottom->addWidget(import);
    bottom->addWidget(save);
    bottom->addStretch(1);
    auto* buttons = new QDialogButtonBox{QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this};
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Use in run"));
    bottom->addWidget(buttons);
    layout->addLayout(bottom);
    connect(buttons, &QDialogButtonBox::accepted, this, &CurveEditorDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &CurveEditorDialog::reject);
}

void CurveEditorDialog::addRow(double megahertz, double value, Interpolation interpolation)
{
    const bool updating = m_updating;
    m_updating = true;
    const int row = m_points->rowCount();
    m_points->insertRow(row);
    m_points->setItem(
        row, ColumnFrequency, new QTableWidgetItem{QString::number(megahertz, 'g', 10)});
    m_points->setItem(row, ColumnValue, new QTableWidgetItem{QString::number(value, 'f', 2)});
    if (m_mode == Mode::Limit) {
        auto* combo = new QComboBox{m_points};
        for (const auto mode :
             {Interpolation::LogFrequency, Interpolation::Linear, Interpolation::Step})
        {
            combo->addItem(interpolationName(mode), static_cast<int>(mode));
        }
        combo->setCurrentIndex(combo->findData(static_cast<int>(interpolation)));
        connect(combo, &QComboBox::currentIndexChanged, this, &CurveEditorDialog::refreshPreview);
        m_points->setCellWidget(row, ColumnInterpolation, combo);
    }
    m_updating = updating;
}

void CurveEditorDialog::setLimit(const LimitLine& limit)
{
    m_limitBase = limit;
    m_updating = true;
    m_name->setText(qs(limit.name));
    m_standard->setText(qs(limit.standard));
    m_detector->setCurrentIndex(
        std::max(0, m_detector->findData(static_cast<int>(limit.detector))));
    m_unit->setCurrentIndex(std::max(0, m_unit->findData(static_cast<int>(limit.unit))));
    m_points->setRowCount(0);
    for (const auto& point : limit.points) {
        addRow(toMegahertz(point.frequency), point.amplitude, point.interpolationToNext);
    }
    m_updating = false;
    refreshPreview();
}

void CurveEditorDialog::setCorrection(const CorrectionTable& table)
{
    m_correctionBase = table;
    m_updating = true;
    m_name->setText(qs(table.name));
    m_kind->setCurrentIndex(std::max(0, m_kind->findData(static_cast<int>(table.kind))));
    m_enabled->setChecked(table.enabled);
    m_points->setRowCount(0);
    for (const auto& [frequency, value] : table.points) {
        addRow(toMegahertz(frequency), value, Interpolation::LogFrequency);
    }
    m_updating = false;
    refreshPreview();
}

LimitLine CurveEditorDialog::limit() const
{
    LimitLine limit = m_limitBase;
    limit.builtIn = false;
    limit.name = m_name->text().trimmed().toStdString();
    if (m_standard != nullptr) {
        limit.standard = m_standard->text().trimmed().toStdString();
        limit.detector = static_cast<Detector>(m_detector->currentData().toInt());
        limit.unit = static_cast<AmplitudeUnit>(m_unit->currentData().toInt());
    }
    limit.points.clear();
    for (int row = 0; row < m_points->rowCount(); ++row) {
        const auto frequency = parse(m_points->item(row, ColumnFrequency)->text());
        const auto value = parse(m_points->item(row, ColumnValue)->text());
        if (!frequency || !value) {
            continue;
        }
        auto interpolation = Interpolation::LogFrequency;
        if (const auto* combo =
                qobject_cast<QComboBox*>(m_points->cellWidget(row, ColumnInterpolation)))
        {
            interpolation = static_cast<Interpolation>(combo->currentData().toInt());
        }
        limit.points.push_back(LimitPoint{.frequency = megahertz(*frequency),
                                          .amplitude = *value,
                                          .interpolationToNext = interpolation});
    }
    return limit;
}

CorrectionTable CurveEditorDialog::correction() const
{
    CorrectionTable table = m_correctionBase;
    table.name = m_name->text().trimmed().toStdString();
    if (m_kind != nullptr) {
        table.kind = static_cast<CorrectionKind>(m_kind->currentData().toInt());
        table.enabled = m_enabled->isChecked();
    }
    table.points.clear();
    for (int row = 0; row < m_points->rowCount(); ++row) {
        const auto frequency = parse(m_points->item(row, ColumnFrequency)->text());
        const auto value = parse(m_points->item(row, ColumnValue)->text());
        if (frequency && value) {
            table.points.emplace_back(megahertz(*frequency), *value);
        }
    }
    return table;
}

QString CurveEditorDialog::problem() const
{
    if (m_name->text().trimmed().isEmpty()) {
        return tr("Give it a name.");
    }
    for (int row = 0; row < m_points->rowCount(); ++row) {
        if (!parse(m_points->item(row, ColumnFrequency)->text()) ||
            !parse(m_points->item(row, ColumnValue)->text()))
        {
            return tr("Row %1 is not two numbers.").arg(row + 1);
        }
        if (parse(m_points->item(row, ColumnFrequency)->text()).value_or(0.0) <= 0.0) {
            return tr("Row %1 needs a frequency above 0.").arg(row + 1);
        }
    }
    const auto status = m_mode == Mode::Limit ? limit().validate() : correction().validate();
    return status ? QString{} : QString::fromStdString(status.error().message());
}

void CurveEditorDialog::refreshPreview()
{
    if (m_updating) {
        return;
    }
    const auto why = problem();
    m_problem->setText(why.isEmpty() ? QString{}
                                     : QStringLiteral("<b>%1</b>").arg(why.toHtmlEscaped()));

    m_preview->clearSeries();
    PlotSeries series;
    series.id = QStringLiteral("curve");
    series.width = 1.6;
    if (m_mode == Mode::Limit) {
        const auto line = limit();
        if (line.points.size() >= 2 && line.validate()) {
            series = limitSeries(line, line.coverage(), 0.0);
            m_preview->setAmplitudeLabel(qs(amplitudeUnitKey(line.unit)));
        }
    } else {
        const auto table = correction();
        series.label = QString::fromStdString(table.name);
        series.role = PlotRole::Trace;
        for (const auto& [frequency, value] : table.points) {
            series.points.append(QPointF{static_cast<double>(frequency.value()), value});
        }
        std::sort(series.points.begin(), series.points.end(), [](const auto& a, const auto& b) {
            return a.x() < b.x();
        });
        m_preview->setAmplitudeLabel(QStringLiteral("dB"));
    }
    if (series.points.size() >= 2) {
        m_preview->setSeries(series);
        m_preview->autoScale();
    }
}

void CurveEditorDialog::removeSelectedRows()
{
    std::vector<int> rows;
    for (const auto& range : m_points->selectedRanges()) {
        for (int row = range.topRow(); row <= range.bottomRow(); ++row) {
            rows.push_back(row);
        }
    }
    std::sort(rows.rbegin(), rows.rend());
    for (const int row : rows) {
        m_points->removeRow(row);
    }
    refreshPreview();
}

void CurveEditorDialog::sortRows()
{
    std::vector<Row> rows;
    for (int row = 0; row < m_points->rowCount(); ++row) {
        const auto frequency = parse(m_points->item(row, ColumnFrequency)->text());
        const auto value = parse(m_points->item(row, ColumnValue)->text());
        if (!frequency || !value) {
            continue;
        }
        auto interpolation = Interpolation::LogFrequency;
        if (const auto* combo =
                qobject_cast<QComboBox*>(m_points->cellWidget(row, ColumnInterpolation)))
        {
            interpolation = static_cast<Interpolation>(combo->currentData().toInt());
        }
        rows.push_back(Row{megahertz(*frequency), *value, interpolation});
    }
    // Stable, so two points at one frequency -- a vertical step -- keep their order.
    std::stable_sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) {
        return a.frequency < b.frequency;
    });
    m_updating = true;
    m_points->setRowCount(0);
    for (const auto& row : rows) {
        addRow(toMegahertz(row.frequency), row.value, row.interpolation);
    }
    m_updating = false;
    refreshPreview();
}

void CurveEditorDialog::importFile()
{
    const auto path = QFileDialog::getOpenFileName(
        this, tr("Import"), QString{}, tr("CSV or JSON (*.csv *.json);;All files (*)"));
    if (path.isEmpty()) {
        return;
    }
    if (m_mode == Mode::Limit) {
        auto loaded = limit_io::load(path);
        if (!loaded) {
            QMessageBox::warning(
                this, tr("Import"), QString::fromStdString(loaded.error().message()));
            return;
        }
        if (loaded->name.empty()) {
            loaded->name = QFileInfo{path}.completeBaseName().toStdString();
        }
        setLimit(*loaded);
    } else {
        auto loaded = correction_io::load(path);
        if (!loaded) {
            QMessageBox::warning(
                this, tr("Import"), QString::fromStdString(loaded.error().message()));
            return;
        }
        if (loaded->name.empty()) {
            loaded->name = QFileInfo{path}.completeBaseName().toStdString();
        }
        setCorrection(*loaded);
    }
}

void CurveEditorDialog::saveFile()
{
    if (const auto why = problem(); !why.isEmpty()) {
        QMessageBox::warning(this, tr("Save"), why);
        return;
    }
    auto path = QFileDialog::getSaveFileName(this,
                                             tr("Save as"),
                                             m_name->text().trimmed() + QStringLiteral(".csv"),
                                             tr("CSV (*.csv);;JSON (*.json)"));
    if (path.isEmpty()) {
        return;
    }
    const auto status = m_mode == Mode::Limit ? limit_io::save(limit(), path)
                                              : correction_io::save(correction(), path);
    if (!status) {
        QMessageBox::warning(this, tr("Save"), QString::fromStdString(status.error().message()));
    }
}

void CurveEditorDialog::accept()
{
    if (const auto why = problem(); !why.isEmpty()) {
        m_problem->setText(QStringLiteral("<b>%1</b>").arg(why.toHtmlEscaped()));
        return;
    }
    QDialog::accept();
}

} // namespace peakemi::ui
