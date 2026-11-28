#include <peakemi/core/Campaign.h>
#include <peakemi/core/LimitLineIo.h>
#include <peakemi/core/SetupLibrary.h>
#include <peakemi/reporting/SetupSheetRenderer.h>
#include <peakemi/ui/CurveEditorDialog.h>
#include <peakemi/ui/SetupDiagramView.h>
#include <peakemi/ui/SetupLibraryDialog.h>

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPalette>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QSplitter>
#include <QStringList>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <chrono>
#include <initializer_list>
#include <utility>

namespace peakemi::ui {
namespace {

const QString DirectoryKey = QStringLiteral("setups/directory");

[[nodiscard]] QString qs(std::string_view text)
{
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

[[nodiscard]] std::string str(const QString& text)
{
    return text.toStdString();
}

[[nodiscard]] QString kindName(TestKind kind)
{
    switch (kind) {
        case TestKind::Conducted:
            return QObject::tr("Conducted");
        case TestKind::Radiated:
            return QObject::tr("Radiated");
        case TestKind::Other:
            break;
    }
    return QObject::tr("Other");
}

/// A correction in one line, as the device table shows it.
[[nodiscard]] QString correctionText(const std::optional<CorrectionTable>& table)
{
    if (!table) {
        return QStringLiteral("–");
    }
    auto text = QStringLiteral("%1: %2").arg(reporting::correctionKindName(table->kind),
                                             reporting::describeCorrection(*table));
    if (!table->enabled) {
        text += QObject::tr(" (disabled)");
    }
    return text.replace(QStringLiteral("&ndash;"), QStringLiteral("–"));
}

/// A file name made from a set-up's name, for export and print.
[[nodiscard]] QString fileStem(const TestSetup& setup)
{
    QString stem = qs(setup.name).trimmed();
    stem.replace(QRegularExpression{QStringLiteral(R"([^\w\- ]+)")}, QStringLiteral(" "));
    stem = stem.simplified().replace(QLatin1Char(' '), QLatin1Char('-'));
    return stem.isEmpty() ? QStringLiteral("setup") : stem;
}

/// What a device role usually corrects for, offered when it is added.
[[nodiscard]] std::optional<CorrectionTable> defaultCorrectionFor(const QString& role)
{
    return newDeviceFor(str(role)).correction;
}

/// The kind of table a device role calls for, for a table written from scratch.
[[nodiscard]] CorrectionKind correctionKindForRole(const QString& role)
{
    if (role == QObject::tr("Antenna")) {
        return CorrectionKind::AntennaFactor;
    }
    if (role == QObject::tr("LISN")) {
        return CorrectionKind::LisnFactor;
    }
    if (role == QObject::tr("RF cable")) {
        return CorrectionKind::CableLoss;
    }
    if (role == QObject::tr("Attenuator") || role == QObject::tr("Transient limiter")) {
        return CorrectionKind::Attenuator;
    }
    if (role == QObject::tr("Preamplifier")) {
        return CorrectionKind::AmplifierGain;
    }
    return CorrectionKind::Other;
}

enum DeviceColumn
{
    ColumnRole,
    ColumnModel,
    ColumnSerial,
    ColumnCalibration,
    ColumnCorrection,
    ColumnNotes,
    DeviceColumnCount
};

} // namespace

QString setupLibraryDirectory()
{
    const QSettings settings;
    return settings.value(DirectoryKey, SetupLibrary::defaultDirectory()).toString();
}

SetupLibraryDialog::SetupLibraryDialog(QWidget* parent) : QDialog{parent}
{
    setWindowTitle(tr("Test set-ups"));
    setObjectName(QStringLiteral("setupLibraryDialog"));
    resize(1180, 780);
    buildUi();
    reload();
}

SetupLibraryDialog::~SetupLibraryDialog() = default;

void SetupLibraryDialog::buildUi()
{
    auto* layout = new QVBoxLayout{this};

    auto* folderRow = new QHBoxLayout;
    m_folder = new QLabel{this};
    m_folder->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto* changeFolder = new QPushButton{tr("Change folder…"), this};
    changeFolder->setToolTip(
        tr("Keep the set-ups in a shared folder so everyone in the lab uses the same list."));
    folderRow->addWidget(m_folder, 1);
    folderRow->addWidget(changeFolder);
    layout->addLayout(folderRow);
    connect(changeFolder, &QPushButton::clicked, this, &SetupLibraryDialog::chooseFolder);

    auto* splitter = new QSplitter{Qt::Horizontal, this};
    layout->addWidget(splitter, 1);

    // --- left: the library --------------------------------------------------
    auto* left = new QWidget{splitter};
    auto* leftLayout = new QVBoxLayout{left};
    leftLayout->setContentsMargins(0, 0, 0, 0);
    m_list = new QListWidget{left};
    leftLayout->addWidget(m_list, 1);
    connect(m_list, &QListWidget::currentRowChanged, this, [this](int row) {
        if (m_updating || row == m_current) {
            return;
        }
        if (!confirmLeave()) {
            m_updating = true;
            m_list->setCurrentRow(m_current);
            m_updating = false;
            return;
        }
        selectRow(row);
    });

    m_new = new QPushButton{tr("New"), left};
    auto* newMenu = new QMenu{m_new};
    for (const auto method : setupMethods()) {
        newMenu->addAction(qs(setupMethodName(method)), this, [this, method] { newSetup(method); });
    }
    m_new->setMenu(newMenu);
    m_duplicate = new QPushButton{tr("Duplicate"), left};
    m_delete = new QPushButton{tr("Delete"), left};
    auto* import = new QPushButton{tr("Import…"), left};
    m_export = new QPushButton{tr("Export…"), left};
    auto* listButtons = new QHBoxLayout;
    listButtons->addWidget(m_new);
    listButtons->addWidget(m_duplicate);
    listButtons->addWidget(m_delete);
    auto* fileButtons = new QHBoxLayout;
    fileButtons->addWidget(import);
    fileButtons->addWidget(m_export);
    leftLayout->addLayout(listButtons);
    leftLayout->addLayout(fileButtons);
    connect(m_duplicate, &QPushButton::clicked, this, &SetupLibraryDialog::duplicateSetup);
    connect(m_delete, &QPushButton::clicked, this, &SetupLibraryDialog::deleteSetup);
    connect(import, &QPushButton::clicked, this, &SetupLibraryDialog::importSetups);
    connect(m_export, &QPushButton::clicked, this, &SetupLibraryDialog::exportCurrent);

    // --- right: the selected set-up ---------------------------------------
    auto* right = new QWidget{splitter};
    auto* rightLayout = new QVBoxLayout{right};
    rightLayout->setContentsMargins(6, 0, 0, 0);
    auto* form = new QFormLayout;
    m_name = new QLineEdit{right};
    m_method = new QComboBox{right};
    for (const auto method : setupMethods()) {
        m_method->addItem(qs(setupMethodName(method)), static_cast<int>(method));
    }
    m_method->setToolTip(tr("How the equipment under test is coupled to the analyzer: it "
                            "decides the blocks the diagram offers."));
    m_description = new QPlainTextEdit{right};
    m_description->setPlaceholderText(
        tr("Layout, distances, grounding, cable routing: what someone rebuilding this bench "
           "needs to know."));
    m_description->setMaximumHeight(80);
    form->addRow(tr("Name"), m_name);
    form->addRow(tr("Method"), m_method);
    form->addRow(tr("Description"), m_description);
    rightLayout->addLayout(form);
    connect(m_name, &QLineEdit::textEdited, this, [this](const QString& text) {
        if (auto* setup = current(); setup != nullptr && !m_updating) {
            setup->name = str(text);
            if (auto* item = m_list->item(m_current)) {
                item->setText(text);
            }
            markDirty();
        }
    });
    connect(m_method, &QComboBox::currentIndexChanged, this, [this] {
        if (auto* setup = current(); setup != nullptr && !m_updating) {
            setup->method = static_cast<SetupMethod>(m_method->currentData().toInt());
            setup->kind = kindOf(setup->method);
            markDirty();
            refreshDevices();
            refreshSummary();
        }
    });
    connect(m_description, &QPlainTextEdit::textChanged, this, [this] {
        if (auto* setup = current(); setup != nullptr && !m_updating) {
            setup->description = str(m_description->toPlainText());
            markDirty();
        }
    });

    rightLayout->addWidget(new QLabel{tr("<b>Set-up</b> — click a block to pick its device; its "
                                         "switch takes it in or out of the set-up"),
                                      right});
    m_diagram = new SetupDiagramView{right};
    rightLayout->addWidget(m_diagram);
    connect(m_diagram, &SetupDiagramView::deviceSelected, this, [this](int device) {
        m_devices->selectRow(device);
    });
    connect(m_diagram, &SetupDiagramView::deviceActivated, this, [this](int device) {
        m_devices->selectRow(device);
        m_devices->editItem(m_devices->item(device, ColumnModel));
    });
    connect(m_diagram, &SetupDiagramView::slotSwitched, this, &SetupLibraryDialog::switchSlot);
    connect(m_diagram, &SetupDiagramView::deviceSwitched, this, [this](int device, bool inUse) {
        auto* setup = current();
        if (setup == nullptr || device < 0 ||
            static_cast<std::size_t>(device) >= setup->devices.size())
        {
            return;
        }
        setup->devices[static_cast<std::size_t>(device)].inUse = inUse;
        markDirty();
        refreshDevices();
        refreshSummary();
    });

    rightLayout->addWidget(
        new QLabel{tr("<b>Devices</b> — in signal order; untick one to switch it off"), right});
    m_devices = new QTableWidget{0, DeviceColumnCount, right};
    m_devices->setHorizontalHeaderLabels({tr("Role"),
                                          tr("Model"),
                                          tr("Serial number"),
                                          tr("Calibration due"),
                                          tr("Correction"),
                                          tr("Notes")});
    m_devices->verticalHeader()->setVisible(false);
    m_devices->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_devices->setSelectionMode(QAbstractItemView::SingleSelection);
    m_devices->horizontalHeader()->setStretchLastSection(true);
    m_devices->setColumnWidth(ColumnRole, 120);
    m_devices->setColumnWidth(ColumnModel, 150);
    m_devices->setColumnWidth(ColumnSerial, 100);
    m_devices->setColumnWidth(ColumnCalibration, 110);
    m_devices->setColumnWidth(ColumnCorrection, 210);
    m_devices->setToolTip(tr("Calibration due as YYYY-MM-DD. Double-click to edit; set the "
                             "correction with the Correction button."));
    rightLayout->addWidget(m_devices, 1);
    connect(m_devices, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* item) {
        auto* setup = current();
        if (m_updating || setup == nullptr || item == nullptr) {
            return;
        }
        const auto row = static_cast<std::size_t>(item->row());
        if (row >= setup->devices.size()) {
            return;
        }
        auto& device = setup->devices[row];
        const auto value = str(item->text().trimmed());
        switch (item->column()) {
            case ColumnRole:
                device.equipment.role = value;
                device.inUse = item->checkState() != Qt::Unchecked;
                break;
            case ColumnModel:
                device.equipment.model = value;
                break;
            case ColumnSerial:
                device.equipment.serial = value;
                break;
            case ColumnCalibration:
                device.equipment.calibrationDue = value;
                break;
            case ColumnNotes:
                device.notes = value;
                break;
            default:
                return;
        }
        markDirty();
        // A new role or a switch moves the device in the diagram; a new date
        // can change its colour.
        if (item->column() == ColumnCalibration || item->column() == ColumnRole) {
            refreshDevices();
            refreshSummary();
        } else {
            refreshDiagram();
        }
    });
    connect(m_devices, &QTableWidget::itemSelectionChanged, this, [this] {
        m_diagram->setSelectedDevice(m_devices->currentRow());
        updateEnabledState();
    });

