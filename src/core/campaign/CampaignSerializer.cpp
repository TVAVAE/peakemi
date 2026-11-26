#include "session/JsonConversions.h"

#include <peakemi/core/AtomicFileWriter.h>
#include <peakemi/core/CampaignSerializer.h>
#include <peakemi/core/Logging.h>
#include <peakemi/core/SessionSerializer.h>

namespace peakemi {
namespace {

using json_io::Json;

constexpr const char* SchemaName = "peakemi.campaign";

[[nodiscard]] std::string text(const Json& json, const char* key)
{
    const auto found = json.find(key);
    return found != json.end() && found->is_string() ? found->get<std::string>() : std::string{};
}

/// Empty for a moment never set, such as an unsigned sign-off.
[[nodiscard]] std::string timeText(TimePoint at)
{
    return at == TimePoint{} ? std::string{} : toIso8601(at);
}

[[nodiscard]] TimePoint timeFromJson(const Json& json, const char* key)
{
    return fromIso8601(text(json, key)).value_or(TimePoint{});
}

[[nodiscard]] Json toJson(const Signoff& value)
{
    return Json{{"name", value.name}, {"at", timeText(value.at)}};
}

[[nodiscard]] Signoff signoffFromJson(const Json& json)
{
    if (!json.is_object()) {
        return {};
    }
    return Signoff{.name = text(json, "name"), .at = timeFromJson(json, "at")};
}

[[nodiscard]] Json toJson(const EquipmentItem& value)
{
    return Json{{"role", value.role},
                {"model", value.model},
                {"serial", value.serial},
                {"calibration_due", value.calibrationDue}};
}

[[nodiscard]] EquipmentItem equipmentFromJson(const Json& json)
{
    return EquipmentItem{.role = text(json, "role"),
                         .model = text(json, "model"),
                         .serial = text(json, "serial"),
                         .calibrationDue = text(json, "calibration_due")};
}

[[nodiscard]] Json toJson(const SetupPhoto& value)
{
    return Json{
        {"caption", value.caption}, {"mime_type", value.mimeType}, {"data", value.base64Data}};
}

[[nodiscard]] SetupPhoto photoFromJson(const Json& json)
{
    SetupPhoto photo;
    photo.caption = text(json, "caption");
    if (auto mime = text(json, "mime_type"); !mime.empty()) {
        photo.mimeType = std::move(mime);
    }
    photo.base64Data = text(json, "data");
    return photo;
}

[[nodiscard]] Json toJson(const CampaignTest& test)
{
    Json equipment = Json::array();
    for (const auto& item : test.equipment) {
        equipment.push_back(toJson(item));
    }
    Json photos = Json::array();
    for (const auto& photo : test.photos) {
        photos.push_back(toJson(photo));
    }

    Json json{{"id", test.id},
              {"name", test.name},
              {"kind", std::string{testKindKey(test.kind)}},
              {"port", test.port},
              {"procedure", test.procedure},
              {"config", json_io::toJson(test.config)},
              {"comments", test.comments},
              {"equipment", std::move(equipment)},
              {"photos", std::move(photos)},
              {"tested_by", toJson(test.testedBy)}};
    // The session in the session format, so it can be taken out as one.
    json["session"] =
        test.session ? Json::parse(SessionSerializer::toJson(*test.session, false)) : Json(nullptr);
    json["ambient"] = test.ambient ? json_io::toJson(*test.ambient) : Json(nullptr);
    json["override"] = test.overrideVerdict
                           ? Json{{"verdict", std::string{testVerdictKey(*test.overrideVerdict)}},
                                  {"reason", test.overrideReason}}
                           : Json(nullptr);
    return json;
}

[[nodiscard]] Result<CampaignTest> testFromJson(const Json& json)
{
    if (!json.is_object()) {
        return fail(ErrorCode::ParseFailure, "a campaign test is not a JSON object");
    }
    CampaignTest test;
    test.id = text(json, "id");
    test.name = text(json, "name");
    test.kind = testKindFromKey(text(json, "kind")).value_or(TestKind::Other);
    test.port = text(json, "port");
    test.procedure = text(json, "procedure");
    test.comments = text(json, "comments");
    if (test.id.empty()) {
        test.id = newIdentifier();
    }

    if (json.contains("config")) {
        auto config = json_io::runConfigurationFromJson(json.at("config"));
        if (!config) {
            return std::unexpected(config.error());
        }
        test.config = std::move(*config);
    }
    if (json.contains("session") && json.at("session").is_object()) {
        auto session = SessionSerializer::fromJson(json.at("session").dump());
        if (!session) {
            return std::unexpected(session.error());
        }
        test.session = std::move(*session);
    }
    if (json.contains("ambient") && json.at("ambient").is_object()) {
        auto ambient = json_io::traceFromJson(json.at("ambient"));
        if (!ambient) {
            return std::unexpected(ambient.error());
        }
        test.ambient = std::move(*ambient);
    }
    if (json.contains("override") && json.at("override").is_object()) {
        const auto& overridden = json.at("override");
        const auto verdict = testVerdictFromKey(text(overridden, "verdict"));
        if (!verdict) {
            return fail(ErrorCode::ParseFailure,
                        "'" + test.name + "' has an override verdict this version does not know");
        }
        // Through overrideWith(), so a file cannot carry an override without
        // the reason the report owes its reader.
        if (auto status = test.overrideWith(*verdict, text(overridden, "reason")); !status) {
            return fail(ErrorCode::ParseFailure, "'" + test.name + "': " + status.error().detail);
        }
    }
    if (json.contains("equipment") && json.at("equipment").is_array()) {
        for (const auto& item : json.at("equipment")) {
            test.equipment.push_back(equipmentFromJson(item));
        }
    }
    if (json.contains("photos") && json.at("photos").is_array()) {
        for (const auto& photo : json.at("photos")) {
            test.photos.push_back(photoFromJson(photo));
        }
    }
    if (json.contains("tested_by")) {
        test.testedBy = signoffFromJson(json.at("tested_by"));
    }
    return test;
}

} // namespace

std::string CampaignSerializer::toJson(const Campaign& campaign, bool pretty)
{
    Json tests = Json::array();
    for (const auto& test : campaign.tests) {
        tests.push_back(peakemi::toJson(test));
    }

    const Json document{{"schema", SchemaName},
                        {"schema_version", CurrentSchemaVersion},
                        {"id", campaign.id},
                        {"title", campaign.title},
                        {"standard", campaign.standard},
                        {"template", campaign.templateId},
                        {"eut",
                         Json{{"name", campaign.eutName},
                              {"serial", campaign.eutSerial},
                              {"operating_mode", campaign.eutOperatingMode},
                              {"description", campaign.eutDescription}}},
                        {"customer", campaign.customer},
                        {"location", campaign.location},
                        {"notes", campaign.notes},
                        {"tested_by", peakemi::toJson(campaign.testedBy)},
                        {"reviewed_by", peakemi::toJson(campaign.reviewedBy)},
                        {"locked", campaign.locked},
                        {"application_version", campaign.applicationVersion},
                        {"created_at", timeText(campaign.createdAt)},
                        {"modified_at", timeText(campaign.modifiedAt)},
                        {"tests", std::move(tests)}};
    return pretty ? document.dump(2) : document.dump();
}

Result<Campaign> CampaignSerializer::fromJson(std::string_view content)
{
    Json document = Json::parse(content, nullptr, false);
    if (document.is_discarded() || !document.is_object()) {
        return fail(ErrorCode::ParseFailure, "campaign file is not valid JSON");
    }
    if (document.value("schema", std::string{}) != SchemaName) {
        return fail(ErrorCode::ParseFailure, "not a PeakEmi campaign file");
    }
    const int version = document.value("schema_version", 0);
    if (version < MinimumSupportedSchemaVersion) {
        return fail(ErrorCode::SchemaVersionUnsupported,
                    "campaign schema version " + std::to_string(version) + " is too old");
    }
    if (version > CurrentSchemaVersion) {
        return fail(ErrorCode::SchemaVersionUnsupported,
                    "campaign schema version " + std::to_string(version) +
                        " was written by a newer PeakEmi");
    }

    Campaign campaign;
    campaign.id = text(document, "id");
    campaign.title = text(document, "title");
    campaign.standard = text(document, "standard");
    campaign.templateId = text(document, "template");
    if (document.contains("eut") && document.at("eut").is_object()) {
        const auto& eut = document.at("eut");
        campaign.eutName = text(eut, "name");
        campaign.eutSerial = text(eut, "serial");
        campaign.eutOperatingMode = text(eut, "operating_mode");
        campaign.eutDescription = text(eut, "description");
    }
    campaign.customer = text(document, "customer");
    campaign.location = text(document, "location");
    campaign.notes = text(document, "notes");
    if (document.contains("tested_by")) {
        campaign.testedBy = signoffFromJson(document.at("tested_by"));
    }
    if (document.contains("reviewed_by")) {
        campaign.reviewedBy = signoffFromJson(document.at("reviewed_by"));
    }
    campaign.locked = document.value("locked", false);
    campaign.applicationVersion = text(document, "application_version");
    campaign.createdAt = timeFromJson(document, "created_at");
    campaign.modifiedAt = timeFromJson(document, "modified_at");
    if (campaign.id.empty()) {
        campaign.id = newIdentifier();
    }

    if (document.contains("tests") && document.at("tests").is_array()) {
        for (const auto& entry : document.at("tests")) {
            auto test = testFromJson(entry);
            if (!test) {
                return std::unexpected(test.error());
            }
            campaign.tests.push_back(std::move(*test));
        }
    }
    return campaign;
}

Status CampaignSerializer::save(const Campaign& campaign, const QString& path)
{
    const auto status = writeFileAtomically(path, toJson(campaign));
    if (status) {
        qCInfo(lcSession) << "campaign saved to" << path << "-" << campaign.tests.size() << "tests";
    }
    return status;
}

Result<Campaign> CampaignSerializer::load(const QString& path)
{
    auto content = readFile(path);
    if (!content) {
        return std::unexpected(content.error());
    }
    return fromJson(*content);
}

} // namespace peakemi
