#include <peakemi/core/Logging.h>
#include <peakemi/drivers/RohdeSchwarzFslDriver.h>
#include <peakemi/hal/Scpi.h>

#include <QString>

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace peakemi::drivers {
namespace {

/// Selects Gaussian, FFT, channel, RRC or EMI filters for the resolution
/// bandwidth (operating manual 1300.2519.12, E-12, "SENSe:BANDwidth").
constexpr auto FilterTypeCommand = ":SENSe:BANDwidth:RESolution:TYPE";
constexpr auto GaussianFilters = "NORMal";
constexpr auto EmiFilters = "PULSe";

[[nodiscard]] std::string lowered(std::string_view text)
{
    std::string result{text};
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return result;
}

[[nodiscard]] std::string trimmed(std::string_view text)
{
    const auto first = text.find_first_not_of(" \t\r\n\"");
    if (first == std::string_view::npos) {
        return {};
    }
    const auto last = text.find_last_not_of(" \t\r\n\"");
    return std::string{text.substr(first, last - first + 1)};
}

/// "1.80" -> 180, "2.30 SP1" -> 230; nothing when the reply is not a version.
[[nodiscard]] std::optional<int> firmwareHundredths(std::string_view firmware)
{
    const auto text = trimmed(firmware);
    const auto dot = text.find('.');
    if (dot == std::string::npos || dot == 0) {
        return std::nullopt;
    }
    int major = 0;
    for (std::size_t i = 0; i < dot; ++i) {
        if (std::isdigit(static_cast<unsigned char>(text[i])) == 0) {
            return std::nullopt;
        }
        major = major * 10 + (text[i] - '0');
    }
    int minor = 0;
    int digits = 0;
    for (std::size_t i = dot + 1; i < text.size() && digits < 2; ++i, ++digits) {
        if (std::isdigit(static_cast<unsigned char>(text[i])) == 0) {
            break;
        }
        minor = minor * 10 + (text[i] - '0');
    }
    if (digits == 0) {
        return std::nullopt;
    }
    if (digits == 1) {
        minor *= 10;
    }
    return major * 100 + minor;
}

/// The widest member of the family, assumed until *IDN? and *OPT? have been
/// read: an FSL18 range with the FSL3/FSL6 attenuator and every option fitted.
/// A narrower set takes over as soon as the instrument names itself, and
/// assuming too little here would reject settings the instrument supports.
[[nodiscard]] Capabilities familyCapabilities()
{
    return fslCapabilities(
        FslModel{.name = "R&S FSL",
                 .maximumFrequency = gigahertz(18.0),
                 .maximumAttenuation = decibel(50.0),
                 .trackingGenerator = true},
        FslOptions{
            .narrowFilters = true, .preamplifier = true, .emiFilters = true, .quasiPeak = true});
}

} // namespace

std::optional<FslModel> fslModelFor(const InstrumentId& identity)
{
    if (lowered(identity.manufacturer).find("rohde") == std::string::npos) {
        return std::nullopt;
    }

    // "FSL-6" in *IDN?, "FSL6" on the front panel and in the manuals.
    std::string model;
    for (const char character : lowered(identity.model)) {
        if (std::isalnum(static_cast<unsigned char>(character)) != 0) {
            model.push_back(character);
        }
    }

    FslModel result;
    if (model == "fsl3") {
        result = FslModel{.name = "R&S FSL3",
                          .maximumFrequency = gigahertz(3.0),
                          .maximumAttenuation = decibel(50.0)};
    } else if (model == "fsl6") {
        result = FslModel{.name = "R&S FSL6",
                          .maximumFrequency = gigahertz(6.0),
                          .maximumAttenuation = decibel(50.0)};
    } else if (model == "fsl18") {
        // The FSL18 overranges to 20 GHz, but the data sheet specifies it to 18.
        result = FslModel{.name = "R&S FSL18",
                          .maximumFrequency = gigahertz(18.0),
                          .maximumAttenuation = decibel(40.0)};
    } else {
        return std::nullopt;
    }

    // The tracking-generator models are .13, .16 and .28. The new *IDN? format
    // carries the part number ("1300.2502K16/100005"), the legacy one the model
    // after the serial number ("100005/016").
    const auto serial = lowered(trimmed(identity.serial));
    for (const std::string_view variant : {"13", "16", "28"}) {
        const std::string partNumber = "k" + std::string{variant};
        const std::string legacy = "/0" + std::string{variant};
        if (serial.find(partNumber) != std::string::npos || serial.ends_with(legacy)) {
            result.trackingGenerator = true;
        }
    }
    return result;
}

FslOptions fslOptionsFor(std::string_view optionReply, std::string_view firmware)
{
    FslOptions options;

    // "B4,B5,B6,B7,B8,B10,B22,B30,B31,K7,K9" -- hardware options are B<n>,
    // firmware options K<n>. Some firmware prefixes them with the family name.
    std::string token;
    const auto consider = [&options](std::string option) {
        option = lowered(trimmed(option));
        if (option.starts_with("fsl-")) {
            option.erase(0, 4);
        }
        if (option == "b7") {
            options.narrowFilters = true;
        } else if (option == "b22") {
            options.preamplifier = true;
        }
    };
    for (const char character : optionReply) {
        if (character == ',') {
            consider(token);
            token.clear();
        } else {
            token.push_back(character);
        }
    }
    consider(token);

    if (const auto version = firmwareHundredths(firmware)) {
        options.emiFilters = *version >= 130;
        options.quasiPeak = *version >= 110;
    } else {
        qCInfo(lcDriver) << "FSL firmware"
                         << QString::fromUtf8(firmware.data(),
                                              static_cast<qsizetype>(firmware.size()))
                         << "not understood - assuming EMI filters and quasi-peak are present";
    }
    return options;
}

Capabilities fslCapabilities(const FslModel& model, const FslOptions& options)
{
    // Gaussian (-3 dB) filters: 300 Hz to 10 MHz in a 1-3-10 sequence, from
    // 10 Hz with R&S FSL-B7. 20 MHz exists only in zero span and is left out.
    std::vector<Hertz> resolution{hertz(300),
                                  kilohertz(1),
                                  kilohertz(3),
                                  kilohertz(10),
                                  kilohertz(30),
                                  kilohertz(100),
                                  kilohertz(300),
                                  megahertz(1),
                                  megahertz(3),
                                  megahertz(10)};
    if (options.narrowFilters) {
        resolution.insert(resolution.end(), {hertz(10), hertz(30), hertz(100)});
    }
    // EMI (-6 dB) filters: 9 kHz, 120 kHz and 1 MHz, and 200 Hz with FSL-B7.
    // These are the bandwidths CISPR 16-1-1 mandates, and without them a Phase
    // 2 dwell cannot be configured; listing them only when the firmware and the
    // options provide them is what refuses a run the instrument would
    // otherwise measure with a neighbouring filter.
    if (options.emiFilters) {
        resolution.insert(resolution.end(), {kilohertz(9), kilohertz(120)});
        if (options.narrowFilters) {
            resolution.push_back(hertz(200));
        }
    }
    std::sort(resolution.begin(), resolution.end());
    resolution.erase(std::unique(resolution.begin(), resolution.end()), resolution.end());

    std::vector<Detector> detectors{Detector::Peak};
    if (options.quasiPeak) {
        detectors.push_back(Detector::QuasiPeak);
    }
    detectors.insert(detectors.end(), {Detector::Average, Detector::Rms, Detector::Sample});

    return Capabilities{.range = FrequencyRange{hertz(9000), model.maximumFrequency},
                        // ":SENSe:SWEep:POINts" takes 101 to 32001; 501 after *RST.
                        .minimumPoints = 101,
                        .maximumPoints = 32001,
                        .detectors = std::move(detectors),
                        .resolutionBandwidths = std::move(resolution),
                        // 1 Hz to 10 MHz in a 1-3-10 sequence.
                        .videoBandwidths = {hertz(1),
                                            hertz(3),
                                            hertz(10),
                                            hertz(30),
                                            hertz(100),
                                            hertz(300),
                                            kilohertz(1),
                                            kilohertz(3),
                                            kilohertz(10),
                                            kilohertz(30),
                                            kilohertz(100),
                                            kilohertz(300),
                                            megahertz(1),
                                            megahertz(3),
                                            megahertz(10)},
                        .minimumAttenuation = decibel(0.0),
                        .maximumAttenuation = model.maximumAttenuation,
                        .attenuationStep = decibel(5.0),
                        // The data sheet's -80 to +20 dBm, in the dBuV PeakEmi
                        // asks for (+107 dB), rounded inwards so that a value
                        // accepted here is one the instrument takes unclamped.
                        .minimumRefLevel = decibel(27.0),
                        .maximumRefLevel = decibel(126.9),
                        .preamp = options.preamplifier,
                        .trackingGenerator = model.trackingGenerator,
                        .zeroSpan = true,
                        .nativeUnit = AmplitudeUnit::dBuV};
}

ScpiDialect fslDialect()
{
    // Everything not overridden here is the generic SCPI spelling, which the
    // FSL accepts: frequencies, bandwidths, sweep points and time, detector,
    // reference level, ":UNIT:POWer", ":INITiate", "*OPC?", ":SYSTem:ERRor?".
    ScpiDialect dialect;
    // R&S keeps the attenuator and the preamplifier under INPut, not under
    // SENSe:POWer:RF.
    dialect.attenuation = ":INPut:ATTenuation";
    dialect.attenuationAuto = ":INPut:ATTenuation:AUTO";
    dialect.preamp = ":INPut:GAIN:STATe";
    // ASCII is the *RST format already; it is set anyway, because a previous
    // remote session may have left the instrument on REAL,32.
    dialect.traceFormat = ":FORMat:DATA";
    dialect.traceFormatAscii = "ASCii";
    dialect.traceQuery = ":TRACe:DATA? TRACE1";
    return dialect;
}

std::string fslFilterType(Hertz rbw)
{
    for (const auto cispr : {hertz(200), kilohertz(9), kilohertz(120), megahertz(1)}) {
        if (rbw == cispr) {
            return EmiFilters;
        }
    }
    return GaussianFilters;
}

RohdeSchwarzFslDriver::RohdeSchwarzFslDriver()
    : ScpiAnalyzerDriver{DriverInfo{
                             .id = "rohdeschwarz.fsl",
                             .name = "R&S FSL",
                             .vendor = "Rohde & Schwarz",
                             .version = "1.0",
                             .origin = "built-in",
                             .supportedTransports = {TransportKind::Vxi11, TransportKind::Visa}},
                         familyCapabilities(),
                         fslDialect()}
    , m_fslCapabilities{familyCapabilities()}
    , m_options{.narrowFilters = true, .preamplifier = true, .emiFilters = true, .quasiPeak = true}
    , m_name{"R&S FSL"}
{}

DriverInfo RohdeSchwarzFslDriver::info() const
{
    auto info = ScpiAnalyzerDriver::info();
    info.name = m_name;
    return info;
}

Result<InstrumentId> RohdeSchwarzFslDriver::identify()
{
    auto identity = ScpiAnalyzerDriver::identify();
    if (!identity || m_identified) {
        return identity;
    }
    m_identified = true;

    const auto model = fslModelFor(*identity);
    if (!model) {
        qCInfo(lcDriver) << "no FSL model for" << QString::fromStdString(identity->model)
                         << "- keeping the family defaults";
        return identity;
    }

    std::string optionReply;
    if (auto reply = query("*OPT?", CancelToken{})) {
        optionReply = *reply;
    } else {
        // Assuming the options are absent refuses a 200 Hz or preamplified run
        // that might have worked; assuming them present would measure one that
        // cannot, with whatever the instrument substitutes.
        qCWarning(lcDriver) << "could not read *OPT? - assuming no options are fitted";
    }

    m_options = fslOptionsFor(optionReply, identity->firmware);
    m_fslCapabilities = fslCapabilities(*model, m_options);
    m_name = model->name;
    if (!m_options.preamplifier) {
        // Without R&S FSL-B22 the command does not exist; an empty command is
        // one the generic driver skips.
        m_dialect.preamp.clear();
    }
    qCInfo(lcDriver) << "profile adopted:" << QString::fromStdString(m_name) << "- range up to"
                     << toMegahertz(model->maximumFrequency) << "MHz,"
                     << "narrow filters" << m_options.narrowFilters << "preamplifier"
                     << m_options.preamplifier << "EMI filters" << m_options.emiFilters;
    return identity;
}

Status RohdeSchwarzFslDriver::configureSweep(const SweepParams& params)
{
    if (!isOpen()) {
        return fail(ErrorCode::NotConnected, m_name);
    }
    if (auto status = m_fslCapabilities.validate(params); !status) {
        return status;
    }
    if (params.detector == Detector::QuasiPeak && params.vbw > Hertz{0}) {
        return fail(ErrorCode::UnsupportedSetting,
                    "the FSL has no video bandwidth while the quasi-peak detector is on; "
                    "leave the video bandwidth on automatic");
    }

    // Before the bandwidth: see fslFilterType and the class comment.
    const std::string filterType =
        m_options.emiFilters && params.rbw > Hertz{0} ? fslFilterType(params.rbw) : GaussianFilters;
    if (auto status = sendValue(FilterTypeCommand, filterType); !status) {
        return status;
    }

    if (auto status = ScpiAnalyzerDriver::configureSweep(params); !status) {
        return status;
    }

    if (params.span.width() == Hertz{0}) {
        // Start and stop have just been sent equal, which the FSL may take as
        // its minimum span rather than as zero span. Centre and a span of 0
        // leave no doubt.
        if (auto status =
                sendValue(":SENSe:FREQuency:CENTer", scpi::formatHertz(params.span.start));
            !status)
        {
            return status;
        }
        if (auto status = sendValue(":SENSe:FREQuency:SPAN", "0"); !status) {
            return status;
        }
    }
    return {};
}

DriverPtr makeRohdeSchwarzFslDriver()
{
    return std::make_shared<RohdeSchwarzFslDriver>();
}

} // namespace peakemi::drivers
