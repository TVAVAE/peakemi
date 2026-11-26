# Test set-ups

A test set-up documents a bench as it is built: the devices from the equipment under test
to the analyzer, each with its model, serial number, calibration due date, notes and, where
it changes the reading, its correction. Saved set-ups make a bench reusable from one test,
campaign or session to the next.

## Where they are kept

**Run ▸ Test set-ups…** opens the library. Each set-up is one `.peakemi-setup.json` file in
the library folder, which is PeakEmi's own data folder unless you choose another with
**Change folder…**. Point it at a shared drive and the whole lab works from one list of
benches. **Export…** and **Import…** move single set-ups between libraries; an import never
replaces a set-up that is already there.

## What a set-up holds

| Field | Purpose |
|---|---|
| Name, type, description | Conducted, radiated or other; the description is the layout, distances and grounding someone rebuilding the bench needs |
| Signal chain | Devices in signal order, EUT first and analyzer last |
| Calibration due | `YYYY-MM-DD`; a lapsed date is shown in red in the library, the set-up sheet and the campaign report |
| Correction | A fixed value (attenuator, transient limiter, preamplifier gain, cable loss) or a table imported from CSV or JSON (LISN factor, antenna factor, cable loss over frequency) |

A correction belongs to its device, so listing a 10 dB limiter also corrects for it. Gains
are entered as positive numbers and subtracted from the reading. The library shows the
chain's total correction across the band, and **Print sheet…** writes a one-page PDF to
keep with the bench.

## Applying a set-up

- **To a campaign test**: select the test, then **Apply set-up…** in its Equipment tab, or
  **Apply** in the library. The test's equipment list and corrections are replaced by the
  set-up's, and the campaign report names the set-up and lists the corrections each test was
  measured with. The test keeps its own copy: changing or deleting the set-up later does not
  change a test already filed.
- **To a plain session**: with no campaign test selected, **Apply** puts the set-up's
  corrections in the run configuration and its name in the session's test set-up field,
  which the session report prints.

**Save as set-up…** in a test's Equipment tab turns what the test already holds into a new
set-up: each correction is attached to the device whose role matches it (an antenna factor
to the antenna, a cable loss to the cable), and a correction no device claims becomes a
device of its own.
