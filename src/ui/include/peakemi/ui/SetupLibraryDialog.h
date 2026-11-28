#pragma once

#include <peakemi/core/IReportRenderer.h>
#include <peakemi/core/TestSetup.h>

#include <QDialog>
#include <QString>

#include <vector>

class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QTableWidget;

namespace peakemi::ui {

class SetupDiagramView;

/// The folder the set-up library lives in: the one chosen in the dialog, else
/// the application's own.
[[nodiscard]] QString setupLibraryDirectory();

/// The saved test set-ups, and an editor for the selected one.
///
/// A set-up is documented as its signal chain, from the equipment under test
/// to the analyzer: each device with its model, serial number, calibration
/// due date, notes and correction. Saved set-ups can be printed as a sheet,
/// exported and imported, and applied: to the selected campaign test, or to
/// the run configuration.
class SetupLibraryDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SetupLibraryDialog(QWidget* parent = nullptr);
    ~SetupLibraryDialog() override;

    /// What the Apply button applies to, e.g. "test 'Radiated -- horizontal'";
    /// empty hides it.
    void setApplyTarget(const QString& target);
    void setReportTemplate(const ReportTemplate& reportTemplate);
    /// Add a set-up that is not in the library yet -- one made from a test --
    /// and select it for editing before it is saved.
    void addUnsavedSetup(TestSetup setup);

signals:
    void applyRequested(const peakemi::TestSetup& setup);

protected:
    void reject() override;

private:
    void buildUi();
    void reload();
    void refreshList();
    void selectRow(int row);
    void showCurrent();
    void refreshDevices();
    void refreshDiagram();
    void switchSlot(int slot, bool inUse);
    void refreshSummary();
    void updateEnabledState();
    void markDirty();
    [[nodiscard]] bool confirmLeave();
    [[nodiscard]] TestSetup* current();

    void newSetup(SetupMethod method);
    void duplicateSetup();
    void deleteSetup();
    bool saveCurrent();
    void importSetups();
    void exportCurrent();
    void printSheet();
    void chooseFolder();
    void applyCurrent();

    void addDevice(const QString& role);
    void removeDevice();
    void moveDevice(int offset);
    void setFixedCorrection();
    void importCorrection();
    void removeCorrection();

    std::vector<TestSetup> m_setups;
    int m_current{-1};
    bool m_dirty{false};
    /// The current set-up has never been written to the library.
    bool m_unsaved{false};
    bool m_updating{false};
    ReportTemplate m_reportTemplate;

    QLabel* m_folder{nullptr};
    QListWidget* m_list{nullptr};
    QPushButton* m_new{nullptr};
    QPushButton* m_duplicate{nullptr};
    QPushButton* m_delete{nullptr};
    QPushButton* m_export{nullptr};

    QLineEdit* m_name{nullptr};
    QComboBox* m_method{nullptr};
    QPlainTextEdit* m_description{nullptr};
    SetupDiagramView* m_diagram{nullptr};
    QTableWidget* m_devices{nullptr};
    QPushButton* m_addDevice{nullptr};
    QPushButton* m_removeDevice{nullptr};
    QPushButton* m_up{nullptr};
    QPushButton* m_down{nullptr};
    QPushButton* m_correction{nullptr};
    QLabel* m_summary{nullptr};

    QPushButton* m_print{nullptr};
    QPushButton* m_save{nullptr};
    QPushButton* m_apply{nullptr};
};

} // namespace peakemi::ui
