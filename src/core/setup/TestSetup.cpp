#include <peakemi/core/Campaign.h>
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

[[nodiscard]] SetupSlot slot(std::string role,
                             std::string caption,
                             SlotPlace place,
                             bool optional = false,
                             bool onByDefault = true)
{
    return SetupSlot{.role = std::move(role),
                     .caption = std::move(caption),
                     .place = place,
                     .optional = optional,
                     .onByDefault = onByDefault};
}

[[nodiscard]] SetupSlot optionalSlot(std::string role, SlotPlace place, bool onByDefault)
{
    auto caption = role;
    return slot(std::move(role), std::move(caption), place, true, onByDefault);
}

} // namespace

std::string_view setupMethodKey(SetupMethod method)
{
    switch (method) {
        case SetupMethod::Custom:
            return "custom";
        case SetupMethod::LisnAc:
            return "lisn-ac";
        case SetupMethod::LisnDc:
            return "lisn-dc";
        case SetupMethod::CurrentProbe:
            return "current-probe";
        case SetupMethod::Antenna:
            return "antenna";
        case SetupMethod::TemCell:
            return "tem-cell";
        case SetupMethod::NearFieldProbe:
            return "near-field-probe";
    }
    return "custom";
}

std::optional<SetupMethod> setupMethodFromKey(std::string_view key)
{
    for (const auto method : setupMethods()) {
        if (setupMethodKey(method) == key) {
            return method;
        }
    }
    return std::nullopt;
}

std::string_view setupMethodName(SetupMethod method)
{
    switch (method) {
        case SetupMethod::Custom:
            return "Custom chain";
        case SetupMethod::LisnAc:
            return "Conducted – AC mains LISN";
        case SetupMethod::LisnDc:
            return "Conducted – DC LISNs";
        case SetupMethod::CurrentProbe:
            return "Conducted – RF current probe";
        case SetupMethod::Antenna:
            return "Radiated – antenna";
        case SetupMethod::TemCell:
            return "Radiated – TEM cell";
        case SetupMethod::NearFieldProbe:
            return "Near-field probe";
    }
    return "Custom chain";
}

std::vector<SetupMethod> setupMethods()
{
    return {SetupMethod::LisnAc,
            SetupMethod::LisnDc,
            SetupMethod::CurrentProbe,
            SetupMethod::Antenna,
            SetupMethod::TemCell,
            SetupMethod::NearFieldProbe,
            SetupMethod::Custom};
}

TestKind kindOf(SetupMethod method)
{
    switch (method) {
        case SetupMethod::LisnAc:
        case SetupMethod::LisnDc:
        case SetupMethod::CurrentProbe:
            return TestKind::Conducted;
        case SetupMethod::Antenna:
        case SetupMethod::TemCell:
            return TestKind::Radiated;
        case SetupMethod::NearFieldProbe:
        case SetupMethod::Custom:
            break;
    }
    return TestKind::Other;
}

SetupMethod defaultMethodFor(TestKind kind)
{
    switch (kind) {
        case TestKind::Conducted:
            return SetupMethod::LisnAc;
        case TestKind::Radiated:
            return SetupMethod::Antenna;
        case TestKind::Other:
            break;
    }
    return SetupMethod::Custom;
}

std::vector<SetupSlot> slotsFor(SetupMethod method)
{
    const auto analyzer = slot("Spectrum analyzer", "Spectrum analyzer", SlotPlace::Analyzer);
    const auto cable = optionalSlot("RF cable", SlotPlace::Chain, true);
    const auto limiter = optionalSlot("Transient limiter", SlotPlace::Chain, false);
    const auto attenuator = optionalSlot("Attenuator", SlotPlace::Chain, false);
    const auto preamplifier = [](bool on) {
        return optionalSlot("Preamplifier", SlotPlace::Chain, on);
    };
    switch (method) {
        case SetupMethod::LisnAc:
            return {slot("LISN", "50 µH LISN", SlotPlace::Coupling),
                    optionalSlot("Isolation transformer", SlotPlace::Supply, false),
                    limiter,
                    attenuator,
                    cable,
                    analyzer};
        case SetupMethod::LisnDc:
            return {slot("LISN", "5 µH LISN, + line", SlotPlace::Coupling),
                    slot("LISN", "5 µH LISN, − line, 50 Ω terminated", SlotPlace::Parallel, true),
                    slot("Power supply", "DC supply", SlotPlace::Supply, true),
                    limiter,
                    attenuator,
                    cable,
                    analyzer};
        case SetupMethod::CurrentProbe:
            return {slot("Current probe", "RF current probe", SlotPlace::Coupling),
                    optionalSlot("LISN", SlotPlace::Supply, false),
                    optionalSlot("Power supply", SlotPlace::Supply, false),
                    preamplifier(false),
                    cable,
                    analyzer};
        case SetupMethod::Antenna:
            return {slot("Antenna", "Measurement antenna", SlotPlace::Coupling),
                    cable,
                    preamplifier(false),
                    slot("RF cable", "RF cable to the analyzer", SlotPlace::Chain, true, false),
                    analyzer};
        case SetupMethod::TemCell:
            return {slot("TEM cell", "TEM cell", SlotPlace::Coupling),
                    preamplifier(false),
                    cable,
                    analyzer};
        case SetupMethod::NearFieldProbe:
            return {slot("Near-field probe", "Near-field probe", SlotPlace::Coupling),
                    preamplifier(true),
                    cable,
                    analyzer};
        case SetupMethod::Custom:
            break;
    }
    return {analyzer};
}

