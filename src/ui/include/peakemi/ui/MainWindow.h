#pragma once

#include <peakemi/core/IReportRenderer.h>
#include <peakemi/core/MeasurementEngine.h>
#include <peakemi/core/PeakDetector.h>
#include <peakemi/core/Session.h>
#include <peakemi/python/PluginRegistry.h>

#include <QMainWindow>
#include <QString>

#include <cstddef>
#include <cstdint>
#include <optional>

class QAction;
class QLabel;
class QProgressBar;

#include <peakemi/core/TestSetup.h>

namespace peakemi::ui {

class CampaignDock;
class InstrumentDock;
class LogDock;
class PainterPlotBackend;
class ResultsDock;
class RunConfigDock;
class RunController;

/// Application main window: the Widgets shell of ADR-1.
///
/// It wires the docks to the run controller and owns the current session. It
/// contains no measurement logic — everything it does is translate a user
/// action into a controller command and a controller signal into a view update.
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    [[nodiscard]] const Session& session() const { return m_session; }

    /// Loads a session file at start-up, e.g. from the command line.
    void openSessionFile(const QString& path);

protected:
    void closeEvent(QCloseEvent* event) override;
    void changeEvent(QEvent* event) override;

private slots:
    void newSession();
    void openSession();
    bool saveSession();
    bool saveSessionAs();
    void editSessionMetadata();

    void exportResultsCsv();
    void exportResultsJson();
    void editReportTemplate();
    void showPluginManager();
    void exportTraceCsv();
    void exportPlotImage();
    void exportPdfReport();

    void onConnectRequested(const peakemi::TransportDescriptor& descriptor,
                            const QString& driverId);
    void onConnectionChanged(bool connected);
    void onPhaseChanged(peakemi::MeasurementEngine::Phase phase);
    void onTraceAcquired(const peakemi::TracePtr& trace);
    void onPeaksFlagged(std::vector<peakemi::PeakCandidate> peaks);
    void onPointMeasured(const peakemi::MeasurementPoint& point);
    void onProgress(int completed, int total, qint64 estimatedRemainingMs);
    void onRunFailed(const peakemi::Error& error);
    void onRunFinished(const peakemi::Session& session);

    void startRun();
    void togglePause();
    void abortRun();

    void showAboutDialog();

    void onTestSelected(const peakemi::CampaignTest& test);
    void runCampaignTest();
    void runAmbientScan();
    void exportCampaignReport();
    void openSetupLibrary();

private:
    void createActions();
    void createMenus();
    void createToolBar();
    void createDocks();
    void createStatusBar();
    void restoreLayout();
    void saveLayout();

    void updateActionStates();
    void updateWindowTitle();
    void refreshThemeIcons();
    void setInstrumentStatus(const QString& text, bool connected);
    void updatePlotSettings();
    void showLimitOverlays();
    void refreshMarkers();
    /// The set-up library, with @p unsaved added for editing when given.
    void showSetupLibrary(const std::optional<TestSetup>& unsaved);
    /// To the selected campaign test, or else to the run configuration.
    void applySetupFromLibrary(const TestSetup& setup);
    void log(const QString& message);
    [[nodiscard]] bool confirmDiscardChanges();
    [[nodiscard]] QString autosavePathFor(const QString& runId) const;

    RunController* m_controller{nullptr};
    PainterPlotBackend* m_plot{nullptr};

    InstrumentDock* m_instrumentDock{nullptr};
    RunConfigDock* m_configDock{nullptr};
    ResultsDock* m_resultsDock{nullptr};
    LogDock* m_logDock{nullptr};

    QAction* m_newAction{nullptr};
    QAction* m_openAction{nullptr};
    QAction* m_saveAction{nullptr};
    QAction* m_saveAsAction{nullptr};
    QAction* m_metadataAction{nullptr};
    QAction* m_exportResultsAction{nullptr};
    QAction* m_exportTraceAction{nullptr};
    QAction* m_exportPlotAction{nullptr};
    QAction* m_exportResultsJsonAction{nullptr};
    QAction* m_reportTemplateAction{nullptr};
    QAction* m_reportAction{nullptr};
    QAction* m_quitAction{nullptr};
    QAction* m_startAction{nullptr};
    QAction* m_pauseAction{nullptr};
    QAction* m_abortAction{nullptr};
    QAction* m_disconnectAction{nullptr};
    QAction* m_logFrequencyAction{nullptr};
    QAction* m_autoScaleAction{nullptr};
    QAction* m_pluginManagerAction{nullptr};
    QAction* m_aboutAction{nullptr};
    QAction* m_aboutQtAction{nullptr};
    QAction* m_setupsAction{nullptr};

    QLabel* m_instrumentStatus{nullptr};
    QLabel* m_cursorStatus{nullptr};
    QLabel* m_phaseStatus{nullptr};
    QProgressBar* m_progress{nullptr};

    CampaignDock* m_campaignDock{nullptr};
    /// What the run in progress was started for, when it is a campaign's.
    enum class CampaignRun : std::uint8_t
    {
        None,
        Test,
        Ambient
    };
    CampaignRun m_campaignRun{CampaignRun::None};
    /// Used instead of the run configuration dock for the next run: the
    /// ambient scan measures the test's configuration without Phase 2.
    std::optional<RunConfiguration> m_runOverride;
    /// The first of m_session.traces that belongs to the run in progress.
    std::size_t m_runFirstTrace{0};
    /// Set while a campaign test's configuration is loaded into the dock, so
    /// loading it is not taken for an edit of it.
    bool m_loadingTest{false};

    std::vector<PeakCandidate> m_peaks;
    Hertz m_selectedFrequency{0};
    python::PluginRegistry m_plugins;
    ReportTemplate m_reportTemplate;
    Session m_session;
    QString m_sessionPath;
    bool m_dirty{false};
    bool m_connected{false};
    MeasurementEngine::Phase m_phase{MeasurementEngine::Phase::Idle};
};

} // namespace peakemi::ui
