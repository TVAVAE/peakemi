# Changelog

Changes in this fork ([TVAVAE/peakemi](https://github.com/TVAVAE/peakemi)) on top of
[PeakEmi](https://github.com/bitcrushtesting/peakemi) by Bitcrush Testing.

Each feature is developed on its own branch off upstream `main`, so it can be offered upstream
on its own; the `integration` branch merges them all and is what the versions below are tagged
on. Versions are `upstream-version` + `-fork.N`: the number says which PeakEmi release the
fork's work follows, the suffix counts the fork's releases on top of it.

## [0.4.0-fork.1] – 2026-11-28

Follows upstream PeakEmi 0.3.1: everything in it is included.

### Added

- **Rohde & Schwarz FSL** (`feature/rs-fsl-driver`, draft PR #1). A driver for the FSL3,
  FSL6 and FSL18 in spectrum-analyzer mode: the model's frequency range, the CISPR
  bandwidths on the EMI filters, the quasi-peak detector, and the options read from `*OPT?`
  (B7 for 200 Hz, B22 preamplifier). Written from the manual and not yet verified on an
  instrument. See [docs/instruments/rs-fsl.md](docs/instruments/rs-fsl.md).
- **Instrument look, light and dark** (`feature/instrument-theme`). A theme of PeakEmi's
  own that follows Windows between light and dark; the spectrum drawn like an analyzer
  display, with a settings bar, start/span/stop, boxed markers and legend; and later the
  EMI-receiver view: logarithmic frequency axis, a margin band under the limit, and the
  verdict on the plot.
- **Test campaigns** (`feature/test-campaigns`). A lab-style test plan for one equipment
  under test, started from CISPR 32 / EN 55032 or FCC Part 15 B class A or B: each test with
  its measurement, an ambient scan, a measured verdict that can only be overridden with a
  reason, comments, equipment with calibration due dates, set-up photos and signatures; a
  campaign signed off and locked, and one PDF report. See
  [docs/campaigns-design.md](docs/campaigns-design.md).
- **Test set-ups** (`feature/test-setups`). A bench documented as its devices from EUT to
  analyzer, each with model, serial number, calibration date, notes and correction, kept in a
  library folder that can be shared, printed as a one-page sheet, and applied to a campaign
  test or a session.
- **Set-up diagrams** (`feature/setup-diagram`). A set-up has a measurement method -- AC or
  DC LISN, RF current probe, antenna, TEM cell, near-field probe or a custom chain -- and is
  drawn and configured as that method's block diagram, each block switched on or off. The
  diagram is on the set-up sheet and in the session report; **File ▸ New session** asks which
  bench the session is on and keeps a copy of it. See [docs/test-setups.md](docs/test-setups.md).
- **Analysis on the bench** (`feature/analysis-tools`). Reference traces from sessions or
  CSV, overlaid in their own colours; trace math (worst case, minimum, average, difference);
  two markers and their delta; re-measuring selected peaks without a new scan; and an editor
  with a live preview for limit lines and correction tables. See
  [docs/analysis-tools.md](docs/analysis-tools.md).
- **One-command Windows build** (`feature/windows-build-script`).
  `scripts\windows\build.ps1` finds the compiler, Qt, CMake and Ninja, builds, deploys the Qt
  libraries and starts PeakEmi.
- Integration only: a re-measured peak is filed under the selected campaign test, a locked
  campaign refuses re-measuring, and the set-up library edits a device's correction table in
  the new editor.

### Fixed

- Images in the campaign report and the set-up sheet no longer run off the right edge of the
  page.
- The session report keeps a section heading on the same page as its image.
- Peak labels and markers are no longer hidden behind the plot legend when references are
  shown.
