#include "ScriptedTransport.h"
#include "TestSupport.h"

#include <peakemi/drivers/RohdeSchwarzFslDriver.h>
#include <peakemi/drivers/SimulatedDriver.h>
#include <peakemi/hal/DriverRegistry.h>

#include <QTest>

#include <algorithm>
#include <memory>

using namespace peakemi;

namespace {

// *IDN? in the new format (part number/serial) and the legacy one
// (serial/model), both from operating manual 1300.2519.12, E-12, page 6.7.
constexpr auto Fsl6Idn = "Rohde&Schwarz,FSL-6,1300.2502K06/100005,1.80";
constexpr auto Fsl6TrackingGeneratorIdn = "Rohde&Schwarz,FSL-6,1300.2502K16/100005,1.80";
constexpr auto Fsl6LegacyTrackingGeneratorIdn = "Rohde&Schwarz,FSL-6,100005/016,1.80";

/// An FSL6 that names itself and lists @p options in *OPT?.
///
/// The replies follow the operating manual; this is not yet a transcript
/// recorded on an instrument, which is what docs/instruments/rs-fsl.md asks for
/// before the driver is called verified.
[[nodiscard]] std::shared_ptr<test::ScriptedTransport> fslTransport(const char* idn = Fsl6Idn,
                                                                    const char* options = "B10,B22")
{
    auto transport = std::make_shared<test::ScriptedTransport>();
    transport->setResponse("*IDN?", idn);
    transport->setResponse("*OPT?", options);
    transport->setResponse("*OPC?", "1");
    transport->setResponse(":SYSTem:ERRor?", "0,\"No error\"");
    return transport;
}

[[nodiscard]] SweepParams cisprBandC()
{
    SweepParams params;
    params.span = FrequencyRange{megahertz(30), megahertz(230)};
    params.points = 1001;
    params.rbw = kilohertz(120);
    params.refLevel = decibel(80.0);
    return params;
}

[[nodiscard]] bool offers(const std::vector<Hertz>& bandwidths, Hertz wanted)
{
    return std::find(bandwidths.begin(), bandwidths.end(), wanted) != bandwidths.end();
}

} // namespace

class RohdeSchwarzFslTest : public QObject
{
    Q_OBJECT

private slots:
    void modelsComeFromTheIdentity();
    void optionsAndFirmwareNarrowTheCapabilities();
    void cisprBandwidthsUseTheEmiFilters();
    void registryChoosesTheFslDriver();
    void driverNarrowsItsCapabilitiesAfterIdentifying();
    void driverSpeaksTheRohdeSchwarzDialect();
    void filterTypeIsSentBeforeTheBandwidth();
    void preamplifierIsNotSentWithoutTheOption();
    void quasiPeakRefusesAVideoBandwidth();
    void zeroSpanIsSetAsZeroSpan();
    void driverCompletesASweep();
};

void RohdeSchwarzFslTest::modelsComeFromTheIdentity()
{
    InstrumentId identity;
    identity.manufacturer = "Rohde&Schwarz";
    identity.model = "FSL-6";
    identity.serial = "1300.2502K06/100005";
    auto model = drivers::fslModelFor(identity);
    QVERIFY(model.has_value());
    QCOMPARE(model->name, std::string{"R&S FSL6"});
    QCOMPARE(model->maximumFrequency, gigahertz(6.0));
    QCOMPARE(model->maximumAttenuation, decibel(50.0));
    QVERIFY(!model->trackingGenerator);

    // The tracking-generator variant, in both serial formats.
    identity.serial = "1300.2502K16/100005";
    QVERIFY(drivers::fslModelFor(identity)->trackingGenerator);
    identity.serial = "100005/016";
    QVERIFY(drivers::fslModelFor(identity)->trackingGenerator);

    // The FSL18 has a smaller attenuator.
    identity.model = "FSL-18";
    identity.serial = "1300.2502K18/100005";
    model = drivers::fslModelFor(identity);
    QVERIFY(model.has_value());
    QCOMPARE(model->maximumFrequency, gigahertz(18.0));
    QCOMPARE(model->maximumAttenuation, decibel(40.0));

    // Other R&S analyzers are not this driver's to claim.
    identity.model = "FSV-7";
    QVERIFY(!drivers::fslModelFor(identity).has_value());
    identity.manufacturer = "Siglent";
    identity.model = "FSL-6";
    QVERIFY(!drivers::fslModelFor(identity).has_value());
}

