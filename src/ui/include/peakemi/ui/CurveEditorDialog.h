#pragma once

#include <peakemi/core/CorrectionTable.h>
#include <peakemi/core/LimitLine.h>

#include <QDialog>

#include <cstdint>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;

namespace peakemi::ui {

class PainterPlotBackend;

/// A table editor for a limit line or a correction table, with a preview.
///
/// Points are entered as frequency in MHz and a value in dB; a limit line also
/// takes how it continues to the next point (log-frequency, linear or a step).
/// The result can be used in the run straight away or saved as CSV or JSON in
/// the format the importers read.
class CurveEditorDialog : public QDialog
{
    Q_OBJECT

public:
    enum class Mode : std::uint8_t
    {
        Limit,
        Correction
    };

    explicit CurveEditorDialog(Mode mode, QWidget* parent = nullptr);

    void setLimit(const LimitLine& limit);
    [[nodiscard]] LimitLine limit() const;

    void setCorrection(const CorrectionTable& table);
    [[nodiscard]] CorrectionTable correction() const;

    /// What the OK button says; "Use in run" unless the caller says otherwise.
    void setAcceptText(const QString& text);

protected:
    void accept() override;

private:
    void buildUi();
    void addRow(double megahertz, double value, Interpolation interpolation);
    void removeSelectedRows();
    void sortRows();
    void importFile();
    void saveFile();
    void refreshPreview();
    /// Why the table cannot be used as it is, or empty.
    [[nodiscard]] QString problem() const;

    Mode m_mode;
    /// What the editor does not show -- notes, class, distance -- kept from
    /// the limit or table it was given.
    LimitLine m_limitBase;
    CorrectionTable m_correctionBase;
    bool m_updating{false};

    QLineEdit* m_name{nullptr};
    QLineEdit* m_standard{nullptr};
    QComboBox* m_detector{nullptr};
    QComboBox* m_unit{nullptr};
    QComboBox* m_kind{nullptr};
    QCheckBox* m_enabled{nullptr};
    QTableWidget* m_points{nullptr};
    PainterPlotBackend* m_preview{nullptr};
    QLabel* m_problem{nullptr};
    QPushButton* m_accept{nullptr};
};

} // namespace peakemi::ui
