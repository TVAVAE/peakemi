# Rohde & Schwarz FSL

Driver id `rohdeschwarz.fsl`, chosen automatically for any `*IDN?` reply from
`Rohde&Schwarz` whose model starts with `FSL`. Source:
[RohdeSchwarzFslDriver.cpp](../../src/drivers/scpi/RohdeSchwarzFslDriver.cpp).

**Status: written from the documentation, not yet verified on an instrument.** The
commands, ranges and option rules below come from the R&S FSL operating manual
(1300.2519.12, edition E-12) and the R&S FSL data sheet (version 10.00, February 2010).
The unit test (`tests/unit/RohdeSchwarzFslTest.cpp`) replays replies taken from the
manual, not a transcript recorded on hardware. Until one is, treat results from this
driver as unconfirmed.

## Connecting

* **LAN: use VXI-11.** The FSL speaks VXI-11 and RSIB on its LAN port. The 2010 manual
  documents no raw socket, so a raw TCP connection on port 5025 is not expected to work.
* **GPIB** needs the R&S FSL-B10 option and a build with `PEAKEMI_WITH_VISA=ON`.

## What the driver does differently from the generic SCPI driver

| Setting | FSL command | Why it differs |
|---|---|---|
| Attenuation | `:INPut:ATTenuation`, `:INPut:ATTenuation:AUTO` | R&S keeps the attenuator under `INPut`; 0 to 50 dB (FSL18: 40 dB) in 5 dB steps |
| Preamplifier | `:INPut:GAIN:STATe` | Only sent when `*OPT?` lists `B22`; otherwise a run asking for it is refused |
| Trace format | `:FORMat:DATA ASCii` | R&S spelling of the data format command |
| Filter type | `:SENSe:BANDwidth:RESolution:TYPE PULSe` / `NORMal` | Sent **before** the bandwidth: the CISPR bandwidths exist only as EMI (6 dB) filters |
| Zero span | `:SENSe:FREQuency:CENTer f` then `:SENSe:FREQuency:SPAN 0` | So a verification span of 0 really dwells at the frequency |

The 200 Hz, 9 kHz, 120 kHz and 1 MHz bandwidths always use the EMI filters (`PULSe`), and
every other bandwidth uses the Gaussian filters (`NORMal`). If the filter type is changed
after the bandwidth, the FSL moves to the next larger filter of the new type, which is why
the order matters.

Capabilities are narrowed after `*IDN?` and `*OPT?`:

* Frequency range from the model: FSL3 3 GHz, FSL6 6 GHz, FSL18 18 GHz. The tracking
  generator is recognised from the `.13`, `.16` and `.28` variants.
* Without R&S FSL-B7 there is no 200 Hz EMI filter, so a CISPR band A run is refused.
* Firmware before 1.30 has no EMI filters, and firmware before 1.10 no quasi-peak detector.
* The FSL has no video bandwidth while the quasi-peak detector is on, so a sweep asking for
  both is refused. Phase 2 leaves the video bandwidth on automatic and is not affected.
* Reference level 27.0 to 126.9 dBµV, which is the data sheet's −80 to +20 dBm.

## Verifying it on an instrument

Run these against a real FSL and record what happens. Each line is a claim the driver
makes from the documentation.

1. Connect over VXI-11 and check that the instrument dock shows `R&S FSL6` (or FSL3 or
   FSL18). The log shows the `*OPT?` result: narrow filters, preamplifier, EMI filters.
2. Open the SCPI console and send `*OPT?`. Compare the reply with the options the log
   reported.
3. Run Phase 1 over 30 to 230 MHz at 120 kHz. On the instrument, check that the filter
   type reads **EMI (6dB)** and the resolution bandwidth reads **120 kHz**, not 100 kHz
   or 300 kHz.
4. Run a 150 kHz to 30 MHz scan at 9 kHz and check the same for 9 kHz.
5. Set the verification span to 0 and verify one peak with the quasi-peak detector. The
   instrument should be in zero span at the peak frequency, with detector **QP**, for the
   dwell time.
6. Send `:SYSTem:ERRor?` in the SCPI console after a run until it answers `0,"No error"`.
   Any other entry means a command above is wrong for this firmware: note the entry and
   the firmware version from `*IDN?`.
7. Check one level against the front panel: put a marker on a verified peak and compare
   it with PeakEmi's raw amplitude, both in dBµV.

When all seven agree, replace the scripted replies in `RohdeSchwarzFslTest.cpp` with the
real `*IDN?` and `*OPT?` replies and the error-queue result, and remove the "not yet
verified" note here and in the README.
