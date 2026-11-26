#pragma once

#include <peakemi/core/Campaign.h>
#include <peakemi/core/Error.h>

#include <QString>

#include <string>
#include <string_view>

namespace peakemi {

/// Versioned JSON container for test campaigns, ".peakemi-campaign.json".
///
/// Each test's session is embedded in the session format SessionSerializer
/// writes, so a measurement taken out of a campaign is an ordinary session, and
/// the same compatibility rule applies: older schema versions are read, newer
/// ones are refused rather than misread.
class CampaignSerializer
{
public:
    static constexpr int CurrentSchemaVersion = 1;
    static constexpr int MinimumSupportedSchemaVersion = 1;

    [[nodiscard]] static std::string toJson(const Campaign& campaign, bool pretty = true);
    [[nodiscard]] static Result<Campaign> fromJson(std::string_view text);

    [[nodiscard]] static Status save(const Campaign& campaign, const QString& path);
    [[nodiscard]] static Result<Campaign> load(const QString& path);
};

} // namespace peakemi
