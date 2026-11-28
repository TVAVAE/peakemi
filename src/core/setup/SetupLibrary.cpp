#include "session/JsonConversions.h"

#include <peakemi/core/AtomicFileWriter.h>
#include <peakemi/core/Campaign.h>
#include <peakemi/core/Logging.h>
#include <peakemi/core/SetupLibrary.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

#include <algorithm>

namespace peakemi {
namespace {

using json_io::Json;

constexpr const char* SchemaName = "peakemi.setup";

[[nodiscard]] std::string text(const Json& json, const char* key)
{
    const auto found = json.find(key);
    return found != json.end() && found->is_string() ? found->get<std::string>() : std::string{};
}

[[nodiscard]] std::string timeText(TimePoint at)
{
    return at == TimePoint{} ? std::string{} : toIso8601(at);
}

[[nodiscard]] QString qs(std::string_view value)
{
    return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}

} // namespace

namespace json_io {

Json toJson(const TestSetup& setup)
{
    Json devices = Json::array();
    for (const auto& device : setup.devices) {
        Json entry{{"role", device.equipment.role},
                   {"model", device.equipment.model},
                   {"serial", device.equipment.serial},
                   {"calibration_due", device.equipment.calibrationDue},
                   {"notes", device.notes},
                   {"in_use", device.inUse}};
        entry["correction"] =
            device.correction ? json_io::toJson(*device.correction) : Json(nullptr);
        devices.push_back(std::move(entry));
    }
    return Json{{"id", setup.id},
                {"name", setup.name},
                {"kind", std::string{testKindKey(setup.kind)}},
                {"method", std::string{setupMethodKey(setup.method)}},
                {"description", setup.description},
                {"created_at", timeText(setup.createdAt)},
                {"modified_at", timeText(setup.modifiedAt)},
                {"devices", std::move(devices)}};
}

Result<TestSetup> testSetupFromJson(const Json& document)
{
    TestSetup setup;
    setup.id = text(document, "id");
    setup.name = text(document, "name");
    setup.kind = testKindFromKey(text(document, "kind")).value_or(TestKind::Other);
    // Written before set-ups had a method: the usual one for their kind.
    setup.method =
        setupMethodFromKey(text(document, "method")).value_or(defaultMethodFor(setup.kind));
    setup.description = text(document, "description");
    setup.createdAt = fromIso8601(text(document, "created_at")).value_or(TimePoint{});
    setup.modifiedAt = fromIso8601(text(document, "modified_at")).value_or(TimePoint{});
    if (setup.id.empty()) {
        setup.id = newIdentifier();
    }
    if (document.contains("devices") && document.at("devices").is_array()) {
        for (const auto& entry : document.at("devices")) {
            SetupDevice device;
            device.equipment = EquipmentItem{.role = text(entry, "role"),
                                             .model = text(entry, "model"),
                                             .serial = text(entry, "serial"),
                                             .calibrationDue = text(entry, "calibration_due")};
            device.notes = text(entry, "notes");
            const auto inUse = entry.find("in_use");
            device.inUse = inUse == entry.end() || !inUse->is_boolean() || inUse->get<bool>();
            if (entry.contains("correction") && entry.at("correction").is_object()) {
                auto table = json_io::correctionTableFromJson(entry.at("correction"));
                if (!table) {
                    return std::unexpected(table.error());
                }
                device.correction = std::move(*table);
            }
            setup.devices.push_back(std::move(device));
        }
    }
    if (auto status = setup.validate(); !status) {
        return fail(ErrorCode::ParseFailure, status.error().detail);
    }
    return setup;
}

} // namespace json_io

std::string TestSetupSerializer::toJson(const TestSetup& setup, bool pretty)
{
    Json document{{"schema", SchemaName}, {"schema_version", CurrentSchemaVersion}};
    document.update(json_io::toJson(setup));
    return pretty ? document.dump(2) : document.dump();
}

Result<TestSetup> TestSetupSerializer::fromJson(std::string_view content)
{
    Json document = Json::parse(content, nullptr, false);
    if (document.is_discarded() || !document.is_object()) {
        return fail(ErrorCode::ParseFailure, "set-up file is not valid JSON");
    }
    if (document.value("schema", std::string{}) != SchemaName) {
        return fail(ErrorCode::ParseFailure, "not a PeakEmi set-up file");
    }
    const int version = document.value("schema_version", 0);
    if (version < MinimumSupportedSchemaVersion || version > CurrentSchemaVersion) {
        return fail(ErrorCode::SchemaVersionUnsupported,
                    "set-up schema version " + std::to_string(version) +
                        (version > CurrentSchemaVersion ? " was written by a newer PeakEmi"
                                                        : " is too old"));
    }
    return json_io::testSetupFromJson(document);
}

Status TestSetupSerializer::save(const TestSetup& setup, const QString& path)
{
    if (auto status = setup.validate(); !status) {
        return status;
    }
    return writeFileAtomically(path, toJson(setup));
}

Result<TestSetup> TestSetupSerializer::load(const QString& path)
{
    auto content = readFile(path);
    if (!content) {
        return std::unexpected(content.error());
    }
    return fromJson(*content);
}

SetupLibrary::SetupLibrary(QString directory) : m_directory{std::move(directory)} {}

QString SetupLibrary::defaultDirectory()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
           QStringLiteral("/setups");
}

SetupLibrary::Contents SetupLibrary::load() const
{
    Contents contents;
    const QDir directory{m_directory};
    if (!directory.exists()) {
        return contents;
    }
    const auto files = directory.entryInfoList(
        {QStringLiteral("*") + qs(FileSuffix)}, QDir::Files | QDir::Readable, QDir::Name);
    for (const auto& file : files) {
        auto setup = TestSetupSerializer::load(file.absoluteFilePath());
        if (!setup) {
            contents.problems.push_back(file.fileName().toStdString() + ": " +
                                        setup.error().message());
            continue;
        }
        contents.setups.push_back(std::move(*setup));
    }
    std::sort(contents.setups.begin(), contents.setups.end(), [](const auto& a, const auto& b) {
        return QString::localeAwareCompare(qs(a.name), qs(b.name)) < 0;
    });
    return contents;
}

Status SetupLibrary::save(const TestSetup& setup) const
{
    if (!QDir{}.mkpath(m_directory)) {
        return fail(ErrorCode::IoFailure, "cannot create " + m_directory.toStdString());
    }
    const auto status = TestSetupSerializer::save(setup, pathFor(setup.id));
    if (status) {
        qCInfo(lcSession) << "set-up" << qs(setup.name) << "saved to" << pathFor(setup.id);
    }
    return status;
}

Status SetupLibrary::remove(std::string_view id) const
{
    const auto path = pathFor(id);
    if (QFileInfo::exists(path) && !QFile::remove(path)) {
        return fail(ErrorCode::IoFailure, "cannot remove " + path.toStdString());
    }
    return {};
}

QString SetupLibrary::pathFor(std::string_view id) const
{
    return QDir{m_directory}.filePath(qs(id) + qs(FileSuffix));
}

} // namespace peakemi