std::vector<EquipmentItem> TestSetup::equipment() const
{
    std::vector<EquipmentItem> items;
    items.reserve(devices.size());
    for (const auto& device : devices) {
        if (device.inUse) {
            items.push_back(device.equipment);
        }
    }
    return items;
}

std::vector<CorrectionTable> TestSetup::corrections() const
{
    std::vector<CorrectionTable> tables;
    for (const auto& device : devices) {
        if (device.inUse && device.correction) {
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
            return device.inUse && device.equipment.calibrationExpired(asOf);
        }));
}

std::vector<SetupBlock> TestSetup::blocks() const
{
    const auto slots = slotsFor(method);
    std::vector<bool> claimed(devices.size(), false);
    std::vector<SetupBlock> arranged;
    arranged.reserve(slots.size() + devices.size());
    for (std::size_t s = 0; s < slots.size(); ++s) {
        SetupBlock block{.slot = static_cast<int>(s), .device = -1, .place = slots[s].place};
        const auto role = lowered(slots[s].role);
        for (std::size_t d = 0; d < devices.size(); ++d) {
            if (!claimed[d] && lowered(devices[d].equipment.role) == role) {
                claimed[d] = true;
                block.device = static_cast<int>(d);
                break;
            }
        }
        arranged.push_back(block);
    }
    // Devices no slot names join the chain just before the analyzer.
    std::vector<SetupBlock> extras;
    for (std::size_t d = 0; d < devices.size(); ++d) {
        if (!claimed[d]) {
            extras.push_back(
                SetupBlock{.slot = -1, .device = static_cast<int>(d), .place = SlotPlace::Chain});
        }
    }
    const auto analyzer = std::find_if(arranged.begin(), arranged.end(), [](const auto& block) {
        return block.place == SlotPlace::Analyzer;
    });
    arranged.insert(analyzer, extras.begin(), extras.end());
    return arranged;
}

int TestSetup::setSlotInUse(int slot, bool inUse)
{
    const auto slots = slotsFor(method);
    if (slot < 0 || static_cast<std::size_t>(slot) >= slots.size()) {
        return -1;
    }
    const auto arranged = blocks();
    std::size_t position = 0;
    for (const auto& block : arranged) {
        if (block.slot != slot) {
            // After the devices of the slots before this one, so the list
            // stays in the diagram's order.
            if (block.slot >= 0 && block.slot < slot && block.device >= 0) {
                position = std::max(position, static_cast<std::size_t>(block.device) + 1);
            }
            continue;
        }
        if (block.device >= 0) {
            devices[static_cast<std::size_t>(block.device)].inUse = inUse;
            return block.device;
        }
        if (!inUse) {
            return -1;
        }
        devices.insert(devices.begin() + static_cast<std::ptrdiff_t>(position),
                       newDeviceFor(slots[static_cast<std::size_t>(slot)].role));
        return static_cast<int>(position);
    }
    return -1;
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
    setup.method = defaultMethodFor(kind);
    setup.createdAt = std::chrono::system_clock::now();
    setup.modifiedAt = setup.createdAt;
    return setup;
}

TestSetup TestSetup::createFor(std::string name, SetupMethod method)
{
    auto setup = createNew(std::move(name), kindOf(method));
    setup.method = method;
    for (const auto& slot : slotsFor(method)) {
        if (!slot.optional || slot.onByDefault) {
            setup.devices.push_back(newDeviceFor(slot.role));
        }
    }
    return setup;
}

SetupDevice newDeviceFor(std::string role)
{
    const auto text = lowered(role);
    std::optional<CorrectionTable> correction;
    if (text == "attenuator") {
        correction = flatCorrection("Attenuator", CorrectionKind::Attenuator, 10.0);
    } else if (text == "transient limiter") {
        correction = flatCorrection("Transient limiter", CorrectionKind::Attenuator, 10.0);
    } else if (text == "preamplifier") {
        correction = flatCorrection("Preamplifier", CorrectionKind::AmplifierGain, 20.0);
    }
    return SetupDevice{.equipment = EquipmentItem{.role = std::move(role)},
                       .correction = std::move(correction)};
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