void RohdeSchwarzFslTest::optionsAndFirmwareNarrowTheCapabilities()
{
    const drivers::FslModel fsl6{.name = "R&S FSL6",
                                 .maximumFrequency = gigahertz(6.0),
                                 .maximumAttenuation = decibel(50.0)};

    // A bare unit: the EMI filters of bands B to E, but not the 200 Hz of band
    // A, which needs the narrow filters of R&S FSL-B7.
    auto options = drivers::fslOptionsFor("B10", "1.80");
    QVERIFY(!options.narrowFilters);
    QVERIFY(!options.preamplifier);
    auto capabilities = drivers::fslCapabilities(fsl6, options);
    QCOMPARE(capabilities.range.stop, gigahertz(6.0));
    QVERIFY(offers(capabilities.resolutionBandwidths, kilohertz(9)));
    QVERIFY(offers(capabilities.resolutionBandwidths, kilohertz(120)));
    QVERIFY(offers(capabilities.resolutionBandwidths, megahertz(1)));
    QVERIFY(!offers(capabilities.resolutionBandwidths, hertz(200)));
    QVERIFY(!offers(capabilities.resolutionBandwidths, hertz(100)));
    QVERIFY(capabilities.supports(Detector::QuasiPeak));
    QVERIFY(!capabilities.preamp);
    QCOMPARE(capabilities.maximumAttenuation, decibel(50.0));
    QCOMPARE(capabilities.attenuationStep, decibel(5.0));
    QCOMPARE(capabilities.nativeUnit, AmplitudeUnit::dBuV);

    // B7 and B22 fitted, the way *OPT? lists them.
    options = drivers::fslOptionsFor("B4,B5,B6,B7,B8,B10,B22,B30,B31,K7,K9", "1.80");
    QVERIFY(options.narrowFilters);
    QVERIFY(options.preamplifier);
    capabilities = drivers::fslCapabilities(fsl6, options);
    QVERIFY(offers(capabilities.resolutionBandwidths, hertz(200)));
    QVERIFY(offers(capabilities.resolutionBandwidths, hertz(10)));
    QVERIFY(capabilities.preamp);
    QVERIFY(std::is_sorted(capabilities.resolutionBandwidths.begin(),
                           capabilities.resolutionBandwidths.end()));

    // EMI filters arrived with firmware 1.30 and the quasi-peak detector with
    // 1.10; an older unit must not be asked for either.
    options = drivers::fslOptionsFor("B7", "1.20");
    capabilities = drivers::fslCapabilities(fsl6, options);
    QVERIFY(!offers(capabilities.resolutionBandwidths, kilohertz(9)));
    QVERIFY(capabilities.supports(Detector::QuasiPeak));
    options = drivers::fslOptionsFor("", "1.00");
    QVERIFY(!drivers::fslCapabilities(fsl6, options).supports(Detector::QuasiPeak));
}

void RohdeSchwarzFslTest::cisprBandwidthsUseTheEmiFilters()
{
    for (const auto cispr : {hertz(200), kilohertz(9), kilohertz(120), megahertz(1)}) {
        QCOMPARE(drivers::fslFilterType(cispr), std::string{"PULSe"});
    }
    for (const auto gaussian : {hertz(300), kilohertz(10), kilohertz(100), megahertz(3)}) {
        QCOMPARE(drivers::fslFilterType(gaussian), std::string{"NORMal"});
    }
}

void RohdeSchwarzFslTest::registryChoosesTheFslDriver()
{
    drivers::registerBuiltInDrivers();
    const auto& registry = hal::DriverRegistry::instance();

    InstrumentId identity;
    identity.manufacturer = "Rohde&Schwarz";
    identity.model = "FSL-6";
    identity.raw = Fsl6Idn;
    const auto matches = registry.match(identity);
    QVERIFY(!matches.empty());
    QCOMPARE(matches.front().driverId, std::string{"rohdeschwarz.fsl"});

    auto driver = registry.createBestMatch(identity);
    const auto reason = test::errorText(driver);
    QVERIFY2(driver.has_value(), reason.constData());
    QCOMPARE((*driver)->info().id, std::string{"rohdeschwarz.fsl"});
}