    auto* deviceButtons = new QHBoxLayout;
    m_addDevice = new QPushButton{tr("Add device"), right};
    auto* deviceMenu = new QMenu{m_addDevice};
    for (const auto& role : {tr("Spectrum analyzer"),
                             tr("LISN"),
                             tr("Transient limiter"),
                             tr("Attenuator"),
                             tr("Preamplifier"),
                             tr("Antenna"),
                             tr("RF cable"),
                             tr("Current probe"),
                             tr("TEM cell"),
                             tr("Near-field probe"),
                             tr("Isolation transformer"),
                             tr("Power supply"),
                             tr("Other")})
    {
        deviceMenu->addAction(role, this, [this, role] { addDevice(role); });
    }
    m_addDevice->setMenu(deviceMenu);
    m_removeDevice = new QPushButton{tr("Remove"), right};
    m_up = new QPushButton{tr("Up"), right};
    m_down = new QPushButton{tr("Down"), right};
    m_correction = new QPushButton{tr("Correction"), right};
    auto* correctionMenu = new QMenu{m_correction};
    correctionMenu->addAction(tr("Fixed value…"), this, &SetupLibraryDialog::setFixedCorrection);
    correctionMenu->addAction(tr("Edit table…"), this, &SetupLibraryDialog::editCorrection);
    correctionMenu->addAction(
        tr("Import table (CSV or JSON)…"), this, &SetupLibraryDialog::importCorrection);
    correctionMenu->addAction(tr("No correction"), this, &SetupLibraryDialog::removeCorrection);
    m_correction->setMenu(correctionMenu);
    deviceButtons->addWidget(m_addDevice);
    deviceButtons->addWidget(m_removeDevice);
    deviceButtons->addWidget(m_up);
    deviceButtons->addWidget(m_down);
    deviceButtons->addStretch(1);
    deviceButtons->addWidget(m_correction);
    rightLayout->addLayout(deviceButtons);
    connect(m_removeDevice, &QPushButton::clicked, this, &SetupLibraryDialog::removeDevice);
    connect(m_up, &QPushButton::clicked, this, [this] { moveDevice(-1); });
    connect(m_down, &QPushButton::clicked, this, [this] { moveDevice(1); });

