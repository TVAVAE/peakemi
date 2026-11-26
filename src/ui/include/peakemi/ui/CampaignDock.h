#pragma once

#include <peakemi/core/Campaign.h>

#include <QDockWidget>
#include <QString>

#include <optional>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QMenu;
class QPlainTextEdit;
class QPushButton;
class QTabWidget;
class QTableWidget;

namespace peakemi::ui {

/// The test plan of a campaign, worked through the way a test lab does.
///
/// Left, the tests with their measured and final verdicts; right, the selected
/// test: verdict override with its mandatory reason, comments and sign-off,
/// setup and method, equipment with calibration dates, and setup photos. The
/// campaign itself -- EUT details, sign-off and lock -- is edited through the
/// dock's menu.
///
/// The dock owns the campaign. The main window owns the measuring: it asks
/// which test is active, runs it, and hands the session (or the ambient scan)
/// back with attachSession() or attachAmbient().
class CampaignDock : public QDockWidget
{
    Q_OBJECT

public:
    explicit CampaignDock(QWidget* parent = nullptr);
    ~CampaignDock() override;

    [[nodiscard]] bool hasCampaign() const { return m_campaign.has_value(); }

    [[nodiscard]] const Campaign* campaign() const;
    [[nodiscard]] bool isLocked() const;

    [[nodiscard]] bool isDirty() const { return m_dirty; }

    /// The selected test, if a campaign is open and a test is selected.
    [[nodiscard]] const CampaignTest* activeTest() const;

    /// Replace the open campaign without asking, e.g. for a screenshot.
    void setCampaign(Campaign campaign, const QString& path = {});
    void selectTest(int row);

    /// The run configuration the active test is measured with, as edited in
    /// the run configuration dock.
    void setActiveTestConfiguration(const RunConfiguration& config);
    /// File the measurement just made under the active test.
    void attachSession(const Session& session);
    /// File an ambient (EUT off) scan under the active test.
    void attachAmbient(const Trace& trace);

    /// Whether an instrument is connected and whether a run is in progress:
    /// run buttons and the test selection follow.
    void setRunState(bool connected, bool running);

    /// The "Campaign" menu, for the main window's menu bar.
    [[nodiscard]] QMenu* menu() const { return m_menu; }

    /// Ask before an unsaved campaign is replaced or the application quits.
    [[nodiscard]] bool confirmDiscard();

public slots:
    void newCampaign(const QString& templateId);
    void openCampaign();
    bool saveCampaign();
    bool saveCampaignAs();
    void editDetails();
    void editSignoff();

signals:
    /// A test was selected: show its configuration and its measurement.
    void testSelected(const peakemi::CampaignTest& test);
    void runTestRequested();
    void ambientRequested();
    void reportRequested();
    void statusMessage(const QString& message);

private:
    void buildMenu();
    [[nodiscard]] QWidget* buildResultTab();
    [[nodiscard]] QWidget* buildSetupTab();
    [[nodiscard]] QWidget* buildEquipmentTab();
    [[nodiscard]] QWidget* buildPhotosTab();

    [[nodiscard]] CampaignTest* mutableActiveTest();
    void markDirty();
    void refreshHeader();
    void refreshTests();
    void refreshTestRow(int row);
    void refreshDetails();
    void refreshEquipment();
    void refreshPhotos();
    void updateEnabledState();
    void applyOverride();

    void addTest(TestKind kind);
    void removeTest();
    void addEquipment();
    void removeEquipment();
    void copyEquipmentToSimilarTests();
    void addPhotos();
    void removePhoto();

    std::optional<Campaign> m_campaign;
    QString m_path;
    bool m_dirty{false};
    bool m_connected{false};
    bool m_running{false};
    /// Set while widgets are filled from the model, so their change signals
    /// are not taken for edits.
    bool m_updating{false};

    QMenu* m_menu{nullptr};
    QLabel* m_title{nullptr};
    QLabel* m_overall{nullptr};
    QTableWidget* m_tests{nullptr};
    QPushButton* m_addTest{nullptr};
    QPushButton* m_removeTest{nullptr};
    QPushButton* m_ambient{nullptr};
    QPushButton* m_run{nullptr};
    QTabWidget* m_details{nullptr};

    // Result tab.
    QLabel* m_measured{nullptr};
    QLabel* m_worstMargin{nullptr};
    QCheckBox* m_override{nullptr};
    QComboBox* m_overrideVerdict{nullptr};
    QLineEdit* m_overrideReason{nullptr};
    QLabel* m_overrideHint{nullptr};
    QPlainTextEdit* m_comments{nullptr};
    QLineEdit* m_testedBy{nullptr};
    QPushButton* m_sign{nullptr};
    QLabel* m_signedAt{nullptr};

    // Setup tab.
    QLineEdit* m_name{nullptr};
    QLineEdit* m_port{nullptr};
    QPlainTextEdit* m_procedure{nullptr};

    // Equipment tab.
    QTableWidget* m_equipment{nullptr};
    QPushButton* m_addEquipment{nullptr};
    QPushButton* m_removeEquipment{nullptr};
    QPushButton* m_copyEquipment{nullptr};

    // Photos tab.
    QListWidget* m_photos{nullptr};
    QLineEdit* m_caption{nullptr};
    QPushButton* m_addPhotos{nullptr};
    QPushButton* m_removePhoto{nullptr};
};

/// Display text for a verdict.
[[nodiscard]] QString testVerdictText(TestVerdict verdict);

} // namespace peakemi::ui
