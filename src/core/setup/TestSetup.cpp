#include <peakemi/core/TestSetup.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <string_view>

namespace peakemi {
namespace {

[[nodiscard]] std::string lowered(std::string_view text)
{
    std::string result{text};
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return result;
}

/// Words in a device's role that tell what kind of correction it carries.
[[nodiscard]] std::vector<std::string_view> roleWordsFor(CorrectionKind kind)
{
    switch (kind) {
        case CorrectionKind::AntennaFactor:
            return {"antenna"};
        case CorrectionKind::CableLoss:
            return {"cable"};
        case CorrectionKind::LisnFactor:
            return {"lisn", "network"};
        case CorrectionKind::Attenuator:
            return {"attenuator", "pad", "limiter"};
        case CorrectionKind::AmplifierGain:
            return {"amplifier", "preamp"};
        case CorrectionKind::Other:
            break;
    }
    return {};
}

[[nodiscard]] bool roleMatches(std::string_view role, CorrectionKind kind)
{
    const auto text = lowered(role);
    const auto words = roleWordsFor(kind);
    return std::any_of(words.begin(), words.end(), [&text](std::string_view word) {
        return text.find(word) != std::string::npos;
    });
}

} // namespace

std::vector<EquipmentItem> TestSetup::equipment() const
{
    std::vector<EquipmentItem> items;
    items.reserve(devices.size());
    for (const auto& device : devices) {
        items.push_back(device.equipment);
    }
    return items;
}

std::vector<CorrectionTable> TestSetup::corrections() const
{
    std::vector<CorrectionTable> tables;
    for (const auto& device : devices) {
        if (device.correction) {
            tables.push_back(*device.correction);
        }
    }
    return tables;
}

double TestSetup::totalCorrectionAt(Hertz frequency) const
{
    const auto tables = corrections();
    return peakemi::totalCorrectionAt(frequency, tables);
}

int TestSetup::expiredCount(TimePoint asOf) const
{
    return static_cast<int>(
        std::count_if(devices.begin(), devices.end(), [asOf](const auto& device) {
            return device.equipment.calibrationExpired(asOf);
        }));
}

Status TestSetup::validate() const
{
    if (name.find_first_not_of(" \t\r\n") == std::string::npos) {
        return fail(ErrorCode::InvalidConfiguration, "a set-up needs a name");
    }
    for (const auto& device : devices) {
        if (!device.correction) {
            continue;
        }
        if (auto status = device.correction->validate(); !status) {
            return fail(ErrorCode::InvalidConfiguration,
                        "the correction of '" +
                            (device.equipment.role.empty() ? device.correction->name
                                                           : device.equipment.role) +
                            "': " + status.error().detail);
        }
    }
    return {};
}

TestSetup TestSetup::createNew(std::string name, TestKind kind)
{
    TestSetup setup;
    setup.id = newIdentifier();
    setup.name = std::move(name);
    setup.kind = kind;
    setup.createdAt = std::chrono::system_clock::now();
    setup.modifiedAt = setup.createdAt;
    return setup;
}

CorrectionTable flatCorrection(std::string name, CorrectionKind kind, double valueDb)
{
    return CorrectionTable{.name = std::move(name),
                           .kind = kind,
                           .enabled = true,
                           .points = {{hertz(9000), valueDb}, {gigahertz(40.0), valueDb}}};
}

std::string roleFor(CorrectionKind kind)
{
    switch (kind) {
        case CorrectionKind::AntennaFactor:
            return "Antenna";
        case CorrectionKind::CableLoss:
            return "RF cable";
        case CorrectionKind::LisnFactor:
            return "LISN";
        case CorrectionKind::Attenuator:
            return "Attenuator";
        case CorrectionKind::AmplifierGain:
            return "Preamplifier";
        case CorrectionKind::Other:
            break;
    }
    return "Other";
}

TestSetup setupFrom(std::string name,
                    TestKind kind,
                    const std::vector<EquipmentItem>& equipment,
                    const std::vector<CorrectionTable>& corrections)
{
    auto setup = TestSetup::createNew(std::move(name), kind);
    for (const auto& item : equipment) {
        setup.devices.push_back(SetupDevice{.equipment = item, .correction = std::nullopt});
    }
    for (const auto& table : corrections) {
        const auto owner =
            std::find_if(setup.devices.begin(), setup.devices.end(), [&table](const auto& device) {
                return !device.correction && roleMatches(device.equipment.role, table.kind);
            });
        if (owner != setup.devices.end()) {
            owner->correction = table;
        } else {
            setup.devices.push_back(SetupDevice{
                .equipment = EquipmentItem{.role = roleFor(table.kind), .model = table.name},
                .correction = table});
        }
    }
    return setup;
}

void applySetup(CampaignTest& test, const TestSetup& setup)
{
    test.equipment = setup.equipment();
    test.config.corrections = setup.corrections();
    test.setupName = setup.name;
    test.setupDescription = setup.description;
}

} // namespace peakemi