    m_summary = new QLabel{right};
    m_summary->setWordWrap(true);
    m_summary->setTextFormat(Qt::RichText);
    rightLayout->addWidget(m_summary);

    splitter->setStretchFactor(0, 2);
    splitter->setStretchFactor(1, 7);

    // --- bottom --------------------------------------------------------------
    auto* bottom = new QHBoxLayout;
    m_print = new QPushButton{tr("Print sheet…"), this};
    m_print->setToolTip(tr("A one-page PDF of this set-up, to keep with the bench."));
    m_save = new QPushButton{tr("Save"), this};
    m_apply = new QPushButton{tr("Apply"), this};
    m_apply->setProperty("primary", true);
    auto* close = new QPushButton{tr("Close"), this};
    bottom->addWidget(m_print);
    bottom->addStretch(1);
    bottom->addWidget(m_save);
    bottom->addWidget(m_apply);
    bottom->addWidget(close);
    layout->addLayout(bottom);
    connect(m_print, &QPushButton::clicked, this, &SetupLibraryDialog::printSheet);
    connect(m_save, &QPushButton::clicked, this, &SetupLibraryDialog::saveCurrent);
    connect(m_apply, &QPushButton::clicked, this, &SetupLibraryDialog::applyCurrent);
    connect(close, &QPushButton::clicked, this, &SetupLibraryDialog::reject);
}

void SetupLibraryDialog::setApplyTarget(const QString& target)
{
    m_apply->setVisible(!target.isEmpty());
    m_apply->setText(tr("Apply to %1").arg(target));
}

void SetupLibraryDialog::setReportTemplate(const ReportTemplate& reportTemplate)
{
    m_reportTemplate = reportTemplate;
}

void SetupLibraryDialog::addUnsavedSetup(TestSetup setup)
{
    if (!confirmLeave()) {
        return;
    }
    m_setups.push_back(std::move(setup));
    refreshList();
    selectRow(static_cast<int>(m_setups.size()) - 1);
    m_unsaved = true;
    markDirty();
}

void SetupLibraryDialog::reject()
{
    if (!confirmLeave()) {
        return;
    }
    QDialog::reject();
}

void SetupLibraryDialog::reload()
{
    const SetupLibrary library{setupLibraryDirectory()};
    // The home folder as "~": shorter to read, and it keeps the user name out
    // of a screenshot.
    QString shown = QDir::cleanPath(library.directory());
    const QString home = QDir::cleanPath(QDir::homePath());
    if (shown.startsWith(home + QLatin1Char('/'), Qt::CaseInsensitive)) {
        shown = QStringLiteral("~") + shown.mid(home.size());
    }
    m_folder->setText(tr("Library folder: %1").arg(QDir::toNativeSeparators(shown)));
    m_folder->setToolTip(QDir::toNativeSeparators(library.directory()));
    auto contents = library.load();
    m_setups = std::move(contents.setups);
    if (!contents.problems.empty()) {
        QStringList problems;
        for (const auto& problem : contents.problems) {
            problems << qs(problem);
        }
        QMessageBox::warning(
            this, tr("Some set-ups could not be read"), problems.join(QLatin1Char('\n')));
    }
    m_dirty = false;
    m_unsaved = false;
    refreshList();
    selectRow(m_setups.empty() ? -1 : 0);
}

void SetupLibraryDialog::refreshList()
{
    m_updating = true;
    m_list->clear();
    for (const auto& setup : m_setups) {
        auto* item = new QListWidgetItem{qs(setup.name), m_list};
        item->setToolTip(qs(setupMethodName(setup.method)));
    }
    m_updating = false;
}

void SetupLibraryDialog::selectRow(int row)
{
    m_current = row >= 0 && row < static_cast<int>(m_setups.size()) ? row : -1;
    m_dirty = false;
    m_unsaved = false;
    m_updating = true;
    m_list->setCurrentRow(m_current);
    m_updating = false;
    showCurrent();
}

TestSetup* SetupLibraryDialog::current()
{
    return m_current >= 0 && m_current < static_cast<int>(m_setups.size())
               ? &m_setups[static_cast<std::size_t>(m_current)]
               : nullptr;
}

void SetupLibraryDialog::showCurrent()
{
    const auto* setup = current();
    m_updating = true;
    m_name->setText(setup != nullptr ? qs(setup->name) : QString{});
    m_method->setCurrentIndex(
        setup != nullptr ? std::max(0, m_method->findData(static_cast<int>(setup->method))) : 0);
    m_description->setPlainText(setup != nullptr ? qs(setup->description) : QString{});
    m_updating = false;
    refreshDevices();
    refreshSummary();
    updateEnabledState();
}

void SetupLibraryDialog::refreshDevices()
{
    const auto* setup = current();
    m_updating = true;
    const int selected = m_devices->currentRow();
    const int rows = setup != nullptr ? static_cast<int>(setup->devices.size()) : 0;
    m_devices->setRowCount(rows);
    const auto now = std::chrono::system_clock::now();
    const bool dark = palette().color(QPalette::Base).lightness() < 128;
    for (int row = 0; row < rows; ++row) {
        const auto& device = setup->devices[static_cast<std::size_t>(row)];
        const std::array<QString, DeviceColumnCount> values{qs(device.equipment.role),
                                                            qs(device.equipment.model),
                                                            qs(device.equipment.serial),
                                                            qs(device.equipment.calibrationDue),
                                                            correctionText(device.correction),
                                                            qs(device.notes)};
        for (int column = 0; column < DeviceColumnCount; ++column) {
            auto* item = m_devices->item(row, column);
            if (item == nullptr) {
                item = new QTableWidgetItem;
                m_devices->setItem(row, column, item);
            }
            item->setText(values[static_cast<std::size_t>(column)]);
            if (column == ColumnCorrection) {
                item->setFlags(item->flags() & ~Qt::ItemIsEditable);
            }
            // A device switched off stays listed, greyed, to switch on again.
            item->setForeground(
                palette().color(device.inUse ? QPalette::Text : QPalette::PlaceholderText));
        }
        auto* role = m_devices->item(row, ColumnRole);
        role->setFlags(role->flags() | Qt::ItemIsUserCheckable);
        role->setCheckState(device.inUse ? Qt::Checked : Qt::Unchecked);
        role->setToolTip(device.inUse ? tr("In use. Untick to switch it off: it stays listed but "
                                           "is not in the equipment list and corrects nothing.")
                                      : tr("Switched off. Tick to use it again."));
        auto* due = m_devices->item(row, ColumnCalibration);
        if (!device.inUse) {
            due->setToolTip(QString{});
        } else if (device.equipment.calibrationExpired(now)) {
            due->setForeground(dark ? QColor{0xFF, 0xA9, 0xA0} : QColor{0xC5, 0x22, 0x1F});
            due->setToolTip(tr("Calibration expired"));
        } else {
            due->setToolTip(QString{});
        }
    }
    if (selected >= 0 && selected < rows) {
        m_devices->selectRow(selected);
    }
    m_updating = false;
    refreshDiagram();
}

void SetupLibraryDialog::refreshDiagram()
{
    m_diagram->setSetup(current());
    m_diagram->setSelectedDevice(m_devices->currentRow());
}

void SetupLibraryDialog::switchSlot(int slot, bool inUse)
{
    auto* setup = current();
    if (setup == nullptr) {
        return;
    }
    const auto before = setup->devices.size();
    const int device = setup->setSlotInUse(slot, inUse);
    markDirty();
    refreshDevices();
    refreshSummary();
    if (device >= 0 && inUse) {
        m_devices->selectRow(device);
        if (setup->devices.size() > before) {
            // A device just added: its model is the first thing to enter.
            m_devices->editItem(m_devices->item(device, ColumnModel));
        }
    }
}

void SetupLibraryDialog::refreshSummary()
{
    const auto* setup = current();
    if (setup == nullptr) {
        m_summary->setText(m_setups.empty() ? tr("No set-ups saved yet. Start one with <b>New</b>.")
                                            : QString{});
        return;
    }
    QStringList parts;
    const auto corrections = setup->corrections();
    if (corrections.empty()) {
        parts << tr("No corrections: levels are taken as the analyzer reads them.");
    } else {
        const auto at = [setup](Hertz frequency) {
            const double value = setup->totalCorrectionAt(frequency);
            return QStringLiteral("%1%2 dB")
                .arg(value >= 0.0 ? QStringLiteral("+") : QString{})
                .arg(value, 0, 'f', 1);
        };
        if (setup->kind == TestKind::Conducted) {
            parts << tr("Total correction: %1 at 150 kHz, %2 at 30 MHz.")
                         .arg(at(kilohertz(150)), at(megahertz(30)));
        } else {
            parts << tr("Total correction: %1 at 30 MHz, %2 at 300 MHz, %3 at 1 GHz.")
                         .arg(at(megahertz(30)), at(megahertz(300)), at(gigahertz(1.0)));
        }
    }
    const int expired = setup->expiredCount(std::chrono::system_clock::now());
    if (expired > 0) {
        parts << tr("<b>%n device(s) out of calibration.</b>", nullptr, expired);
    }
    if (m_dirty) {
        parts << tr("<i>Unsaved changes.</i>");
    }
    m_summary->setText(parts.join(QLatin1Char(' ')));
}

void SetupLibraryDialog::updateEnabledState()
{
    const bool selected = current() != nullptr;
    const int device = m_devices->currentRow();
    const int devices = m_devices->rowCount();
    for (QWidget* widget : std::initializer_list<QWidget*>{
             m_name, m_method, m_description, m_diagram, m_devices, m_addDevice, m_print, m_export})
    {
        widget->setEnabled(selected);
    }
    m_duplicate->setEnabled(selected && !m_unsaved);
    m_delete->setEnabled(selected);
    m_removeDevice->setEnabled(selected && device >= 0);
    m_up->setEnabled(selected && device > 0);
    m_down->setEnabled(selected && device >= 0 && device + 1 < devices);
    m_correction->setEnabled(selected && device >= 0);
    m_save->setEnabled(selected && m_dirty);
    m_apply->setEnabled(selected);
}

void SetupLibraryDialog::markDirty()
{
    m_dirty = true;
    refreshSummary();
    updateEnabledState();
}

bool SetupLibraryDialog::confirmLeave()
{
    const auto* setup = current();
    if (setup == nullptr || !m_dirty) {
        return true;
    }
    const auto answer =
        QMessageBox::question(this,
                              tr("Unsaved set-up"),
                              tr("'%1' has unsaved changes. Save them?").arg(qs(setup->name)),
                              QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    if (answer == QMessageBox::Save) {
        return saveCurrent();
    }
    if (answer == QMessageBox::Discard) {
        // Put back what the library holds, or drop a set-up it never held.
        const auto id = setup->id;
        if (m_unsaved) {
            m_setups.erase(m_setups.begin() + m_current);
            m_current = -1;
        } else if (auto saved =
                       TestSetupSerializer::load(SetupLibrary{setupLibraryDirectory()}.pathFor(id)))
        {
            m_setups[static_cast<std::size_t>(m_current)] = std::move(*saved);
        }
        m_dirty = false;
        m_unsaved = false;
        refreshList();
        m_updating = true;
        m_list->setCurrentRow(m_current);
        m_updating = false;
        return true;
    }
    return false;
}

void SetupLibraryDialog::newSetup(SetupMethod method)
{
    if (!confirmLeave()) {
        return;
    }
    auto setup = TestSetup::createFor(
        str(tr("New %1 set-up").arg(kindName(kindOf(method)).toLower())), method);
    m_setups.push_back(std::move(setup));
    refreshList();
    selectRow(static_cast<int>(m_setups.size()) - 1);
    m_unsaved = true;
    markDirty();
    m_name->setFocus();
    m_name->selectAll();
}

void SetupLibraryDialog::duplicateSetup()
{
    const auto* setup = current();
    if (setup == nullptr || !confirmLeave()) {
        return;
    }
    auto copy = *current();
    copy.id = newIdentifier();
    copy.name += str(tr(" (copy)"));
    copy.createdAt = std::chrono::system_clock::now();
    copy.modifiedAt = copy.createdAt;
    m_setups.push_back(std::move(copy));
    refreshList();
    selectRow(static_cast<int>(m_setups.size()) - 1);
    m_unsaved = true;
    markDirty();
}

void SetupLibraryDialog::deleteSetup()
{
    const auto* setup = current();
    if (setup == nullptr) {
        return;
    }
    if (QMessageBox::question(this,
                              tr("Delete set-up"),
                              tr("Delete '%1' from the library? Tests that used it keep their "
                                 "copy of its equipment and corrections.")
                                  .arg(qs(setup->name))) != QMessageBox::Yes)
    {
        return;
    }
    if (!m_unsaved) {
        if (auto status = SetupLibrary{setupLibraryDirectory()}.remove(setup->id); !status) {
            QMessageBox::warning(this, tr("Delete set-up"), qs(status.error().message()));
            return;
        }
    }
    const int row = m_current;
    m_setups.erase(m_setups.begin() + row);
    m_dirty = false;
    refreshList();
    selectRow(std::min(row, static_cast<int>(m_setups.size()) - 1));
}

bool SetupLibraryDialog::saveCurrent()
{
    auto* setup = current();
    if (setup == nullptr) {
        return false;
    }
    if (auto status = setup->validate(); !status) {
        QMessageBox::warning(this, tr("Save set-up"), qs(status.error().detail));
        return false;
    }
    setup->modifiedAt = std::chrono::system_clock::now();
    if (auto status = SetupLibrary{setupLibraryDirectory()}.save(*setup); !status) {
        QMessageBox::warning(this, tr("Save set-up"), qs(status.error().message()));
        return false;
    }
    m_dirty = false;
    m_unsaved = false;
    refreshSummary();
    updateEnabledState();
    return true;
}

void SetupLibraryDialog::importSetups()
{
    if (!confirmLeave()) {
        return;
    }
    const auto paths = QFileDialog::getOpenFileNames(
        this,
        tr("Import set-ups"),
        QString{},
        tr("PeakEmi set-ups (*.peakemi-setup.json);;JSON files (*.json)"));
    if (paths.isEmpty()) {
        return;
    }
    const SetupLibrary library{setupLibraryDirectory()};
    QStringList problems;
    for (const auto& path : paths) {
        auto setup = TestSetupSerializer::load(path);
        if (!setup) {
            problems << QStringLiteral("%1: %2").arg(QFileInfo{path}.fileName(),
                                                     qs(setup.error().message()));
            continue;
        }
        // An import never silently replaces a set-up already in the library.
        const bool known =
            std::any_of(m_setups.begin(), m_setups.end(), [&setup](const auto& existing) {
                return existing.id == setup->id;
            });
        if (known) {
            setup->id = newIdentifier();
            setup->name += str(tr(" (imported)"));
        }
        if (auto status = library.save(*setup); !status) {
            problems << QStringLiteral("%1: %2").arg(QFileInfo{path}.fileName(),
                                                     qs(status.error().message()));
        }
    }
    if (!problems.isEmpty()) {
        QMessageBox::warning(this, tr("Import set-ups"), problems.join(QLatin1Char('\n')));
    }
    reload();
}

void SetupLibraryDialog::exportCurrent()
{
    const auto* setup = current();
    if (setup == nullptr) {
        return;
    }
    auto path =
        QFileDialog::getSaveFileName(this,
                                     tr("Export set-up"),
                                     fileStem(*setup) + QStringLiteral(".peakemi-setup.json"),
                                     tr("PeakEmi set-ups (*.peakemi-setup.json)"));
    if (path.isEmpty()) {
        return;
    }
    if (!path.endsWith(QStringLiteral(".json"), Qt::CaseInsensitive)) {
        path += QStringLiteral(".peakemi-setup.json");
    }
    if (auto status = TestSetupSerializer::save(*setup, path); !status) {
        QMessageBox::warning(this, tr("Export set-up"), qs(status.error().message()));
    }
}

void SetupLibraryDialog::printSheet()
{
    const auto* setup = current();
    if (setup == nullptr) {
        return;
    }
    const auto path = QFileDialog::getSaveFileName(this,
                                                   tr("Print set-up sheet"),
                                                   fileStem(*setup) + QStringLiteral(".pdf"),
                                                   tr("PDF documents (*.pdf)"));
    if (path.isEmpty()) {
        return;
    }
    const reporting::SetupSheetRenderer renderer;
    if (auto status = renderer.render(*setup, m_reportTemplate, path); !status) {
        QMessageBox::warning(this, tr("Print set-up sheet"), qs(status.error().message()));
    }
}

void SetupLibraryDialog::chooseFolder()
{
    if (!confirmLeave()) {
        return;
    }
    const auto folder = QFileDialog::getExistingDirectory(
        this, tr("Set-up library folder"), setupLibraryDirectory());
    if (folder.isEmpty()) {
        return;
    }
    QSettings{}.setValue(DirectoryKey, folder);
    reload();
}

void SetupLibraryDialog::applyCurrent()
{
    const auto* setup = current();
    if (setup == nullptr) {
        return;
    }
    if (m_dirty && !saveCurrent()) {
        return;
    }
    emit applyRequested(*current());
    accept();
}

void SetupLibraryDialog::addDevice(const QString& role)
{
    auto* setup = current();
    if (setup == nullptr) {
        return;
    }
    SetupDevice device{.equipment = EquipmentItem{.role = str(role)},
                       .correction = defaultCorrectionFor(role)};
    // A new device goes in front of the analyzer, which ends the chain.
    auto position = setup->devices.end();
    if (!setup->devices.empty()) {
        const auto& last = setup->devices.back().equipment.role;
        if (QString::fromStdString(last).contains(tr("analyzer"), Qt::CaseInsensitive) &&
            role != tr("Spectrum analyzer"))
        {
            position = setup->devices.end() - 1;
        }
    }
    const auto row = static_cast<int>(position - setup->devices.begin());
    setup->devices.insert(position, std::move(device));
    markDirty();
    refreshDevices();
    m_devices->selectRow(row);
    m_devices->editItem(m_devices->item(row, ColumnModel));
}

void SetupLibraryDialog::removeDevice()
{
    auto* setup = current();
    const int row = m_devices->currentRow();
    if (setup == nullptr || row < 0 || static_cast<std::size_t>(row) >= setup->devices.size()) {
        return;
    }
    setup->devices.erase(setup->devices.begin() + row);
    markDirty();
    refreshDevices();
    refreshSummary();
}

void SetupLibraryDialog::moveDevice(int offset)
{
    auto* setup = current();
    const int row = m_devices->currentRow();
    const int target = row + offset;
    if (setup == nullptr || row < 0 || target < 0 ||
        target >= static_cast<int>(setup->devices.size()))
    {
        return;
    }
    std::swap(setup->devices[static_cast<std::size_t>(row)],
              setup->devices[static_cast<std::size_t>(target)]);
    markDirty();
    refreshDevices();
    m_devices->selectRow(target);
}

void SetupLibraryDialog::setFixedCorrection()
{
    auto* setup = current();
    const int row = m_devices->currentRow();
    if (setup == nullptr || row < 0 || static_cast<std::size_t>(row) >= setup->devices.size()) {
        return;
    }
    auto& device = setup->devices[static_cast<std::size_t>(row)];

    QDialog dialog{this};
    dialog.setWindowTitle(tr("Fixed correction"));
    auto* form = new QFormLayout{&dialog};
    auto* kind = new QComboBox{&dialog};
    for (const auto value : {CorrectionKind::Attenuator,
                             CorrectionKind::AmplifierGain,
                             CorrectionKind::CableLoss,
                             CorrectionKind::LisnFactor,
                             CorrectionKind::AntennaFactor,
                             CorrectionKind::Other})
    {
        kind->addItem(reporting::correctionKindName(value), static_cast<int>(value));
    }
    auto* value = new QDoubleSpinBox{&dialog};
    value->setRange(-100.0, 100.0);
    value->setDecimals(1);
    value->setSuffix(QStringLiteral(" dB"));
    if (device.correction) {
        kind->setCurrentIndex(kind->findData(static_cast<int>(device.correction->kind)));
        if (!device.correction->points.empty()) {
            value->setValue(device.correction->points.front().second);
        }
    } else {
        value->setValue(10.0);
    }
    form->addRow(tr("Type"), kind);
    form->addRow(tr("Value"), value);
    auto* hint = new QLabel{tr("A gain is entered as a positive number; it is subtracted from "
                               "the reading."),
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
    const auto name =
        device.equipment.model.empty() ? device.equipment.role : device.equipment.model;
    device.correction = flatCorrection(name.empty() ? str(tr("Correction")) : name,
                                       static_cast<CorrectionKind>(kind->currentData().toInt()),
                                       value->value());
    markDirty();
    refreshDevices();
    refreshSummary();
}

void SetupLibraryDialog::importCorrection()
{
    auto* setup = current();
    const int row = m_devices->currentRow();
    if (setup == nullptr || row < 0 || static_cast<std::size_t>(row) >= setup->devices.size()) {
        return;
    }
    const auto path =
        QFileDialog::getOpenFileName(this,
                                     tr("Import correction table"),
                                     QString{},
                                     tr("Correction files (*.csv *.json);;All files (*)"));
    if (path.isEmpty()) {
        return;
    }
    auto table = correction_io::load(path);
    if (!table) {
        QMessageBox::warning(this, tr("Import correction table"), qs(table.error().message()));
        return;
    }
    setup->devices[static_cast<std::size_t>(row)].correction = std::move(*table);
    markDirty();
    refreshDevices();
    refreshSummary();
}

void SetupLibraryDialog::editCorrection()
{
    const auto* setup = current();
    const int row = m_devices->currentRow();
    if (setup == nullptr || row < 0 || static_cast<std::size_t>(row) >= setup->devices.size()) {
        return;
    }
    const auto& device = setup->devices[static_cast<std::size_t>(row)];
    CorrectionTable table;
    if (device.correction) {
        table = *device.correction;
    } else {
        // A new table named after the device, of the kind its role suggests.
        table.name =
            device.equipment.model.empty() ? device.equipment.role : device.equipment.model;
        table.kind = correctionKindForRole(qs(device.equipment.role));
    }

    CurveEditorDialog editor{CurveEditorDialog::Mode::Correction, this};
    editor.setAcceptText(tr("Use for this device"));
    editor.setCorrection(table);
    if (editor.exec() != QDialog::Accepted) {
        return;
    }
    // Looked up again: the reference above does not outlive a modal loop.
    auto* edited = current();
    if (edited == nullptr || static_cast<std::size_t>(row) >= edited->devices.size()) {
        return;
    }
    edited->devices[static_cast<std::size_t>(row)].correction = editor.correction();
    markDirty();
    refreshDevices();
    refreshSummary();
}

void SetupLibraryDialog::removeCorrection()
{
    auto* setup = current();
    const int row = m_devices->currentRow();
    if (setup == nullptr || row < 0 || static_cast<std::size_t>(row) >= setup->devices.size()) {
        return;
    }
    setup->devices[static_cast<std::size_t>(row)].correction.reset();
    markDirty();
    refreshDevices();
    refreshSummary();
}

} // namespace peakemi::ui