void RohdeSchwarzFslTest::driverNarrowsItsCapabilitiesAfterIdentifying()
{
    auto transport = fslTransport(Fsl6Idn, "B10");
    auto driver = drivers::makeRohdeSchwarzFslDriver();
    QVERIFY(driver->open(transport).has_value());

    // Before *IDN?: the widest of the family.
    QCOMPARE(driver->capabilities().range.stop, gigahertz(18.0));

    const auto identity = driver->identify();
    QVERIFY(identity.has_value());
    QCOMPARE(identity->model, std::string{"FSL-6"});
    QCOMPARE(identity->firmware, std::string{"1.80"});
    QCOMPARE(driver->info().name, std::string{"R&S FSL6"});
    QCOMPARE(driver->capabilities().range.stop, gigahertz(6.0));
    QVERIFY(transport->sawCommandStartingWith("*OPT?"));

    // Without B7 there is no 200 Hz EMI filter; asked for one, the FSL would
    // move to the next larger filter, so the run is refused instead.
    auto params = cisprBandC();
    params.span = FrequencyRange{kilohertz(9), kilohertz(150)};
    params.rbw = hertz(200);
    auto refused = driver->configureSweep(params);
    QVERIFY(!refused.has_value());
    QCOMPARE(refused.error().code, ErrorCode::UnsupportedSetting);

    // And a span past 6 GHz, which the family default would have allowed.
    params = cisprBandC();
    params.span = FrequencyRange{gigahertz(1.0), gigahertz(7.0)};
    params.rbw = megahertz(1);
    refused = driver->configureSweep(params);
    QVERIFY(!refused.has_value());
    QCOMPARE(refused.error().code, ErrorCode::UnsupportedSetting);
}

void RohdeSchwarzFslTest::driverSpeaksTheRohdeSchwarzDialect()
{
    auto transport = fslTransport();
    auto driver = drivers::makeRohdeSchwarzFslDriver();
    QVERIFY(driver->open(transport).has_value());
    QVERIFY(driver->identify().has_value());

    auto params = cisprBandC();
    params.automaticAttenuation = false;
    params.attenuation = decibel(10.0);
    QVERIFY(driver->configureSweep(params).has_value());

    // R&S keeps the attenuator and preamplifier under INPut.
    QVERIFY(transport->sawCommandStartingWith(":INPut:ATTenuation:AUTO OFF"));
    QVERIFY(transport->sawCommandStartingWith(":INPut:ATTenuation 10.00"));
    QVERIFY(transport->sawCommandStartingWith(":INPut:GAIN:STATe OFF"));
    QVERIFY(!transport->sawCommandStartingWith(":SENSe:POWer:RF"));

    // The settable point count is sent, and the unit precedes the level.
    QVERIFY(transport->sawCommandStartingWith(":SENSe:SWEep:POINts 1001"));
    const auto unit = transport->indexOfCommandStartingWith(":UNIT:POWer DBUV");
    const auto level =
        transport->indexOfCommandStartingWith(":DISPlay:WINDow:TRACe:Y:SCALe:RLEVel");
    QVERIFY(unit >= 0);
    QVERIFY(level > unit);
}

void RohdeSchwarzFslTest::filterTypeIsSentBeforeTheBandwidth()
{
    auto transport = fslTransport();
    auto driver = drivers::makeRohdeSchwarzFslDriver();
    QVERIFY(driver->open(transport).has_value());
    QVERIFY(driver->identify().has_value());

    QVERIFY(driver->configureSweep(cisprBandC()).has_value());
    const auto type =
        transport->indexOfCommandStartingWith(":SENSe:BANDwidth:RESolution:TYPE PULSe");
    const auto bandwidth =
        transport->indexOfCommandStartingWith(":SENSe:BANDwidth:RESolution 120000");
    QVERIFY(type >= 0);
    QVERIFY(bandwidth >= 0);
    QVERIFY2(type < bandwidth, "the EMI filter type must be selected before the 120 kHz bandwidth");

    // A non-CISPR bandwidth goes back to the Gaussian filters.
    auto params = cisprBandC();
    params.rbw = kilohertz(100);
    QVERIFY(driver->configureSweep(params).has_value());
    QVERIFY(transport->sawCommandStartingWith(":SENSe:BANDwidth:RESolution:TYPE NORMal"));
}

void RohdeSchwarzFslTest::preamplifierIsNotSentWithoutTheOption()
{
    auto transport = fslTransport(Fsl6Idn, "B10");
    auto driver = drivers::makeRohdeSchwarzFslDriver();
    QVERIFY(driver->open(transport).has_value());
    QVERIFY(driver->identify().has_value());
    QVERIFY(!driver->capabilities().preamp);

    QVERIFY(driver->configureSweep(cisprBandC()).has_value());
    QVERIFY(!transport->sawCommandStartingWith(":INPut:GAIN"));

    auto params = cisprBandC();
    params.preamp = true;
    QVERIFY(!driver->configureSweep(params).has_value());
}

