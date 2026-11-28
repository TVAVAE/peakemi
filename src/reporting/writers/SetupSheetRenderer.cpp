#include <peakemi/core/Time.h>
#include <peakemi/reporting/SetupDiagram.h>
#include <peakemi/reporting/SetupSheetRenderer.h>

#include <QFileInfo>
#include <QFont>
#include <QMarginsF>
#include <QPageLayout>
#include <QPageSize>
#include <QPdfWriter>
#include <QTextDocument>
#include <QUrl>

#include <algorithm>
#include <chrono>
#include <vector>

namespace peakemi::reporting {
namespace {

/// Where the sheet's rich text finds the diagram image.
constexpr auto DiagramUrl = "peakemi-setup:diagram";
/// Text width of an A4 page with the 2 cm margins print() adds, in the
/// document's pixels.
constexpr double PageWidth = 640.0;
/// The diagram is drawn at twice the size it is shown, so it stays sharp in print.
constexpr double DiagramScale = 2.0;

[[nodiscard]] QString qs(std::string_view text)
{
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

[[nodiscard]] QString escaped(std::string_view text)
{
    return qs(text).toHtmlEscaped().replace(QLatin1Char('\n'), QStringLiteral("<br>"));
}

[[nodiscard]] QString orDash(std::string_view text)
{
    return text.empty() ? QStringLiteral("&ndash;") : escaped(text);
}

[[nodiscard]] QString signedDb(double value)
{
    return QStringLiteral("%1%2 dB")
        .arg(value >= 0.0 ? QStringLiteral("+") : QString{})
        .arg(value, 0, 'f', 1);
}

[[nodiscard]] QString frequencyText(Hertz frequency)
{
    const auto value = static_cast<double>(frequency.value());
    if (value >= 1e9) {
        return QStringLiteral("%1 GHz").arg(value / 1e9, 0, 'g', 4);
    }
    if (value >= 1e6) {
        return QStringLiteral("%1 MHz").arg(value / 1e6, 0, 'g', 4);
    }
    return QStringLiteral("%1 kHz").arg(value / 1e3, 0, 'g', 4);
}

/// Where the chain's total correction is worth reading off for each kind of
/// test: the band edges and a couple of points between.
[[nodiscard]] std::vector<Hertz> checkpointsFor(TestKind kind)
{
    switch (kind) {
        case TestKind::Conducted:
            return {kilohertz(150), megahertz(1), megahertz(10), megahertz(30)};
        case TestKind::Radiated:
            return {megahertz(30), megahertz(100), megahertz(300), gigahertz(1.0)};
        case TestKind::Other:
            break;
    }
    return {kilohertz(150), megahertz(30), megahertz(300), gigahertz(1.0)};
}

} // namespace

QString describeCorrection(const CorrectionTable& table)
{
    if (table.points.empty()) {
        return QStringLiteral("&ndash;");
    }
    const auto [low, high] =
        std::minmax_element(table.points.begin(),
                            table.points.end(),
                            [](const auto& a, const auto& b) { return a.second < b.second; });
    if (high->second - low->second < 0.05) {
        return QStringLiteral("%1, flat").arg(signedDb(low->second));
    }
    return QStringLiteral("%1 to %2, %3 points")
        .arg(signedDb(low->second), signedDb(high->second))
        .arg(table.points.size());
}

QString correctionKindName(CorrectionKind kind)
{
    switch (kind) {
        case CorrectionKind::AntennaFactor:
            return QStringLiteral("Antenna factor");
        case CorrectionKind::CableLoss:
            return QStringLiteral("Cable loss");
        case CorrectionKind::LisnFactor:
            return QStringLiteral("LISN factor");
        case CorrectionKind::Attenuator:
            return QStringLiteral("Attenuation");
        case CorrectionKind::AmplifierGain:
            return QStringLiteral("Amplifier gain");
        case CorrectionKind::Other:
            break;
    }
    return QStringLiteral("Other");
}

QString SetupSheetRenderer::html(const TestSetup& setup, const ReportTemplate& reportTemplate) const
{
    const TimePoint asOf =
        m_generatedAt == TimePoint{} ? std::chrono::system_clock::now() : m_generatedAt;
    const auto iso = qs(toIso8601(asOf));
    const QString dated = iso.left(10);

    QString html = QStringLiteral(R"(<html><body style="font-size:9pt;">)");
    if (!reportTemplate.companyName.empty()) {
        html += QStringLiteral(R"(<p style="color:#5F6368;">%1</p>)")
                    .arg(escaped(reportTemplate.companyName));
    }
    html += QStringLiteral(R"(<h1 style="font-size:16pt;">Test set-up: %1</h1>)")
                .arg(escaped(setup.name));

    html += QStringLiteral(R"(<table cellpadding="3" width="100%">)");
    const auto row = [&html](const QString& key, const QString& value) {
        html +=
            QStringLiteral(R"(<tr><td style="color:#5F6368;" width="28%">%1</td><td>%2</td></tr>)")
                .arg(key, value);
    };
    row(QStringLiteral("Method"), qs(setupMethodName(setup.method)));
    row(QStringLiteral("Last changed"),
        setup.modifiedAt == TimePoint{} ? QStringLiteral("&ndash;")
                                        : qs(toIso8601(setup.modifiedAt)).left(10));
    row(QStringLiteral("Sheet printed"), dated);
    const int expired = setup.expiredCount(asOf);
    row(QStringLiteral("Calibration"),
        expired == 0 ? QStringLiteral("All dated calibrations current on %1").arg(dated)
                     : QStringLiteral(
                           R"(<b style="color:#C5221F;">%1 device(s) out of calibration on %2</b>)")
                           .arg(expired)
                           .arg(dated));
    html += QStringLiteral("</table>");

    if (!setup.description.empty()) {
        html += QStringLiteral("<h2>Description</h2><p>%1</p>").arg(escaped(setup.description));
    }

    // The diagram first: how the bench is built, then what it is built from.
    const SetupDiagram diagram{setup, false};
    html += QStringLiteral(R"(<h2>Diagram</h2><p><img src="%1" width="%2"></p>)")
                .arg(QString::fromLatin1(DiagramUrl))
                .arg(std::min(PageWidth, diagram.size().width()));

    html += QStringLiteral(
        R"(<h2>Signal chain</h2><p style="color:#5F6368;">The devices in use, from the equipment under test to the analyzer.</p>)");
    html += QStringLiteral(
        R"(<table border="1" cellspacing="0" cellpadding="3" width="100%"><tr style="background-color:#F1F3F4;"><th>#</th><th>Role</th><th>Model</th><th>Serial number</th><th>Calibration due</th><th>Correction</th><th>Notes</th></tr>)");
    int number = 0;
    for (const auto& device : setup.devices) {
        if (!device.inUse) {
            continue;
        }
        QString due = orDash(device.equipment.calibrationDue);
        if (device.equipment.calibrationExpired(asOf)) {
            due = QStringLiteral(
                      R"(<span style="color:#C5221F; font-weight:bold;">%1 &ndash; EXPIRED</span>)")
                      .arg(escaped(device.equipment.calibrationDue));
        }
        QString correction = QStringLiteral("&ndash;");
        if (device.correction) {
            correction =
                QStringLiteral("%1<br>%2%3")
                    .arg(correctionKindName(device.correction->kind),
                         describeCorrection(*device.correction),
                         device.correction->enabled ? QString{} : QStringLiteral(" (disabled)"));
        }
        html += QStringLiteral("<tr><td>%1</td><td>%2</td><td>%3</td><td>%4</td><td>%5</td><td>%6</"
                               "td><td>%7</td></tr>")
                    .arg(++number)
                    .arg(orDash(device.equipment.role),
                         orDash(device.equipment.model),
                         orDash(device.equipment.serial),
                         due,
                         correction,
                         orDash(device.notes));
    }
    if (number == 0) {
        html += QStringLiteral(R"(<tr><td colspan="7">No devices listed.</td></tr>)");
    }
    html += QStringLiteral("</table>");

    if (!setup.corrections().empty()) {
        html += QStringLiteral(
            R"(<h2>Total correction</h2><p style="color:#5F6368;">Added to every reading taken through this chain (gains subtracted).</p><table border="1" cellspacing="0" cellpadding="3"><tr style="background-color:#F1F3F4;">)");
        const auto checkpoints = checkpointsFor(setup.kind);
        for (const auto frequency : checkpoints) {
            html += QStringLiteral("<th>%1</th>").arg(frequencyText(frequency));
        }
        html += QStringLiteral("</tr><tr>");
        for (const auto frequency : checkpoints) {
            html += QStringLiteral("<td>%1</td>").arg(signedDb(setup.totalCorrectionAt(frequency)));
        }
        html += QStringLiteral("</tr></table>");
    }

    html += QStringLiteral(
                R"(<p style="color:#5F6368; font-size:8pt;">Set-up %1, PeakEmi.</p></body></html>)")
                .arg(escaped(setup.id));
    return html;
}

Status SetupSheetRenderer::render(const TestSetup& setup,
                                  const ReportTemplate& reportTemplate,
                                  const QString& path) const
{
    QTextDocument document;
    QFont font = document.defaultFont();
    font.setPointSize(9);
    document.setDefaultFont(font);
    document.setHtml(html(setup, reportTemplate));
    document.addResource(
        QTextDocument::ImageResource,
        QUrl{QString::fromLatin1(DiagramUrl)},
        SetupDiagram{setup, false}.toImage(DiagramScale, DiagramColours::forPrint()));

    QPdfWriter writer{path};
    writer.setPageSize(QPageSize{QPageSize::A4});
    // No margin of the writer's own: QTextDocument::print() puts 2 cm round an
    // unpaginated document, and the two together left images hanging off the
    // page. 2 cm leaves 170 mm, about 640 of the document's pixels.
    writer.setPageMargins(QMarginsF{0.0, 0.0, 0.0, 0.0}, QPageLayout::Millimeter);
    writer.setTitle(QStringLiteral("Test set-up: %1").arg(qs(setup.name)));
    writer.setCreator(QStringLiteral("PeakEmi"));
    document.print(&writer);

    if (QFileInfo{path}.size() <= 0) {
        return fail(ErrorCode::IoFailure, "cannot write the PDF to " + path.toStdString());
    }
    return {};
}

} // namespace peakemi::reporting
