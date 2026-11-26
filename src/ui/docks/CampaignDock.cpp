#include <peakemi/core/CampaignSerializer.h>
#include <peakemi/core/Version.h>
#include <peakemi/ui/CampaignDock.h>

#include <QBuffer>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPalette>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QStringList>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <chrono>

namespace peakemi::ui {
namespace {

[[nodiscard]] QString qs(std::string_view text)
{
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

[[nodiscard]] std::string str(const QString& text)
{
    return text.toStdString();
}

[[nodiscard]] QString when(TimePoint at)
{
    const auto iso = qs(toIso8601(at));
    return iso.left(10) + QLatin1Char(' ') + iso.mid(11, 5) + QStringLiteral(" UTC");
}

struct VerdictColours
{
    QColor background;
    QColor foreground;
};

/// Pale tints on a light table, deep ones on a dark table, so the verdict
/// stays readable whichever way the palette goes.
[[nodiscard]] VerdictColours verdictColours(TestVerdict verdict, bool dark)
{
    switch (verdict) {
        case TestVerdict::Pass:
            return dark ? VerdictColours{QColor{0x15, 0x30, 0x1E}, QColor{0xA8, 0xE6, 0xBC}}
                        : VerdictColours{QColor{0xE6, 0xF4, 0xEA}, QColor{0x13, 0x73, 0x33}};
        case TestVerdict::Marginal:
            return dark ? VerdictColours{QColor{0x3A, 0x30, 0x0E}, QColor{0xF7, 0xD9, 0x7A}}
                        : VerdictColours{QColor{0xFE, 0xF7, 0xE0}, QColor{0xB0, 0x60, 0x00}};
        case TestVerdict::Fail:
            return dark ? VerdictColours{QColor{0x40, 0x1A, 0x18}, QColor{0xFF, 0xA9, 0xA0}}
                        : VerdictColours{QColor{0xFC, 0xE8, 0xE6}, QColor{0xC5, 0x22, 0x1F}};
        case TestVerdict::NotTested:
        case TestVerdict::NotApplicable:
            break;
    }
    return dark ? VerdictColours{QColor{0x2A, 0x2F, 0x37}, QColor{0xAA, 0xB3, 0xC0}}
                : VerdictColours{QColor{0xF1, 0xF3, 0xF4}, QColor{0x5F, 0x63, 0x68}};
}

[[nodiscard]] bool isDark(const QWidget* widget)
{
    return widget->palette().color(QPalette::Base).lightness() < 128;
}

[[nodiscard]] QString defaultTestName(TestKind kind)
{
    switch (kind) {
        case TestKind::Conducted:
            return QObject::tr("Conducted emissions");
        case TestKind::Radiated:
            return QObject::tr("Radiated emissions");
        case TestKind::Other:
            break;
    }
    return QObject::tr("New test");
}

/// Large photos are scaled down and stored as JPEG: a campaign file carries
/// them, and a 20-megapixel original adds nothing a report can show.
[[nodiscard]] std::optional<SetupPhoto> photoFromFile(const QString& path)
{
    QImage image{path};
    if (image.isNull()) {
        return std::nullopt;
    }
    constexpr int MaximumSide = 1600;
    if (image.width() > MaximumSide || image.height() > MaximumSide) {
        image =
            image.scaled(MaximumSide, MaximumSide, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    QByteArray bytes;
    QBuffer buffer{&bytes};
    buffer.open(QIODevice::WriteOnly);
    if (!image.convertToFormat(QImage::Format_RGB32).save(&buffer, "JPEG", 85)) {
        return std::nullopt;
    }
    return SetupPhoto{.caption = str(QFileInfo{path}.completeBaseName()),
                      .mimeType = "image/jpeg",
                      .base64Data = bytes.toBase64().toStdString()};
}

[[nodiscard]] QIcon thumbnail(const SetupPhoto& photo)
{
    const auto bytes = QByteArray::fromBase64(QByteArray::fromStdString(photo.base64Data));
    const QImage image = QImage::fromData(bytes);
    if (image.isNull()) {
        return {};
    }
    return QIcon{
        QPixmap::fromImage(image.scaled(160, 120, Qt::KeepAspectRatio, Qt::SmoothTransformation))};
}

enum TestColumn
{
    ColumnName,
    ColumnPort,
    ColumnMeasured,
    ColumnFinal,
    ColumnNotes,
    TestColumnCount
};

enum EquipmentColumn
{
    ColumnRole,
    ColumnModel,
    ColumnSerial,
    ColumnCalibration,
    EquipmentColumnCount
};

} // namespace

QString testVerdictText(TestVerdict verdict)
{
    switch (verdict) {
        case TestVerdict::Pass:
            return QObject::tr("Pass");
        case TestVerdict::Marginal:
            return QObject::tr("Marginal");
        case TestVerdict::Fail:
            return QObject::tr("Fail");
        case TestVerdict::NotApplicable:
            return QObject::tr("Not applicable");
        case TestVerdict::NotTested:
            break;
    }
    return QObject::tr("Not tested");
}

CampaignDock::CampaignDock(QWidget* parent) : QDockWidget{tr("Test plan"), parent}
{
    setObjectName(QStringLiteral("campaignDock"));
    buildMenu();

    auto* content = new QWidget{this};
    auto* layout = new QHBoxLayout{content};
    layout->setContentsMargins(6, 6, 6, 6);

    // --- left: the campaign and its tests --------------------------------
    auto* left = new QVBoxLayout;
    auto* header = new QHBoxLayout;
    m_title = new QLabel{content};
    m_title->setTextFormat(Qt::RichText);
    m_overall = new QLabel{content};
    m_overall->setObjectName(QStringLiteral("campaignVerdict"));
    auto* newButton = new QPushButton{tr("New"), content};
    newButton->setMenu(m_menu->actions().front()->menu());
    auto* openButton = new QPushButton{tr("Open…"), content};
    auto* saveButton = new QPushButton{tr("Save"), content};
    auto* reportButton = new QPushButton{tr("Report…"), content};
    header->addWidget(m_title, 1);
    header->addWidget(m_overall);
    header->addWidget(newButton);
    header->addWidget(openButton);
    header->addWidget(saveButton);
    header->addWidget(reportButton);
    left->addLayout(header);
    connect(openButton, &QPushButton::clicked, this, &CampaignDock::openCampaign);
    connect(saveButton, &QPushButton::clicked, this, &CampaignDock::saveCampaign);
    connect(reportButton, &QPushButton::clicked, this, &CampaignDock::reportRequested);

    m_tests = new QTableWidget{0, TestColumnCount, content};
    m_tests->setObjectName(QStringLiteral("campaignTests"));
    m_tests->setHorizontalHeaderLabels(
        {tr("Test"), tr("Port"), tr("Measured"), tr("Final"), tr("Notes")});
    m_tests->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tests->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tests->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_tests->verticalHeader()->setVisible(false);
    m_tests->horizontalHeader()->setSectionResizeMode(ColumnName, QHeaderView::Stretch);
    m_tests->horizontalHeader()->setSectionResizeMode(ColumnPort, QHeaderView::Stretch);
    for (const int column : {ColumnMeasured, ColumnFinal, ColumnNotes}) {
        m_tests->horizontalHeader()->setSectionResizeMode(column, QHeaderView::ResizeToContents);
    }
    connect(m_tests, &QTableWidget::itemSelectionChanged, this, [this] {
        if (m_updating) {
            return;
        }
        refreshDetails();
        if (const auto* test = activeTest()) {
            emit testSelected(*test);
        }
    });
    left->addWidget(m_tests, 1);

    auto* testButtons = new QHBoxLayout;
    m_addTest = new QPushButton{tr("Add test"), content};
    auto* addMenu = new QMenu{m_addTest};
    addMenu->addAction(tr("Conducted emissions"), this, [this] { addTest(TestKind::Conducted); });
    addMenu->addAction(tr("Radiated emissions"), this, [this] { addTest(TestKind::Radiated); });
    addMenu->addAction(tr("Other test"), this, [this] { addTest(TestKind::Other); });
    m_addTest->setMenu(addMenu);
    m_removeTest = new QPushButton{tr("Remove"), content};
    m_ambient = new QPushButton{tr("Ambient scan"), content};
    m_ambient->setToolTip(tr("Scan with the equipment under test switched off. The scan is "
                             "drawn under the measurement to tell ambient signals apart."));
    m_run = new QPushButton{tr("Run test"), content};
    m_run->setProperty("primary", true);
    m_run->setToolTip(tr("Measure the selected test with its run configuration and file the "
                         "result under it."));
    testButtons->addWidget(m_addTest);
    testButtons->addWidget(m_removeTest);
    testButtons->addStretch(1);
    testButtons->addWidget(m_ambient);
    testButtons->addWidget(m_run);
    left->addLayout(testButtons);
    connect(m_removeTest, &QPushButton::clicked, this, &CampaignDock::removeTest);
    connect(m_ambient, &QPushButton::clicked, this, &CampaignDock::ambientRequested);
    connect(m_run, &QPushButton::clicked, this, &CampaignDock::runTestRequested);

    // --- right: the selected test -----------------------------------------
    m_details = new QTabWidget{content};
    m_details->addTab(buildResultTab(), tr("Result"));
    m_details->addTab(buildSetupTab(), tr("Setup"));
    m_details->addTab(buildEquipmentTab(), tr("Equipment"));
    m_details->addTab(buildPhotosTab(), tr("Photos"));

    layout->addLayout(left, 5);
    layout->addWidget(m_details, 6);
    setWidget(content);

    refreshHeader();
    refreshTests();
    refreshDetails();
}

CampaignDock::~CampaignDock() = default;

void CampaignDock::buildMenu()
{
    m_menu = new QMenu{tr("&Campaign"), this};
    auto* newMenu = m_menu->addMenu(tr("&New campaign"));
    for (const auto& entry : campaignTemplates()) {
        const auto id = qs(entry.id);
        newMenu->addAction(qs(entry.name), this, [this, id] { newCampaign(id); });
    }
    newMenu->addSeparator();
    newMenu->addAction(tr("Empty campaign"), this, [this] { newCampaign(QString{}); });
    m_menu->addAction(tr("&Open campaign…"), this, &CampaignDock::openCampaign);
    m_menu->addAction(tr("&Save campaign"), this, &CampaignDock::saveCampaign);
    m_menu->addAction(tr("Save campaign &as…"), this, &CampaignDock::saveCampaignAs);
    m_menu->addSeparator();
    m_menu->addAction(tr("Campaign &details…"), this, &CampaignDock::editDetails);
    m_menu->addAction(tr("Sign-off and &lock…"), this, &CampaignDock::editSignoff);
    m_menu->addSeparator();
    m_menu->addAction(tr("Generate campaign &report…"), this, &CampaignDock::reportRequested);
}

QWidget* CampaignDock::buildResultTab()
{
    auto* tab = new QWidget;
    auto* form = new QFormLayout{tab};

    m_measured = new QLabel{tab};
    m_worstMargin = new QLabel{tab};
    form->addRow(tr("Measured verdict"), m_measured);
    form->addRow(tr("Worst margin"), m_worstMargin);

    m_override = new QCheckBox{tr("Override the measured verdict"), tab};
    m_overrideVerdict = new QComboBox{tab};
    for (const auto verdict :
         {TestVerdict::Pass, TestVerdict::Marginal, TestVerdict::Fail, TestVerdict::NotApplicable})
    {
        m_overrideVerdict->addItem(testVerdictText(verdict), static_cast<int>(verdict));
    }
    m_overrideReason = new QLineEdit{tab};
    m_overrideReason->setPlaceholderText(tr("Reason (required; printed in the report)"));
    auto* overrideRow = new QHBoxLayout;
    overrideRow->addWidget(m_overrideVerdict);
    overrideRow->addWidget(m_overrideReason, 1);
    form->addRow(m_override);
    form->addRow(tr("Final verdict"), overrideRow);
    m_overrideHint = new QLabel{tab};
    m_overrideHint->setWordWrap(true);
    form->addRow(QString{}, m_overrideHint);
    connect(m_override, &QCheckBox::toggled, this, &CampaignDock::applyOverride);
    connect(m_overrideVerdict, &QComboBox::currentIndexChanged, this, &CampaignDock::applyOverride);
    connect(m_overrideReason, &QLineEdit::textEdited, this, &CampaignDock::applyOverride);

    m_comments = new QPlainTextEdit{tab};
    m_comments->setPlaceholderText(tr("Observations, deviations from the method, follow-up…"));
    m_comments->setMaximumHeight(90);
    form->addRow(tr("Comments"), m_comments);
    connect(m_comments, &QPlainTextEdit::textChanged, this, [this] {
        auto* test = mutableActiveTest();
        if (m_updating || test == nullptr) {
            return;
        }
        test->comments = str(m_comments->toPlainText());
        markDirty();
        refreshTestRow(m_tests->currentRow());
    });

    m_testedBy = new QLineEdit{tab};
    m_testedBy->setPlaceholderText(tr("Name"));
    m_sign = new QPushButton{tr("Sign"), tab};
    m_signedAt = new QLabel{tab};
    auto* signRow = new QHBoxLayout;
    signRow->addWidget(m_testedBy, 1);
    signRow->addWidget(m_sign);
    signRow->addWidget(m_signedAt);
    form->addRow(tr("Tested by"), signRow);
    connect(m_sign, &QPushButton::clicked, this, [this] {
        auto* test = mutableActiveTest();
        if (test == nullptr) {
            return;
        }
        const auto name = m_testedBy->text().trimmed();
        test->testedBy = name.isEmpty()
                             ? Signoff{}
                             : Signoff{.name = str(name), .at = std::chrono::system_clock::now()};
        markDirty();
        refreshDetails();
    });
    return tab;
}

QWidget* CampaignDock::buildSetupTab()
{
    auto* tab = new QWidget;
    auto* form = new QFormLayout{tab};
    m_name = new QLineEdit{tab};
    m_port = new QLineEdit{tab};
    m_port->setPlaceholderText(tr("e.g. AC mains, line L1"));
    m_procedure = new QPlainTextEdit{tab};
    m_procedure->setPlaceholderText(tr("Setup and method, as the operator should carry it out"));
    form->addRow(tr("Name"), m_name);
    form->addRow(tr("Port"), m_port);
    form->addRow(tr("Method"), m_procedure);
    auto* hint = new QLabel{tr("Frequency range, limit lines, detectors and corrections are "
                               "edited in the run configuration while this test is selected."),
                            tab};
    hint->setWordWrap(true);
    hint->setEnabled(false);
    form->addRow(hint);

    connect(m_name, &QLineEdit::editingFinished, this, [this] {
        auto* test = mutableActiveTest();
        if (m_updating || test == nullptr || m_name->text().trimmed().isEmpty() ||
            str(m_name->text()) == test->name)
        {
            return;
        }
        test->name = str(m_name->text().trimmed());
        markDirty();
        refreshTestRow(m_tests->currentRow());
    });
    connect(m_port, &QLineEdit::editingFinished, this, [this] {
        auto* test = mutableActiveTest();
        if (m_updating || test == nullptr || str(m_port->text()) == test->port) {
            return;
        }
        test->port = str(m_port->text());
        markDirty();
        refreshTestRow(m_tests->currentRow());
    });
    connect(m_procedure, &QPlainTextEdit::textChanged, this, [this] {
        auto* test = mutableActiveTest();
        if (m_updating || test == nullptr) {
            return;
        }
        test->procedure = str(m_procedure->toPlainText());
        markDirty();
    });
    return tab;
}

QWidget* CampaignDock::buildEquipmentTab()
{
    auto* tab = new QWidget;
    auto* layout = new QVBoxLayout{tab};

    auto* setupRow = new QHBoxLayout;
    m_setupLabel = new QLabel{tab};
    m_setupLabel->setTextFormat(Qt::RichText);
    m_applySetup = new QPushButton{tr("Apply set-up…"), tab};
    m_applySetup->setToolTip(tr("Fill this test's equipment and corrections from a saved "
                                "set-up."));
    m_saveSetup = new QPushButton{tr("Save as set-up…"), tab};
    m_saveSetup->setToolTip(tr("Keep this test's equipment and corrections as a set-up for "
                               "later tests and campaigns."));
    setupRow->addWidget(m_setupLabel, 1);
    setupRow->addWidget(m_applySetup);
    setupRow->addWidget(m_saveSetup);
    layout->addLayout(setupRow);
    connect(m_applySetup, &QPushButton::clicked, this, &CampaignDock::setupLibraryRequested);
    connect(m_saveSetup, &QPushButton::clicked, this, [this] {
        const auto* test = activeTest();
        if (test == nullptr) {
            return;
        }
        const auto name = test->setupName.empty() ? test->name : test->setupName;
        auto setup = setupFrom(name, test->kind, test->equipment, test->config.corrections);
        setup.description = test->setupDescription;
        emit saveAsSetupRequested(setup);
    });

    m_equipment = new QTableWidget{0, EquipmentColumnCount, tab};
    m_equipment->setHorizontalHeaderLabels(
        {tr("Role"), tr("Model"), tr("Serial number"), tr("Calibration due (YYYY-MM-DD)")});
    m_equipment->verticalHeader()->setVisible(false);
    m_equipment->horizontalHeader()->setStretchLastSection(true);
    m_equipment->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_equipment->setColumnWidth(ColumnRole, 130);
    m_equipment->setColumnWidth(ColumnModel, 150);
    m_equipment->setColumnWidth(ColumnSerial, 120);
    layout->addWidget(m_equipment, 1);
    connect(m_equipment, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* item) {
        auto* test = mutableActiveTest();
        if (m_updating || test == nullptr || item == nullptr) {
            return;
        }
        const auto row = static_cast<std::size_t>(item->row());
        if (row >= test->equipment.size()) {
            return;
        }
        auto& entry = test->equipment[row];
        const auto value = str(item->text().trimmed());
        switch (item->column()) {
            case ColumnRole:
                entry.role = value;
                break;
            case ColumnModel:
                entry.model = value;
                break;
            case ColumnSerial:
                entry.serial = value;
                break;
            case ColumnCalibration:
                entry.calibrationDue = value;
                break;
            default:
                return;
        }
        markDirty();
        if (item->column() == ColumnCalibration) {
            refreshEquipment();
        }
    });

    auto* buttons = new QHBoxLayout;
    m_addEquipment = new QPushButton{tr("Add"), tab};
    m_removeEquipment = new QPushButton{tr("Remove"), tab};
    m_copyEquipment = new QPushButton{tr("Use for all similar tests"), tab};
    m_copyEquipment->setToolTip(
        tr("Copy this list to every other test of the same type, e.g. all radiated tests."));
    buttons->addWidget(m_addEquipment);
    buttons->addWidget(m_removeEquipment);
    buttons->addStretch(1);
    buttons->addWidget(m_copyEquipment);
    layout->addLayout(buttons);
    connect(m_addEquipment, &QPushButton::clicked, this, &CampaignDock::addEquipment);
    connect(m_removeEquipment, &QPushButton::clicked, this, &CampaignDock::removeEquipment);
    connect(
        m_copyEquipment, &QPushButton::clicked, this, &CampaignDock::copyEquipmentToSimilarTests);
    return tab;
}

QWidget* CampaignDock::buildPhotosTab()
{
    auto* tab = new QWidget;
    auto* layout = new QVBoxLayout{tab};
    m_photos = new QListWidget{tab};
    m_photos->setViewMode(QListView::IconMode);
    m_photos->setIconSize(QSize{160, 120});
    m_photos->setResizeMode(QListView::Adjust);
    m_photos->setMovement(QListView::Static);
    m_photos->setSpacing(6);
    layout->addWidget(m_photos, 1);

    auto* captionRow = new QHBoxLayout;
    m_caption = new QLineEdit{tab};
    m_caption->setPlaceholderText(tr("Caption of the selected photo"));
    m_addPhotos = new QPushButton{tr("Add photos…"), tab};
    m_removePhoto = new QPushButton{tr("Remove"), tab};
    captionRow->addWidget(m_caption, 1);
    captionRow->addWidget(m_addPhotos);
    captionRow->addWidget(m_removePhoto);
    layout->addLayout(captionRow);

    connect(m_photos, &QListWidget::currentRowChanged, this, [this](int row) {
        const auto* test = activeTest();
        m_updating = true;
        m_caption->setText(test != nullptr && row >= 0 &&
                                   static_cast<std::size_t>(row) < test->photos.size()
                               ? qs(test->photos[static_cast<std::size_t>(row)].caption)
                               : QString{});
        m_updating = false;
        updateEnabledState();
    });
    connect(m_caption, &QLineEdit::editingFinished, this, [this] {
        auto* test = mutableActiveTest();
        const int row = m_photos->currentRow();
        if (m_updating || test == nullptr || row < 0 ||
            static_cast<std::size_t>(row) >= test->photos.size())
        {
            return;
        }
        test->photos[static_cast<std::size_t>(row)].caption = str(m_caption->text());
        if (auto* item = m_photos->item(row)) {
            item->setText(m_caption->text());
        }
        markDirty();
    });
    connect(m_addPhotos, &QPushButton::clicked, this, &CampaignDock::addPhotos);
    connect(m_removePhoto, &QPushButton::clicked, this, &CampaignDock::removePhoto);
    return tab;
}

const Campaign* CampaignDock::campaign() const
{
    return m_campaign ? &*m_campaign : nullptr;
}

bool CampaignDock::isLocked() const
{
    return m_campaign && m_campaign->locked;
}

const CampaignTest* CampaignDock::activeTest() const
{
    if (!m_campaign) {
        return nullptr;
    }
    const int row = m_tests->currentRow();
    if (row < 0 || static_cast<std::size_t>(row) >= m_campaign->tests.size()) {
        return nullptr;
    }
    return &m_campaign->tests[static_cast<std::size_t>(row)];
}

CampaignTest* CampaignDock::mutableActiveTest()
{
    if (isLocked()) {
        return nullptr;
    }
    return const_cast<CampaignTest*>(activeTest());
}

void CampaignDock::setCampaign(Campaign campaign, const QString& path)
{
    m_campaign = std::move(campaign);
    m_path = path;
    m_dirty = false;
    refreshTests();
    refreshHeader();
    selectTest(m_campaign->tests.empty() ? -1 : 0);
    show();
    raise();
}

void CampaignDock::selectTest(int row)
{
    if (row < 0 || row >= m_tests->rowCount()) {
        m_tests->clearSelection();
        refreshDetails();
        return;
    }
    m_tests->selectRow(row);
    m_tests->setCurrentCell(row, ColumnName);
}

void CampaignDock::setActiveTestConfiguration(const RunConfiguration& config)
{
    auto* test = mutableActiveTest();
    if (test == nullptr || test->config == config) {
        return;
    }
    test->config = config;
    markDirty();
}

void CampaignDock::attachSession(const Session& session)
{
    auto* test = mutableActiveTest();
    if (test == nullptr) {
        return;
    }
    test->session = session;
    markDirty();
    refreshTestRow(m_tests->currentRow());
    refreshDetails();
    emit statusMessage(tr("Measurement filed under '%1': %2")
                           .arg(qs(test->name), testVerdictText(test->measuredVerdict())));
    emit testSelected(*test);
}

void CampaignDock::attachAmbient(const Trace& trace)
{
    auto* test = mutableActiveTest();
    if (test == nullptr) {
        return;
    }
    test->ambient = trace;
    markDirty();
    refreshTestRow(m_tests->currentRow());
    emit statusMessage(tr("Ambient scan filed under '%1'.").arg(qs(test->name)));
    emit testSelected(*test);
}

void CampaignDock::applySetup(const TestSetup& setup)
{
    auto* test = mutableActiveTest();
    if (test == nullptr) {
        return;
    }
    if (setup.kind != TestKind::Other && test->kind != TestKind::Other &&
        setup.kind != test->kind &&
        QMessageBox::question(this,
                              tr("Apply set-up"),
                              tr("'%1' is a %2 set-up and '%3' a %4 test. Apply it anyway?")
                                  .arg(qs(setup.name),
                                       qs(testKindKey(setup.kind)),
                                       qs(test->name),
                                       qs(testKindKey(test->kind)))) != QMessageBox::Yes)
    {
        return;
    }
    peakemi::applySetup(*test, setup);
    markDirty();
    refreshTestRow(m_tests->currentRow());
    refreshDetails();
    emit statusMessage(tr("Set-up '%1' applied to '%2': %3 device(s), %4 correction(s).")
                           .arg(qs(setup.name), qs(test->name))
                           .arg(setup.devices.size())
                           .arg(setup.corrections().size()));
    emit testSelected(*test);
}

void CampaignDock::setRunState(bool connected, bool running)
{
    m_connected = connected;
    m_running = running;
    updateEnabledState();
}

bool CampaignDock::confirmDiscard()
{
    if (!m_campaign || !m_dirty) {
        return true;
    }
    const auto answer = QMessageBox::question(
        this,
        tr("Unsaved campaign"),
        tr("The campaign '%1' has unsaved changes. Save them first?").arg(qs(m_campaign->title)),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    if (answer == QMessageBox::Save) {
        return saveCampaign();
    }
    return answer == QMessageBox::Discard;
}

void CampaignDock::newCampaign(const QString& templateId)
{
    if (!confirmDiscard()) {
        return;
    }
    const std::string version{ProjectVersion};
    Campaign campaign;
    if (templateId.isEmpty()) {
        campaign = Campaign::createNew(version);
        campaign.title = str(tr("New campaign"));
    } else {
        auto made = campaignFromTemplate(str(templateId), version);
        if (!made) {
            QMessageBox::warning(this, tr("New campaign"), qs(made.error().message()));
            return;
        }
        campaign = std::move(*made);
    }
    setCampaign(std::move(campaign));
    m_dirty = true;
    refreshHeader();
    emit statusMessage(tr("New campaign: %1 test(s). Fill in the equipment under test next.")
                           .arg(m_campaign->tests.size()));
    editDetails();
}

void CampaignDock::openCampaign()
{
    if (!confirmDiscard()) {
        return;
    }
    const auto path = QFileDialog::getOpenFileName(
        this,
        tr("Open campaign"),
        QString{},
        tr("PeakEmi campaigns (*.peakemi-campaign.json);;JSON files (*.json)"));
    if (path.isEmpty()) {
        return;
    }
    auto loaded = CampaignSerializer::load(path);
    if (!loaded) {
        QMessageBox::warning(this, tr("Open campaign"), qs(loaded.error().message()));
        return;
    }
    setCampaign(std::move(*loaded), path);
    emit statusMessage(tr("Opened campaign %1").arg(path));
}

bool CampaignDock::saveCampaign()
{
    if (!m_campaign) {
        return false;
    }
    if (m_path.isEmpty()) {
        return saveCampaignAs();
    }
    m_campaign->modifiedAt = std::chrono::system_clock::now();
    if (auto status = CampaignSerializer::save(*m_campaign, m_path); !status) {
        QMessageBox::warning(this, tr("Save campaign"), qs(status.error().message()));
        return false;
    }
    m_dirty = false;
    refreshHeader();
    emit statusMessage(tr("Campaign saved to %1").arg(m_path));
    return true;
}

bool CampaignDock::saveCampaignAs()
{
    if (!m_campaign) {
        return false;
    }
    QString suggested = qs(m_campaign->title).trimmed();
    suggested.replace(QRegularExpression{QStringLiteral(R"([^\w\- ]+)")}, QStringLiteral(" "));
    suggested = suggested.simplified().replace(QLatin1Char(' '), QLatin1Char('-'));
    if (suggested.isEmpty()) {
        suggested = QStringLiteral("campaign");
    }
    auto path = QFileDialog::getSaveFileName(this,
                                             tr("Save campaign"),
                                             suggested + QStringLiteral(".peakemi-campaign.json"),
                                             tr("PeakEmi campaigns (*.peakemi-campaign.json)"));
    if (path.isEmpty()) {
        return false;
    }
    if (!path.endsWith(QStringLiteral(".json"), Qt::CaseInsensitive)) {
        path += QStringLiteral(".peakemi-campaign.json");
    }
    m_path = path;
    return saveCampaign();
}

void CampaignDock::editDetails()
{
    if (!m_campaign) {
        emit statusMessage(tr("No campaign is open."));
        return;
    }
    QDialog dialog{this};
    dialog.setWindowTitle(tr("Campaign details"));
    auto* form = new QFormLayout{&dialog};
    const auto line = [&dialog](const std::string& value) {
        auto* edit = new QLineEdit{qs(value), &dialog};
        edit->setMinimumWidth(360);
        return edit;
    };
    auto* title = line(m_campaign->title);
    auto* standard = line(m_campaign->standard);
    auto* eutName = line(m_campaign->eutName);
    auto* eutSerial = line(m_campaign->eutSerial);
    auto* mode = line(m_campaign->eutOperatingMode);
    auto* description = line(m_campaign->eutDescription);
    auto* customer = line(m_campaign->customer);
    auto* location = line(m_campaign->location);
    auto* notes = new QPlainTextEdit{qs(m_campaign->notes), &dialog};
    notes->setMaximumHeight(90);
    form->addRow(tr("Title"), title);
    form->addRow(tr("Standard"), standard);
    form->addRow(tr("Equipment under test"), eutName);
    form->addRow(tr("Serial number"), eutSerial);
    form->addRow(tr("Operating mode"), mode);
    form->addRow(tr("Description"), description);
    form->addRow(tr("Customer"), customer);
    form->addRow(tr("Test location"), location);
    form->addRow(tr("Notes"), notes);

    const bool readOnly = isLocked();
    for (auto* edit : {title, standard, eutName, eutSerial, mode, description, customer, location})
    {
        edit->setReadOnly(readOnly);
    }
    notes->setReadOnly(readOnly);

    auto* buttons = new QDialogButtonBox{readOnly ? QDialogButtonBox::Close
                                                  : QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                         &dialog};
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted || readOnly) {
        return;
    }
    m_campaign->title = str(title->text());
    m_campaign->standard = str(standard->text());
    m_campaign->eutName = str(eutName->text());
    m_campaign->eutSerial = str(eutSerial->text());
    m_campaign->eutOperatingMode = str(mode->text());
    m_campaign->eutDescription = str(description->text());
    m_campaign->customer = str(customer->text());
    m_campaign->location = str(location->text());
    m_campaign->notes = str(notes->toPlainText());
    markDirty();
}

void CampaignDock::editSignoff()
{
    if (!m_campaign) {
        emit statusMessage(tr("No campaign is open."));
        return;
    }
    auto& campaign = *m_campaign;
    QDialog dialog{this};
    dialog.setWindowTitle(tr("Sign-off"));
    auto* layout = new QVBoxLayout{&dialog};
    auto* form = new QFormLayout;
    layout->addLayout(form);

    const auto signedText = [](const Signoff& signoff) {
        return signoff.isSigned() ? tr("signed %1").arg(when(signoff.at)) : tr("not signed");
    };
    auto* tester = new QLineEdit{qs(campaign.testedBy.name), &dialog};
    auto* reviewer = new QLineEdit{qs(campaign.reviewedBy.name), &dialog};
    tester->setMinimumWidth(260);
    auto* testerRow = new QHBoxLayout;
    testerRow->addWidget(tester, 1);
    testerRow->addWidget(new QLabel{signedText(campaign.testedBy), &dialog});
    auto* reviewerRow = new QHBoxLayout;
    reviewerRow->addWidget(reviewer, 1);
    reviewerRow->addWidget(new QLabel{signedText(campaign.reviewedBy), &dialog});
    form->addRow(tr("Tested by"), testerRow);
    form->addRow(tr("Reviewed by"), reviewerRow);

    auto* lock = new QCheckBox{tr("Close and lock the campaign"), &dialog};
    lock->setChecked(campaign.locked);
    layout->addWidget(lock);
    auto* hint = new QLabel{&dialog};
    hint->setWordWrap(true);
    if (campaign.locked) {
        hint->setText(tr("The campaign is locked. Clear the box to reopen it; the review is "
                         "then withdrawn and has to be signed again."));
    } else if (auto ready = campaign.canLock(); !ready) {
        hint->setText(tr("Locking needs every test to have a verdict and both signatures. "
                         "Now: %1.")
                          .arg(qs(ready.error().detail)));
    } else {
        hint->setText(tr("Every test has a verdict. A locked campaign can no longer be edited."));
    }
    layout->addWidget(hint);
    tester->setReadOnly(campaign.locked);
    reviewer->setReadOnly(campaign.locked);

    auto* buttons = new QDialogButtonBox{QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog};
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    if (campaign.locked) {
        if (!lock->isChecked()) {
            campaign.unlock();
            markDirty();
            refreshDetails();
            emit statusMessage(tr("Campaign reopened; the review was withdrawn."));
        }
        return;
    }

    const auto sign = [](Signoff& signoff, const QString& name) {
        const auto trimmed = name.trimmed();
        if (trimmed.isEmpty()) {
            signoff = Signoff{};
        } else if (str(trimmed) != signoff.name) {
            signoff = Signoff{.name = str(trimmed), .at = std::chrono::system_clock::now()};
        }
    };
    sign(campaign.testedBy, tester->text());
    sign(campaign.reviewedBy, reviewer->text());
    if (lock->isChecked()) {
        if (auto status = campaign.lock(); !status) {
            QMessageBox::information(this, tr("Cannot lock yet"), qs(status.error().detail));
        } else {
            emit statusMessage(tr("Campaign closed and locked."));
        }
    }
    markDirty();
    refreshDetails();
}

void CampaignDock::markDirty()
{
    m_dirty = true;
    refreshHeader();
}

void CampaignDock::refreshHeader()
{
    if (!m_campaign) {
        m_title->setText(tr("No campaign open. Start one from a standard with <b>New</b>, or "
                            "from the Campaign menu."));
        m_overall->hide();
        return;
    }
    const auto& campaign = *m_campaign;
    QStringList parts;
    if (!campaign.standard.empty()) {
        parts << qs(campaign.standard).toHtmlEscaped();
    }
    if (!campaign.eutName.empty()) {
        parts << qs(campaign.eutName).toHtmlEscaped();
    }
    parts << tr("%1 of %2 tests done").arg(campaign.completedCount()).arg(campaign.tests.size());
    if (campaign.locked) {
        parts << tr("locked");
    }
    m_title->setText(QStringLiteral("<b>%1%2</b><br><span>%3</span>")
                         .arg(qs(campaign.title).toHtmlEscaped(),
                              m_dirty ? QStringLiteral("*") : QString{},
                              parts.join(QStringLiteral(" · "))));

    const auto verdict = campaign.overallVerdict();
    const auto colours = verdictColours(verdict, isDark(this));
    m_overall->setText(testVerdictText(verdict).toUpper());
    m_overall->setStyleSheet(
        QStringLiteral("QLabel { background: %1; color: %2; border-radius: 9px; padding: 2px 12px; "
                       "font-weight: 600; }")
            .arg(colours.background.name(), colours.foreground.name()));
    m_overall->show();
}

void CampaignDock::refreshTests()
{
    const int current = m_tests->currentRow();
    m_updating = true;
    const int rows = m_campaign ? static_cast<int>(m_campaign->tests.size()) : 0;
    m_tests->setRowCount(rows);
    for (int row = 0; row < rows; ++row) {
        refreshTestRow(row);
    }
    if (current >= 0 && current < rows) {
        m_tests->selectRow(current);
    }
    m_updating = false;
}

void CampaignDock::refreshTestRow(int row)
{
    if (!m_campaign || row < 0 || static_cast<std::size_t>(row) >= m_campaign->tests.size()) {
        return;
    }
    const auto& test = m_campaign->tests[static_cast<std::size_t>(row)];
    const bool dark = isDark(this);
    const auto cell = [this, row](int column) {
        auto* item = m_tests->item(row, column);
        if (item == nullptr) {
            item = new QTableWidgetItem;
            m_tests->setItem(row, column, item);
        }
        return item;
    };

    cell(ColumnName)->setText(qs(test.name));
    cell(ColumnPort)->setText(qs(test.port));

    const auto paint = [&](QTableWidgetItem* item, TestVerdict verdict, bool bold) {
        const auto colours = verdictColours(verdict, dark);
        item->setText(testVerdictText(verdict));
        item->setBackground(colours.background);
        item->setForeground(colours.foreground);
        QFont font = item->font();
        font.setBold(bold);
        item->setFont(font);
    };
    paint(cell(ColumnMeasured), test.measuredVerdict(), false);
    paint(cell(ColumnFinal), test.finalVerdict(), true);
    if (test.isOverridden()) {
        cell(ColumnFinal)->setText(testVerdictText(test.finalVerdict()) + QStringLiteral(" *"));
        cell(ColumnFinal)->setToolTip(tr("Overridden: %1").arg(qs(test.overrideReason)));
    } else {
        cell(ColumnFinal)->setToolTip(QString{});
    }

    QStringList notes;
    if (test.ambient) {
        notes << tr("ambient");
    }
    if (!test.comments.empty()) {
        notes << tr("comment");
    }
    if (!test.photos.empty()) {
        notes << tr("%n photo(s)", nullptr, static_cast<int>(test.photos.size()));
    }
    if (test.testedBy.isSigned()) {
        notes << tr("signed");
    }
    cell(ColumnNotes)->setText(notes.join(QStringLiteral(", ")));
    refreshHeader();
}

void CampaignDock::refreshDetails()
{
    const auto* test = activeTest();
    m_updating = true;
    if (test == nullptr) {
        for (auto* label : {m_measured, m_worstMargin, m_signedAt, m_overrideHint}) {
            label->clear();
        }
        m_override->setChecked(false);
        m_overrideReason->clear();
        m_comments->clear();
        m_testedBy->clear();
        m_name->clear();
        m_port->clear();
        m_procedure->clear();
    } else {
        m_measured->setText(testVerdictText(test->measuredVerdict()));
        const auto worst = test->worstResult();
        m_worstMargin->setText(
            worst ? tr("%1 dB at %2 MHz")
                        .arg(worst->marginDb, 0, 'f', 1)
                        .arg(static_cast<double>(worst->frequency.value()) / 1e6, 0, 'f', 3)
                  : tr("–"));
        m_override->setChecked(test->isOverridden());
        if (test->overrideVerdict) {
            m_overrideVerdict->setCurrentIndex(
                m_overrideVerdict->findData(static_cast<int>(*test->overrideVerdict)));
        }
        m_overrideReason->setText(qs(test->overrideReason));
        m_overrideHint->clear();
        if (m_comments->toPlainText() != qs(test->comments)) {
            m_comments->setPlainText(qs(test->comments));
        }
        m_testedBy->setText(qs(test->testedBy.name));
        m_signedAt->setText(test->testedBy.isSigned() ? tr("signed %1").arg(when(test->testedBy.at))
                                                      : tr("not signed"));
        m_name->setText(qs(test->name));
        m_port->setText(qs(test->port));
        if (m_procedure->toPlainText() != qs(test->procedure)) {
            m_procedure->setPlainText(qs(test->procedure));
        }
    }
    m_updating = false;
    refreshEquipment();
    refreshPhotos();
    updateEnabledState();
}

void CampaignDock::refreshEquipment()
{
    const auto* test = activeTest();
    m_setupLabel->setText(test != nullptr && !test->setupName.empty()
                              ? tr("Set-up: <b>%1</b>").arg(qs(test->setupName).toHtmlEscaped())
                              : tr("No saved set-up applied"));
    m_updating = true;
    const int rows = test != nullptr ? static_cast<int>(test->equipment.size()) : 0;
    m_equipment->setRowCount(rows);
    const auto now = std::chrono::system_clock::now();
    for (int row = 0; row < rows; ++row) {
        const auto& entry = test->equipment[static_cast<std::size_t>(row)];
        const std::array<std::string, EquipmentColumnCount> values{
            entry.role, entry.model, entry.serial, entry.calibrationDue};
        for (int column = 0; column < EquipmentColumnCount; ++column) {
            auto* item = m_equipment->item(row, column);
            if (item == nullptr) {
                item = new QTableWidgetItem;
                m_equipment->setItem(row, column, item);
            }
            item->setText(qs(values[static_cast<std::size_t>(column)]));
        }
        auto* due = m_equipment->item(row, ColumnCalibration);
        if (entry.calibrationExpired(now)) {
            due->setForeground(verdictColours(TestVerdict::Fail, isDark(this)).foreground);
            due->setToolTip(tr("Calibration expired"));
        } else {
            due->setForeground(palette().color(QPalette::Text));
            due->setToolTip(QString{});
        }
    }
    m_updating = false;
}

void CampaignDock::refreshPhotos()
{
    const auto* test = activeTest();
    m_updating = true;
    m_photos->clear();
    if (test != nullptr) {
        for (const auto& photo : test->photos) {
            m_photos->addItem(new QListWidgetItem{thumbnail(photo), qs(photo.caption)});
        }
    }
    m_caption->clear();
    m_updating = false;
}

void CampaignDock::updateEnabledState()
{
    const bool open = hasCampaign();
    const bool editable = open && !isLocked() && !m_running;
    const bool selected = activeTest() != nullptr;
    const bool canEdit = editable && selected;

    m_tests->setEnabled(open && !m_running);
    m_addTest->setEnabled(editable);
    m_removeTest->setEnabled(canEdit);
    m_run->setEnabled(canEdit && m_connected);
    m_ambient->setEnabled(canEdit && m_connected);
    m_details->setEnabled(open && selected);

    m_override->setEnabled(canEdit);
    m_overrideVerdict->setEnabled(canEdit && m_override->isChecked());
    m_overrideReason->setEnabled(canEdit && m_override->isChecked());
    m_comments->setReadOnly(!canEdit);
    m_testedBy->setReadOnly(!canEdit);
    m_sign->setEnabled(canEdit);
    m_name->setReadOnly(!canEdit);
    m_port->setReadOnly(!canEdit);
    m_procedure->setReadOnly(!canEdit);
    m_equipment->setEditTriggers(canEdit ? QAbstractItemView::DoubleClicked |
                                               QAbstractItemView::EditKeyPressed |
                                               QAbstractItemView::AnyKeyPressed
                                         : QAbstractItemView::NoEditTriggers);
    m_addEquipment->setEnabled(canEdit);
    m_removeEquipment->setEnabled(canEdit && m_equipment->currentRow() >= 0);
    m_copyEquipment->setEnabled(canEdit);
    m_applySetup->setEnabled(canEdit);
    m_saveSetup->setEnabled(open && selected && !m_running);
    m_addPhotos->setEnabled(canEdit);
    m_removePhoto->setEnabled(canEdit && m_photos->currentRow() >= 0);
    m_caption->setReadOnly(!canEdit);
}

void CampaignDock::applyOverride()
{
    auto* test = mutableActiveTest();
    if (m_updating || test == nullptr) {
        return;
    }
    if (!m_override->isChecked()) {
        test->clearOverride();
        m_overrideHint->clear();
    } else {
        const auto verdict = static_cast<TestVerdict>(m_overrideVerdict->currentData().toInt());
        if (auto status = test->overrideWith(verdict, str(m_overrideReason->text())); !status) {
            test->clearOverride();
            m_overrideHint->setText(tr("Enter a reason: an override without one is not applied, "
                                       "and the measured verdict stands."));
        } else {
            m_overrideHint->clear();
        }
    }
    markDirty();
    refreshTestRow(m_tests->currentRow());
    updateEnabledState();
}

void CampaignDock::addTest(TestKind kind)
{
    if (!m_campaign || isLocked()) {
        return;
    }
    auto test = makeCampaignTest(kind, str(defaultTestName(kind)));
    // A new test of a kind the plan already has starts from its sibling's
    // limits and equipment, which is what a second polarisation or line needs.
    for (const auto& existing : m_campaign->tests) {
        if (existing.kind == kind) {
            test.config = existing.config;
            test.equipment = existing.equipment;
            break;
        }
    }
    m_campaign->tests.push_back(std::move(test));
    refreshTests();
    selectTest(static_cast<int>(m_campaign->tests.size()) - 1);
    markDirty();
    m_details->setCurrentIndex(1);
    m_name->setFocus();
    m_name->selectAll();
}

void CampaignDock::removeTest()
{
    const auto* test = activeTest();
    if (test == nullptr || isLocked()) {
        return;
    }
    if (test->session &&
        QMessageBox::question(this,
                              tr("Remove test"),
                              tr("'%1' has a measurement. Remove the test and its measurement?")
                                  .arg(qs(test->name))) != QMessageBox::Yes)
    {
        return;
    }
    const int row = m_tests->currentRow();
    m_campaign->tests.erase(m_campaign->tests.begin() + row);
    refreshTests();
    selectTest(std::min(row, static_cast<int>(m_campaign->tests.size()) - 1));
    markDirty();
}

void CampaignDock::addEquipment()
{
    auto* test = mutableActiveTest();
    if (test == nullptr) {
        return;
    }
    test->equipment.push_back(EquipmentItem{});
    refreshEquipment();
    const int row = static_cast<int>(test->equipment.size()) - 1;
    m_equipment->setCurrentCell(row, ColumnRole);
    m_equipment->editItem(m_equipment->item(row, ColumnRole));
    markDirty();
}

void CampaignDock::removeEquipment()
{
    auto* test = mutableActiveTest();
    const int row = m_equipment->currentRow();
    if (test == nullptr || row < 0 || static_cast<std::size_t>(row) >= test->equipment.size()) {
        return;
    }
    test->equipment.erase(test->equipment.begin() + row);
    refreshEquipment();
    markDirty();
}

void CampaignDock::copyEquipmentToSimilarTests()
{
    auto* test = mutableActiveTest();
    if (test == nullptr) {
        return;
    }
    int copied = 0;
    for (auto& other : m_campaign->tests) {
        if (&other != test && other.kind == test->kind) {
            other.equipment = test->equipment;
            ++copied;
        }
    }
    markDirty();
    emit statusMessage(tr("Equipment list copied to %n other test(s).", nullptr, copied));
}

void CampaignDock::addPhotos()
{
    auto* test = mutableActiveTest();
    if (test == nullptr) {
        return;
    }
    const auto paths = QFileDialog::getOpenFileNames(
        this, tr("Add setup photos"), QString{}, tr("Images (*.jpg *.jpeg *.png *.bmp)"));
    int added = 0;
    for (const auto& path : paths) {
        if (auto photo = photoFromFile(path)) {
            test->photos.push_back(std::move(*photo));
            ++added;
        } else {
            emit statusMessage(tr("Could not read %1 as an image.").arg(path));
        }
    }
    if (added > 0) {
        refreshPhotos();
        refreshTestRow(m_tests->currentRow());
        markDirty();
    }
}

void CampaignDock::removePhoto()
{
    auto* test = mutableActiveTest();
    const int row = m_photos->currentRow();
    if (test == nullptr || row < 0 || static_cast<std::size_t>(row) >= test->photos.size()) {
        return;
    }
    test->photos.erase(test->photos.begin() + row);
    refreshPhotos();
    refreshTestRow(m_tests->currentRow());
    markDirty();
}

} // namespace peakemi::ui
