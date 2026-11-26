#include <peakemi/core/Disclaimer.h>
#include <peakemi/core/Logging.h>
#include <peakemi/core/Time.h>
#include <peakemi/reporting/CampaignReportRenderer.h>

#include <QByteArray>
#include <QFileInfo>
#include <QFont>
#include <QMarginsF>
#include <QPageLayout>
#include <QPageSize>
#include <QPdfWriter>
#include <QStringList>
#include <QTextDocument>
#include <QUrl>

#include <algorithm>
#include <cmath>

namespace peakemi::reporting {
namespace {

/// Spectrum images are drawn at this size and scaled to the text width.
constexpr QSize SpectrumSize{1800, 760};
/// Text width of an A4 page with 15 mm margins, in the document's units.
constexpr int PageWidth = 660;
constexpr int PhotoWidth = 320;

[[nodiscard]] QString qs(std::string_view text)
{
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

/// Escaped for rich text, with line breaks kept.
[[nodiscard]] QString escaped(std::string_view text)
{
    return qs(text).toHtmlEscaped().replace(QLatin1Char('\n'), QStringLiteral("<br>"));
}

[[nodiscard]] QString orDash(std::string_view text)
{
    return text.empty() ? QStringLiteral("&ndash;") : escaped(text);
}

[[nodiscard]] QString number(double value, int precision = 2)
{
    return std::isfinite(value) ? QString::number(value, 'f', precision)
                                : QStringLiteral("&ndash;");
}

[[nodiscard]] QString megahertz(Hertz frequency, int precision = 4)
{
    return QString::number(static_cast<double>(frequency.value()) / 1e6, 'f', precision);
}

[[nodiscard]] QString date(TimePoint at)
{
    if (at == TimePoint{}) {
        return QStringLiteral("&ndash;");
    }
    // "2026-11-26T09:13:44.120Z" -> "2026-11-26 09:13 UTC"
    const auto iso = qs(toIso8601(at));
    return iso.left(10) + QLatin1Char(' ') + iso.mid(11, 5) + QStringLiteral(" UTC");
}

struct VerdictStyle
{
    QString label;
    QString background;
    QString foreground;
};

[[nodiscard]] VerdictStyle styleOf(TestVerdict verdict)
{
    switch (verdict) {
        case TestVerdict::Pass:
            return {QStringLiteral("PASS"), QStringLiteral("#E6F4EA"), QStringLiteral("#137333")};
        case TestVerdict::Marginal:
            return {
                QStringLiteral("MARGINAL"), QStringLiteral("#FEF7E0"), QStringLiteral("#B06000")};
        case TestVerdict::Fail:
            return {QStringLiteral("FAIL"), QStringLiteral("#FCE8E6"), QStringLiteral("#C5221F")};
        case TestVerdict::NotApplicable:
            return {QStringLiteral("N/A"), QStringLiteral("#F1F3F4"), QStringLiteral("#5F6368")};
        case TestVerdict::NotTested:
            break;
    }
    return {QStringLiteral("NOT TESTED"), QStringLiteral("#F1F3F4"), QStringLiteral("#5F6368")};
}

/// A table cell holding a verdict badge.
[[nodiscard]] QString verdictCell(TestVerdict verdict)
{
    const auto style = styleOf(verdict);
    return QStringLiteral(R"(<td style="background-color:%1; color:%2; font-weight:bold;">%3</td>)")
        .arg(style.background, style.foreground, style.label);
}

[[nodiscard]] QString keyValue(const QString& key, const QString& value)
{
    return QStringLiteral(R"(<tr><td style="color:#5F6368;" width="32%">%1</td><td>%2</td></tr>)")
        .arg(key, value);
}

[[nodiscard]] QString spectrumUrl(std::size_t test)
{
    return QStringLiteral("peakemi://spectrum/%1").arg(test);
}

[[nodiscard]] QString photoUrl(std::size_t test, std::size_t photo)
{
    return QStringLiteral("peakemi://photo/%1/%2").arg(test).arg(photo);
}

[[nodiscard]] QString limitNames(const RunConfiguration& config)
{
    QStringList names;
    for (const auto& limit : config.limits) {
        names << escaped(limit.name);
    }
    return names.isEmpty() ? QStringLiteral("&ndash;") : names.join(QStringLiteral("<br>"));
}

[[nodiscard]] QString detectorName(Detector detector)
{
    return qs(detectorKey(detector));
}

[[nodiscard]] QString
testSection(const CampaignTest& test, std::size_t index, bool hasSpectrum, TimePoint asOf)
{
    QString html;
    html += QStringLiteral(R"(<h2 style="page-break-before: always;">%1. %2</h2>)")
                .arg(index + 1)
                .arg(escaped(test.name));

    // Settings and verdict.
    QString overrideNote;
    if (test.isOverridden()) {
        overrideNote =
            QStringLiteral(R"(<br><span style="color:#5F6368;">Overridden from %1: %2</span>)")
                .arg(styleOf(test.measuredVerdict()).label, escaped(test.overrideReason));
    }
    const auto& config = test.config;
    html += QStringLiteral(R"(<table cellpadding="3" width="100%">)");
    html += keyValue(QStringLiteral("Port"), orDash(test.port));
    html += keyValue(QStringLiteral("Test type"), qs(testKindKey(test.kind)));
    html += keyValue(QStringLiteral("Frequency range"),
                     QStringLiteral("%1 MHz to %2 MHz")
                         .arg(megahertz(config.span.start), megahertz(config.span.stop)));
    html += keyValue(QStringLiteral("Limit lines"), limitNames(config));
    html += keyValue(QStringLiteral("Phase 1 detector"), detectorName(config.phase1Detector));
    html += keyValue(
        QStringLiteral("Phase 2 detector and dwell"),
        QStringLiteral("%1, %2 ms, %3")
            .arg(detectorName(config.verificationDetector))
            .arg(config.dwellTime.count())
            .arg(config.verificationSpan == Hertz{0}
                     ? QStringLiteral("zero span")
                     : QStringLiteral("%1 kHz span")
                           .arg(static_cast<double>(config.verificationSpan.value()) / 1e3)));
    html += keyValue(QStringLiteral("Measured verdict"),
                     QStringLiteral("<b>%1</b>").arg(styleOf(test.measuredVerdict()).label));
    const auto finalStyle = styleOf(test.finalVerdict());
    html += keyValue(QStringLiteral("Final verdict"),
                     QStringLiteral(R"(<b style="color:%1;">%2</b>%3)")
                         .arg(finalStyle.foreground, finalStyle.label, overrideNote));
    html += keyValue(
        QStringLiteral("Tested by"),
        test.testedBy.isSigned()
            ? QStringLiteral("%1, %2").arg(escaped(test.testedBy.name), date(test.testedBy.at))
            : QStringLiteral("&ndash;"));
    if (test.session && !test.session->traces.empty()) {
        const auto& instrument = test.session->traces.back().source;
        if (!instrument.isEmpty()) {
            html += keyValue(QStringLiteral("Instrument"),
                             qs(instrument.displayName()).toHtmlEscaped());
        }
    }
    html += QStringLiteral("</table>");

    if (!test.procedure.empty()) {
        html += QStringLiteral("<h3>Method</h3><p>%1</p>").arg(escaped(test.procedure));
    }

    html += QStringLiteral("<h3>Spectrum</h3>");
    if (hasSpectrum) {
        html += QStringLiteral(R"(<p><img src="%1" width="%2"></p>)")
                    .arg(spectrumUrl(index))
                    .arg(PageWidth);
        if (test.ambient) {
            html += QStringLiteral(
                R"(<p style="color:#5F6368;">The grey trace is the ambient scan, made with the equipment under test switched off.</p>)");
        }
    } else {
        html += QStringLiteral("<p>Not measured.</p>");
    }

    html += QStringLiteral("<h3>Verified points</h3>");
    if (test.session && !test.session->results.empty()) {
        html += QStringLiteral(
            R"(<table border="1" cellspacing="0" cellpadding="3" width="100%"><tr style="background-color:#F1F3F4;"><th>Frequency [MHz]</th><th>Level</th><th>Limit</th><th>Margin [dB]</th><th>Detector</th><th>Limit line</th><th>Verdict</th></tr>)");
        for (const auto& point : test.session->results) {
            const auto unit = qs(amplitudeUnitKey(point.unit));
            TestVerdict verdict = TestVerdict::NotTested;
            switch (point.verdict) {
                case Verdict::Pass:
                    verdict = TestVerdict::Pass;
                    break;
                case Verdict::Marginal:
                    verdict = TestVerdict::Marginal;
                    break;
                case Verdict::Fail:
                    verdict = TestVerdict::Fail;
                    break;
                case Verdict::Unknown:
                    break;
            }
            html += QStringLiteral("<tr><td>%1</td><td>%2 "
                                   "%3</td><td>%4</td><td>%5</td><td>%6</td><td>%7</td>%8</tr>")
                        .arg(megahertz(point.frequency))
                        .arg(number(point.correctedAmplitude),
                             unit,
                             number(point.limitValue),
                             number(point.marginDb),
                             detectorName(point.detector),
                             escaped(point.limitName),
                             verdictCell(verdict));
        }
        html += QStringLiteral("</table>");
    } else if (test.session) {
        html +=
            QStringLiteral("<p>No emission came close enough to the limit to need verifying.</p>");
    } else {
        html += QStringLiteral("<p>&ndash;</p>");
    }

    html += QStringLiteral("<h3>Equipment</h3>");
    if (test.equipment.empty()) {
        html += QStringLiteral("<p>&ndash;</p>");
    } else {
        html += QStringLiteral(
            R"(<table border="1" cellspacing="0" cellpadding="3" width="100%"><tr style="background-color:#F1F3F4;"><th>Role</th><th>Model</th><th>Serial number</th><th>Calibration due</th></tr>)");
        for (const auto& item : test.equipment) {
            QString due = orDash(item.calibrationDue);
            if (item.calibrationExpired(asOf)) {
                due =
                    QStringLiteral(
                        R"(<span style="color:#C5221F; font-weight:bold;">%1 &ndash; EXPIRED</span>)")
                        .arg(escaped(item.calibrationDue));
            }
            html += QStringLiteral("<tr><td>%1</td><td>%2</td><td>%3</td><td>%4</td></tr>")
                        .arg(orDash(item.role), orDash(item.model), orDash(item.serial), due);
        }
        html += QStringLiteral("</table>");
    }

    html += QStringLiteral("<h3>Comments</h3><p>%1</p>").arg(orDash(test.comments));

    if (!test.photos.empty()) {
        html += QStringLiteral("<h3>Setup photos</h3>");
        for (std::size_t photo = 0; photo < test.photos.size(); ++photo) {
            html +=
                QStringLiteral(
                    R"(<p><img src="%1" width="%2"><br><span style="color:#5F6368;">%3</span></p>)")
                    .arg(photoUrl(index, photo))
                    .arg(PhotoWidth)
                    .arg(escaped(test.photos[photo].caption));
        }
    }
    return html;
}

} // namespace

TimePoint CampaignReportRenderer::generatedAt() const
{
    return m_generatedAt == TimePoint{} ? std::chrono::system_clock::now() : m_generatedAt;
}

QString CampaignReportRenderer::html(const Campaign& campaign,
                                     const ReportTemplate& reportTemplate) const
{
    const TimePoint asOf = generatedAt();
    const auto disclaimer = escaped(ComplianceDisclaimer);
    const auto overall = styleOf(campaign.overallVerdict());

    QString html =
        QStringLiteral(R"(<html><body style="font-size:9pt;"><h1 style="font-size:18pt;">%1</h1>)")
            .arg(reportTemplate.title.empty() ? QStringLiteral("EMC pre-compliance test report")
                                              : escaped(reportTemplate.title));
    if (!reportTemplate.companyName.empty() || !reportTemplate.address.empty()) {
        html += QStringLiteral("<p>%1<br>%2</p>")
                    .arg(escaped(reportTemplate.companyName), escaped(reportTemplate.address));
    }
    html += QStringLiteral(R"(<p style="font-size:12pt;"><b>%1</b><br>%2</p>)")
                .arg(orDash(campaign.title), orDash(campaign.standard));

    html += QStringLiteral(R"(<table cellpadding="3" width="100%">)");
    html += keyValue(QStringLiteral("Equipment under test"), orDash(campaign.eutName));
    html += keyValue(QStringLiteral("Serial number"), orDash(campaign.eutSerial));
    html += keyValue(QStringLiteral("Operating mode"), orDash(campaign.eutOperatingMode));
    html += keyValue(QStringLiteral("Description"), orDash(campaign.eutDescription));
    html += keyValue(QStringLiteral("Customer"), orDash(campaign.customer));
    html += keyValue(QStringLiteral("Test location"), orDash(campaign.location));
    html += keyValue(QStringLiteral("Campaign started"), date(campaign.createdAt));
    html += keyValue(QStringLiteral("Report generated"), date(asOf));
    html += keyValue(QStringLiteral("Application"),
                     QStringLiteral("PeakEmi %1").arg(escaped(campaign.applicationVersion)));
    html += keyValue(QStringLiteral("Campaign identifier"), escaped(campaign.id));
    html += QStringLiteral("</table>");

    html +=
        QStringLiteral(
            R"(<p style="font-size:13pt;">Overall result: <span style="background-color:%1; color:%2; font-weight:bold;">&nbsp;%3&nbsp;</span> <span style="font-size:9pt; color:#5F6368;">(%4 of %5 tests completed)</span></p>)")
            .arg(overall.background, overall.foreground, overall.label)
            .arg(campaign.completedCount())
            .arg(campaign.tests.size());

    if (!reportTemplate.introduction.empty()) {
        html += QStringLiteral("<p>%1</p>").arg(escaped(reportTemplate.introduction));
    }

    // Summary.
    html += QStringLiteral(
        R"(<h2>Summary</h2><table border="1" cellspacing="0" cellpadding="4" width="100%"><tr style="background-color:#F1F3F4;"><th>#</th><th>Test</th><th>Port</th><th>Measured</th><th>Final</th><th>Worst margin [dB]</th><th>Comments</th></tr>)");
    for (std::size_t i = 0; i < campaign.tests.size(); ++i) {
        const auto& test = campaign.tests[i];
        const auto worst = test.worstResult();
        QString comment = escaped(test.comments);
        if (test.isOverridden()) {
            comment = QStringLiteral(R"(<span style="color:#5F6368;">Override: %1</span>%2%3)")
                          .arg(escaped(test.overrideReason),
                               test.comments.empty() ? QString{} : QStringLiteral("<br>"),
                               comment);
        }
        html +=
            QStringLiteral("<tr><td>%1</td><td>%2</td><td>%3</td>%4%5<td>%6</td><td>%7</td></tr>")
                .arg(i + 1)
                .arg(escaped(test.name),
                     orDash(test.port),
                     verdictCell(test.measuredVerdict()),
                     verdictCell(test.finalVerdict()),
                     worst ? number(worst->marginDb) : QStringLiteral("&ndash;"),
                     comment.isEmpty() ? QStringLiteral("&ndash;") : comment);
    }
    html += QStringLiteral("</table>");

    // Sign-off.
    const auto signoff = [](const Signoff& value) {
        return value.isSigned() ? QStringLiteral("<td>%1</td><td>%2</td>")
                                      .arg(escaped(value.name), date(value.at))
                                : QStringLiteral("<td>&ndash;</td><td>&ndash;</td>");
    };
    html += QStringLiteral(
        R"(<h2>Sign-off</h2><table border="1" cellspacing="0" cellpadding="4" width="100%"><tr style="background-color:#F1F3F4;"><th></th><th>Name</th><th>Date</th></tr>)");
    html += QStringLiteral("<tr><td><b>Tested by</b></td>%1</tr>").arg(signoff(campaign.testedBy));
    html +=
        QStringLiteral("<tr><td><b>Reviewed by</b></td>%1</tr>").arg(signoff(campaign.reviewedBy));
    html += QStringLiteral("<tr><td><b>Status</b></td><td colspan=\"2\">%1</td></tr>")
                .arg(campaign.locked ? QStringLiteral("Closed and locked after review")
                                     : QStringLiteral("Open"));
    html += QStringLiteral("</table>");

    if (!campaign.notes.empty()) {
        html += QStringLiteral("<h2>Notes</h2><p>%1</p>").arg(escaped(campaign.notes));
    }
    html += QStringLiteral(R"(<h2>Compliance notice</h2><p>%1</p>)").arg(disclaimer);

    for (std::size_t i = 0; i < campaign.tests.size(); ++i) {
        const auto& test = campaign.tests[i];
        const bool hasSpectrum =
            static_cast<bool>(m_spectrum) &&
            ((test.session && !test.session->traces.empty()) || test.ambient.has_value());
        html += testSection(test, i, hasSpectrum, asOf);
    }

    if (!reportTemplate.conclusion.empty()) {
        html += QStringLiteral(R"(<h2 style="page-break-before: always;">Conclusion</h2><p>%1</p>)")
                    .arg(escaped(reportTemplate.conclusion));
    }
    html += QStringLiteral(R"(<p style="color:#5F6368; font-size:8pt;">%1</p></body></html>)")
                .arg(disclaimer);
    return html;
}

Status CampaignReportRenderer::render(const Campaign& campaign,
                                      const ReportTemplate& reportTemplate,
                                      const QString& path) const
{
    QTextDocument document;
    QFont font = document.defaultFont();
    font.setPointSize(9);
    document.setDefaultFont(font);
    document.setHtml(html(campaign, reportTemplate));

    // The images the rich text refers to, added once it is set: they are
    // loaded when the document is laid out for the page.
    for (std::size_t i = 0; i < campaign.tests.size(); ++i) {
        const auto& test = campaign.tests[i];
        if (m_spectrum && ((test.session && !test.session->traces.empty()) || test.ambient)) {
            const QImage spectrum = m_spectrum(test, SpectrumSize);
            if (!spectrum.isNull()) {
                document.addResource(QTextDocument::ImageResource, QUrl{spectrumUrl(i)}, spectrum);
            }
        }
        for (std::size_t photo = 0; photo < test.photos.size(); ++photo) {
            const auto bytes =
                QByteArray::fromBase64(QByteArray::fromStdString(test.photos[photo].base64Data));
            QImage image = QImage::fromData(bytes);
            if (!image.isNull()) {
                image =
                    image.scaledToWidth(std::min(image.width(), 1200), Qt::SmoothTransformation);
                document.addResource(QTextDocument::ImageResource, QUrl{photoUrl(i, photo)}, image);
            }
        }
    }

    QPdfWriter writer{path};
    writer.setPageSize(QPageSize{QPageSize::A4});
    writer.setPageMargins(QMarginsF{15.0, 15.0, 15.0, 15.0}, QPageLayout::Millimeter);
    writer.setTitle(reportTemplate.title.empty() ? QStringLiteral("EMC pre-compliance test report")
                                                 : qs(reportTemplate.title));
    writer.setCreator(QStringLiteral("PeakEmi %1").arg(qs(campaign.applicationVersion)));
    document.print(&writer);

    if (QFileInfo{path}.size() <= 0) {
        return fail(ErrorCode::IoFailure, "cannot write the PDF to " + path.toStdString());
    }
    qCInfo(lcCore) << "campaign report written to" << path << "-" << campaign.tests.size()
                   << "tests";
    return {};
}

} // namespace peakemi::reporting
