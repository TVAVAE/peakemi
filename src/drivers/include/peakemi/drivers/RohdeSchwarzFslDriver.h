#pragma once

#include <peakemi/core/Capabilities.h>
#include <peakemi/drivers/ScpiAnalyzerDriver.h>

#include <optional>
#include <string>
#include <string_view>

namespace peakemi::drivers {

/// One member of the R&S FSL family, as its *IDN? reply identifies it.
///
/// The numbers are taken from the R&S FSL data sheet (version 10.00, February
/// 2010) rather than measured: FSL3 and FSL6 have a 0 to 50 dB attenuator, the
/// FSL18 a 0 to 40 dB one, all in 5 dB steps. The models ending .13, .16 and .28
/// carry a tracking generator.
struct FslModel
{
    std::string name;
    Hertz maximumFrequency;
    Decibel maximumAttenuation;
    bool trackingGenerator{false};
};

/// The options that change what a run may ask an FSL for, read from *OPT?.
struct FslOptions
{
    /// R&S FSL-B7, narrow IF filters: adds the 10 Hz to 100 Hz Gaussian filters
    /// and the 200 Hz EMI filter that CISPR band A needs.
    bool narrowFilters{false};
    /// R&S FSL-B22, RF preamplifier. Without it ":INPut:GAIN:STATe" is not a
    /// command the instrument knows.
    bool preamplifier{false};
    /// Firmware before 1.30 has no EMI (6 dB) filters and firmware before 1.10
    /// no quasi-peak detector (operating manual 1300.2519.12, E-12).
    bool emiFilters{true};
    bool quasiPeak{true};
};

/// The model an FSL *IDN? reply names, or nothing for a model this build does
/// not know. Both the legacy ("100005/016") and the new
/// ("1300.2502K16/100005") serial formats are understood.
[[nodiscard]] std::optional<FslModel> fslModelFor(const InstrumentId& identity);

/// The options an *OPT? reply lists, combined with what the firmware in the
/// *IDN? reply can do.
[[nodiscard]] FslOptions fslOptionsFor(std::string_view optionReply, std::string_view firmware);

/// What @p model with @p options can do.
[[nodiscard]] Capabilities fslCapabilities(const FslModel& model, const FslOptions& options);

/// How an FSL spells the settings PeakEmi sends.
[[nodiscard]] ScpiDialect fslDialect();

/// The resolution filter type an FSL needs for @p rbw.
///
/// 200 Hz, 9 kHz, 120 kHz and 1 MHz are the CISPR 16-1-1 bandwidths, which the
/// standard defines at -6 dB. The FSL realises them only as its EMI filters
/// ("PULSe"); its default Gaussian filters ("NORMal") are -3 dB filters in a
/// 1-3-10 sequence. Asked for 9 kHz with the Gaussian type selected, it does
/// not refuse: it picks a neighbouring filter, and a quasi-peak reading made
/// with it carries a bandwidth the report cannot justify.
[[nodiscard]] std::string fslFilterType(Hertz rbw);

/// Rohde & Schwarz FSL3, FSL6 and FSL18 in spectrum-analyzer mode.
///
/// The FSL follows SCPI closely, so this is ScpiAnalyzerDriver with an R&S
/// dialect and three steps of its own:
///
/// * **The filter type goes first.** Before the resolution bandwidth is sent,
///   ":SENSe:BANDwidth:RESolution:TYPE" selects the EMI filters for a CISPR
///   bandwidth and the Gaussian ones for anything else (see fslFilterType).
///   The order matters: the FSL keeps the bandwidth when the type changes only
///   if the new type has it, and otherwise moves to the next larger one.
/// * **The options decide the capabilities.** *OPT? is read once, after
///   *IDN?, so a unit without the narrow filters refuses a 200 Hz run and a
///   unit without the preamplifier refuses a run that asks for it, instead of
///   either being quietly measured with something else.
/// * **Zero span is set as zero span.** A verification span of 0 is sent as
///   centre frequency and ":SENSe:FREQuency:SPAN 0", which is what dwells the
///   quasi-peak detector at the frequency for the whole dwell time.
///
/// The FSL has no video bandwidth while the quasi-peak detector is on, so a
/// sweep asking for both is refused here rather than half applied.
///
/// Remote control is over LAN with VXI-11 (or RSIB, which PeakEmi does not
/// speak), or over GPIB with the R&S FSL-B10 option through VISA. The 2010
/// operating manual documents no raw socket, so choose VXI-11 for a LAN
/// connection.
class RohdeSchwarzFslDriver final : public ScpiAnalyzerDriver
{
public:
    RohdeSchwarzFslDriver();

    [[nodiscard]] DriverInfo info() const override;

    [[nodiscard]] Capabilities capabilities() const override { return m_fslCapabilities; }

    [[nodiscard]] Result<InstrumentId> identify() override;
    [[nodiscard]] Status configureSweep(const SweepParams& params) override;

private:
    Capabilities m_fslCapabilities;
    FslOptions m_options;
    std::string m_name;
    bool m_identified{false};
};

[[nodiscard]] DriverPtr makeRohdeSchwarzFslDriver();

} // namespace peakemi::drivers