void RohdeSchwarzFslTest::quasiPeakRefusesAVideoBandwidth()
{
    auto transport = fslTransport();
    auto driver = drivers::makeRohdeSchwarzFslDriver();
    QVERIFY(driver->open(transport).has_value());
    QVERIFY(driver->identify().has_value());

    auto params = cisprBandC();
    params.detector = Detector::QuasiPeak;
    params.vbw = kilohertz(300);
    const auto refused = driver->configureSweep(params);
    QVERIFY(!refused.has_value());
    QCOMPARE(refused.error().code, ErrorCode::UnsupportedSetting);

    // Phase 2 leaves the video bandwidth on automatic, which is accepted.
    params.vbw = Hertz{0};
    QVERIFY(driver->configureSweep(params).has_value());
    QVERIFY(transport->sawCommandStartingWith(":SENSe:DETector:FUNCtion QPEak"));
    QVERIFY(!transport->sawCommandStartingWith(":SENSe:BANDwidth:VIDeo"));
}

void RohdeSchwarzFslTest::zeroSpanIsSetAsZeroSpan()
{
    auto transport = fslTransport();
    auto driver = drivers::makeRohdeSchwarzFslDriver();
    QVERIFY(driver->open(transport).has_value());
    QVERIFY(driver->identify().has_value());

    auto params = cisprBandC();
    params.span = FrequencyRange{megahertz(100), megahertz(100)};
    params.points = 101;
    params.detector = Detector::QuasiPeak;
    params.sweepTime = std::chrono::milliseconds{1000};
    QVERIFY(driver->configureSweep(params).has_value());

    const auto centre = transport->indexOfCommandStartingWith(":SENSe:FREQuency:CENTer 100000000");
    const auto span = transport->indexOfCommandStartingWith(":SENSe:FREQuency:SPAN 0");
    const auto stop = transport->indexOfCommandStartingWith(":SENSe:FREQuency:STOP");
    QVERIFY(centre >= 0);
    QVERIFY(span > centre);
    QVERIFY2(stop < centre, "zero span must be set after start and stop");
    QVERIFY(transport->sawCommandStartingWith(":SENSe:SWEep:TIME 1.0"));
}

void RohdeSchwarzFslTest::driverCompletesASweep()
{
    auto transport = fslTransport();
    transport->setResponse(":TRACe:DATA? TRACE1", "35.2,38.9,61.4,40.1");
    auto driver = drivers::makeRohdeSchwarzFslDriver();
    QVERIFY(driver->open(transport).has_value());

    auto params = cisprBandC();
    params.span = FrequencyRange{megahertz(88), megahertz(108)};
    QVERIFY(driver->configureSweep(params).has_value());

    const CancelToken cancel;
    QVERIFY(driver->armAndTrigger(cancel).has_value());
    const auto trace = driver->fetchTrace(cancel);
    const auto reason = test::errorText(trace);
    QVERIFY2(trace.has_value(), reason.constData());
    QVERIFY(transport->sawCommandStartingWith(":FORMat:DATA ASCii"));
    QCOMPARE(trace->amplitudes, (std::vector<double>{35.2, 38.9, 61.4, 40.1}));
    QCOMPARE(trace->unit, AmplitudeUnit::dBuV);
    QCOMPARE(trace->source.manufacturer, std::string{"Rohde&Schwarz"});
    QCOMPARE(trace->source.model, std::string{"FSL-6"});
    QCOMPARE(trace->axis.start, megahertz(88));
    QCOMPARE(trace->axis.stop, megahertz(108));

    // The tracking-generator unit is recognised from either serial format.
    for (const auto* idn : {Fsl6TrackingGeneratorIdn, Fsl6LegacyTrackingGeneratorIdn}) {
        auto tg = drivers::makeRohdeSchwarzFslDriver();
        QVERIFY(tg->open(fslTransport(idn)).has_value());
        QVERIFY(tg->identify().has_value());
        QVERIFY(tg->capabilities().trackingGenerator);
    }
}

QTEST_APPLESS_MAIN(RohdeSchwarzFslTest)
#include "RohdeSchwarzFslTest.moc"
