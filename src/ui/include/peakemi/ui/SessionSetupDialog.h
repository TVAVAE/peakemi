#pragma once

#include <peakemi/core/TestSetup.h>

#include <QDialog>

#include <optional>
#include <vector>

class QCheckBox;
class QLabel;
class QListWidget;
class QPushButton;

namespace peakemi::ui {

class SetupDiagramView;

/// Asked when a new session starts: which bench is it measured on?
///
/// The set-ups come from the library; the chosen one's corrections go into the
/// run configuration, and a copy of it into the session, so its report shows
/// the bench as it was. "Edit set-ups..." opens the library on top.
class SessionSetupDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SessionSetupDialog(QWidget* parent = nullptr);

    /// Whether the library holds any set-up to choose.
    [[nodiscard]] bool hasSetups() const { return !m_setups.empty(); }

    /// The set-up chosen; empty when the session starts without one.
    [[nodiscard]] const std::optional<TestSetup>& chosen() const { return m_chosen; }

    /// Whether to ask at all when a new session starts.
    [[nodiscard]] static bool askOnNewSession();

private:
    void reload(const std::string& selectId);
    void showSelected();
    void editLibrary();

    std::vector<TestSetup> m_setups;
    std::optional<TestSetup> m_chosen;

    QListWidget* m_list{nullptr};
    SetupDiagramView* m_diagram{nullptr};
    QLabel* m_summary{nullptr};
    QCheckBox* m_ask{nullptr};
    QPushButton* m_use{nullptr};
};

} // namespace peakemi::ui
