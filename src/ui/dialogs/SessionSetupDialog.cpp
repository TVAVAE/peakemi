#include <peakemi/core/SetupLibrary.h>
#include <peakemi/ui/SessionSetupDialog.h>
#include <peakemi/ui/SetupDiagramView.h>
#include <peakemi/ui/SetupLibraryDialog.h>

#include <QCheckBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSettings>
#include <QSplitter>
#include <QStringList>
#include <QVBoxLayout>

#include <chrono>

namespace peakemi::ui {
namespace {

const QString AskKey = QStringLiteral("setups/askOnNewSession");

[[nodiscard]] QString qs(std::string_view text)
{
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

} // namespace

bool SessionSetupDialog::askOnNewSession()
{
    return QSettings{}.value(AskKey, true).toBool();
}

SessionSetupDialog::SessionSetupDialog(QWidget* parent) : QDialog{parent}
{
    setWindowTitle(tr("New session"));
    setObjectName(QStringLiteral("sessionSetupDialog"));
    resize(1040, 520);

    auto* layout = new QVBoxLayout{this};
    auto* question = new QLabel{tr("<b>Which bench is this session measured on?</b><br>Its "
                                   "corrections go into the run configuration, and the session "
                                   "keeps a copy of the set-up for its report."),
                                this};
    question->setWordWrap(true);
    layout->addWidget(question);

    auto* splitter = new QSplitter{Qt::Horizontal, this};
    layout->addWidget(splitter, 1);
    m_list = new QListWidget{splitter};
    m_list->setObjectName(QStringLiteral("sessionSetups"));
    auto* right = new QWidget{splitter};
    auto* rightLayout = new QVBoxLayout{right};
    rightLayout->setContentsMargins(6, 0, 0, 0);
    m_diagram = new SetupDiagramView{right};
    m_diagram->setEditable(false);
    rightLayout->addWidget(m_diagram, 1);
    m_summary = new QLabel{right};
    m_summary->setWordWrap(true);
    m_summary->setTextFormat(Qt::RichText);
    rightLayout->addWidget(m_summary);
    m_list->setMinimumWidth(200);
    splitter->setStretchFactor(0, 2);
    splitter->setStretchFactor(1, 7);
    splitter->setSizes({240, 780});
    connect(m_list, &QListWidget::currentRowChanged, this, &SessionSetupDialog::showSelected);
    connect(m_list, &QListWidget::itemDoubleClicked, this, [this] { m_use->click(); });

    auto* bottom = new QHBoxLayout;
    auto* edit = new QPushButton{tr("Edit set-ups…"), this};
    m_ask = new QCheckBox{tr("Ask when a new session starts"), this};
    m_ask->setChecked(askOnNewSession());
    auto* without = new QPushButton{tr("Without a set-up"), this};
    m_use = new QPushButton{tr("Use this set-up"), this};
    m_use->setDefault(true);
    m_use->setProperty("primary", true);
    auto* cancel = new QPushButton{tr("Cancel"), this};
    bottom->addWidget(edit);
    bottom->addWidget(m_ask);
    bottom->addStretch(1);
    bottom->addWidget(without);
    bottom->addWidget(m_use);
    bottom->addWidget(cancel);
    layout->addLayout(bottom);

    connect(edit, &QPushButton::clicked, this, &SessionSetupDialog::editLibrary);
    connect(m_ask, &QCheckBox::toggled, this, [](bool ask) { QSettings{}.setValue(AskKey, ask); });
    connect(without, &QPushButton::clicked, this, [this] {
        m_chosen.reset();
        accept();
    });
    connect(m_use, &QPushButton::clicked, this, [this] {
        const int row = m_list->currentRow();
        if (row < 0 || static_cast<std::size_t>(row) >= m_setups.size()) {
            return;
        }
        m_chosen = m_setups[static_cast<std::size_t>(row)];
        accept();
    });
    connect(cancel, &QPushButton::clicked, this, &SessionSetupDialog::reject);

    reload({});
}

void SessionSetupDialog::reload(const std::string& selectId)
{
    auto contents = SetupLibrary{setupLibraryDirectory()}.load();
    m_setups = std::move(contents.setups);
    m_list->clear();
    int select = m_setups.empty() ? -1 : 0;
    for (std::size_t i = 0; i < m_setups.size(); ++i) {
        const auto& setup = m_setups[i];
        auto* item = new QListWidgetItem{
            QStringLiteral("%1\n%2").arg(qs(setup.name), qs(setupMethodName(setup.method))),
            m_list};
        item->setToolTip(qs(setup.description));
        if (!selectId.empty() && setup.id == selectId) {
            select = static_cast<int>(i);
        }
    }
    m_list->setCurrentRow(select);
    showSelected();
}

void SessionSetupDialog::showSelected()
{
    const int row = m_list->currentRow();
    const bool any = row >= 0 && static_cast<std::size_t>(row) < m_setups.size();
    m_use->setEnabled(any);
    if (!any) {
        m_diagram->setSetup(nullptr);
        m_summary->setText(m_setups.empty() ? tr("The library holds no set-ups yet: make one with "
                                                 "<b>Edit set-ups…</b>.")
                                            : QString{});
        return;
    }
    const auto& setup = m_setups[static_cast<std::size_t>(row)];
    m_diagram->setSetup(&setup);

    QStringList parts;
    const auto equipment = setup.equipment();
    parts << tr("%n device(s) in use", nullptr, static_cast<int>(equipment.size()));
    parts << tr("%n correction(s)", nullptr, static_cast<int>(setup.corrections().size()));
    QString text = parts.join(QStringLiteral(", ")) + QLatin1Char('.');
    if (const int expired = setup.expiredCount(std::chrono::system_clock::now()); expired > 0) {
        text += QLatin1Char(' ') + tr("<b>%n device(s) out of calibration.</b>", nullptr, expired);
    }
    if (!setup.description.empty()) {
        text += QStringLiteral("<br>") + qs(setup.description).toHtmlEscaped();
    }
    m_summary->setText(text);
}

void SessionSetupDialog::editLibrary()
{
    const int row = m_list->currentRow();
    const std::string current = row >= 0 && static_cast<std::size_t>(row) < m_setups.size()
                                    ? m_setups[static_cast<std::size_t>(row)].id
                                    : std::string{};
    SetupLibraryDialog library{this};
    library.setApplyTarget(QString{});
    library.exec();
    reload(current);
}

} // namespace peakemi::ui
