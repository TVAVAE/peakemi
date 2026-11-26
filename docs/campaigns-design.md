# Test campaigns

Lab-style test workflow: a campaign is an ordered test plan for one EUT, with a verdict,
comments and sign-off per test, and one campaign report. Status: **built** (see "Using it").

## Using it

1. **Campaign ▸ New campaign** and pick a standard. The four tests are filled in and the
   campaign details dialog opens for the equipment under test.
2. Select a test in the **Test plan** tab. Its configuration is loaded into the run
   configuration, and edits there belong to that test (add the LISN or antenna
   corrections here).
3. **Ambient scan** (EUT off) records a Phase 1 scan that is drawn in grey under the
   measurement. **Run test**, or the toolbar's Start run, measures the test and files the
   session under it.
4. In the **Result** tab, the measured verdict can be overridden only with a reason;
   add comments and sign as tester. **Equipment** and **Photos** hold the setup.
5. **Campaign ▸ Sign-off and lock** records tester and reviewer; once every test has a
   verdict the campaign can be locked as a closed record.
6. **Report…** writes the campaign PDF. Campaigns are saved as `.peakemi-campaign.json`.

## Decisions (agreed 2026-11-25)

* **Templates.** A new campaign starts from a standard: CISPR 32 / EN 55032 class A or B,
  FCC Part 15B class A or B. Each pre-fills conducted emissions on L1 and N (150 kHz to
  30 MHz) and radiated emissions horizontal and vertical (30 MHz to 1 GHz), using the
  built-in limit lines in `LimitCatalogue.cpp`. Tests can be added, removed and renamed.
* **Verdict.** It is measured from the test's session (`Session::overallVerdict`). The
  operator may override it (for example for an ambient signal) only with a mandatory
  reason. The report shows both the measured and the final verdict.
* **Report.** One campaign PDF: EUT and setup, a summary table of every test (measured and
  final verdict, worst margin, comments), sign-off, then a section per test with its
  spectrum, verified points, equipment, photos and comments. Always ends with
  `ComplianceDisclaimer`.
* **Extras in v1:**
  * equipment per test (role, model, serial, calibration due date; expired flagged);
  * sign-off: "Tested by" and "Reviewed by" with dates, then the campaign can be locked;
  * setup photos per test, embedded in the campaign file (scaled JPEG, base64);
  * ambient scan per test (EUT off): a Phase 1 trace drawn under the EUT trace.

## Planned structure

* `core`: `Campaign.h` (Campaign, CampaignTest, EquipmentItem, SetupPhoto, Signoff,
  TestVerdict), `CampaignTemplates`, `CampaignSerializer` (schema
  `peakemi.campaign`, sessions embedded through `SessionSerializer`).
* `reporting`: `CampaignReportRenderer`, using QTextDocument HTML on a QPdfWriter, with the
  spectrum image supplied by the UI (as `PdfReportRenderer` does).
* `ui`: `CampaignDock` (test list with verdict pills; details tabs for result and comments,
  equipment and photos), dialogs for campaign details and sign-off. MainWindow attaches a
  finished run to the active test, or stores it as the ambient trace.
* `tests/unit`: templates, verdict and override rules, lock rules, calibration expiry,
  JSON round trip, and report generation.

Built on `feature/test-campaigns` from `main`, then merged into `integration`.
