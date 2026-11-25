# The Spectrum plugin

## Summary

* A **Spectrum** panel, from the Spectrum plugin (`src/plugins/spectrum/`),
  one of the info panels, inserted into the layout from View > Edit Layout
  (see [info-panel.md](../info-panel.md) and
  [layout-editing.md](../layout-editing.md)).
* It shows the spectrum of the playing song as 10, 20 (the default), 40 or 80
  bars: the octave, half-, quarter- and eighth-octave bands of IEC 61260-1
  over the ten octaves from 22.4 Hz to 22.4 kHz, on a 0 dB to -60 dB scale,
  with peak marks. The panel's context menu picks the count.
* The audio comes from the host's `IAudioHost`: the playing song's decoded
  samples at the play position ([plugin-system.md](plugin-system.md),
  "Audio for plugins"). The analysis and the widget are the plugin's.
* No new packages: our own FFT.
* **Backends.** The GStreamer backend feeds the audio everywhere. The Qt
  (QMediaPlayer) backend feeds it only where Qt has its FFmpeg media
  backend: `QAudioBufferOutput` is implemented only there. Homebrew's Qt on
  macOS ships only the `darwin` (AVFoundation) backend, whose
  `setAudioBufferOutput` is the base class's no-op, so no buffer arrives.
  When no audio arrives while playing, the panel says so and points to
  Settings > Playback > Playback backend.

## Look

```
                                           0dB
 ----------------------------------------- -10dB
   _                                       -20dB
  | | _    _                               -30dB
  | || |  | |_   _                         -40dB
  | || |_ | | | | |_                       -50dB
  |_||_|_||_|_|_|_|_|______________________ -60dB
  27 38 53 ... 1.2K ... 9.4K 13K 19K
```

* One bar per band, 20 by default (27, 38, 53, 75, 106, ... 6.7K, 9.4K, 13K,
  19K); the bands are described in "Bands". Labels below the
  bars: a center on an ISO 266 preferred frequency shows its preferred value
  (with 10 bands: 31.5, 63, 125, 250, 500, 1K, 2K, 4K, 8K, 16K); others show
  whole hertz under 1 kHz, one decimal with `K` under 10 kHz (`1.2K`), whole
  `K` above. When the bars are too narrow for every label, every second (or
  third, ...) one is drawn. Bars are 2 px apart, 1 px when a bar's step is
  under 8 px, so many narrow bars keep their width.
* **Band count.** Right-click the panel: "10 Bands", "20 Bands", "40 Bands",
  "80 Bands", the current one checked. A fixed set rather than a free number:
  each doubling halves the band width, which is the visible difference, and
  a menu needs no settings page. The choice is saved in the `spectrum/bands`
  setting (an unknown saved value reads as 20). Changing it drops the bars
  and peaks to the floor; while playing, the next frame raises them.
* Scale: 0 dB at the top, -60 dB at the bottom; a grid line and a label on
  the right every 10 dB. 0 dB is a full-scale sine.
* Each bar has a vertical gradient (palette highlight to a mix of highlight
  and text). A thin line above it marks its recent peak.
* Bars rise at once and fall at 40 dB/s; a peak holds 1 s, then falls at
  20 dB/s. Paused: the bars freeze. Stopped: they fall to the bottom.
* Colors come from the widget palette, so the panel follows the light and
  dark themes.

## Bands

The bands are the fractional-octave bands of IEC 61260-1:2014, with
b = count / 10 bands per octave:

* Base 10, the standard's choice: the octave ratio is G = 10^0.3 (1.9953),
  the reference f_r = 1 kHz.
* Centers `f_r G^(x/b)` for odd b, `f_r G^((2x+1)/(2b))` for even b; edges
  `center * G^(±1/(2b))`. The half-step for even b makes b bands fill one
  octave band exactly.
* Ten octaves, from 5.5 below 1 kHz to 4.5 above: edges at
  `1000 * G^(j/b - 5.5)` for j = 0 ... count, 22.4 Hz to 22.4 kHz: the
  audible range, ending just above 44.1 kHz's Nyquist (22.05 kHz). 10, 20, 40
  and 80 bands are then the octave, half-, quarter- and eighth-octave bands.
* Labels: ISO 266's preferred frequencies are the third-octave centers
  `f_r 10^(n/10)`, named by the R10 series (1, 1.25, 1.6, 2, 2.5, 3.15, 4, 5,
  6.3, 8); the octave centers are among them. The other counts' centers fall
  between them and are rounded as above, not by the standard's rule for
  nominal half- to 24th-octave values (Annex E, seen only in python-acoustics'
  implementation), which can differ in the last digit.

What is standard and what is not:

* Standard: the band centers and edges, and the 10-band labels.
* Not standard: the levels. IEC 61260-1 defines a band as a filter, with
  attenuation limits and tolerances (classes 1 and 2). A band here sums the
  FFT bins between its edges, as a brick-wall filter at the FFT's
  resolution. That is a common way to show octave bands and reads close to a
  filter on music, but it is not a conforming filter; it is furthest off in
  bands holding one bin or none (see the cost below). A conforming filter
  bank would buy measurement accuracy a music player does not need.
* Not standard: the labels of 20, 40 and 80 bands (above), and the range:
  the standard allows any run of bands; ten octaves from 22.4 Hz is our
  choice.

Why the standard: every count covers the same range, and the bands of one
count split those of the next coarser one, so changing the count splits or
merges bars instead of moving them; the octave bars are the ones on graphic
equalizers. Rejected: bands log-spaced to fit fixed outer centers (such as
50 Hz to 20 kHz), whose counts do not line up; and foobar2000's spacing, a
fixed ratio per count from 50 Hz, whose top band moves with the count (14 kHz
with 10 bands, 25 kHz with 80) and which follows no standard.

The cost: the 4096-point FFT has 10.8 Hz bins at 44.1 kHz, so bands narrower
than a bin hold one bin or none (and then take their nearest one), and
neighbors can show the same level: the lowest band with 20 bands, those below
about 60 Hz with 40, and below about 125 Hz with 80. A larger FFT would
resolve them but respond more slowly (8192 samples is 186 ms) at every count,
so it was not taken. With 80 bands the top band (20.5 to 22.4 kHz) is partly
above Nyquist at 44.1 kHz and reads low.

Sources. The standards' texts are paid and were not read; the formulas come
from:

* python-acoustics, an open-source implementation of IEC 61260-1:2014 that
  cites its equation numbers (centers, edges, G, f_r, the nominal values):
  <https://python-acoustics.github.io/python-acoustics/_modules/acoustics/standards/iec_61260_1_2014.html>
* The ISO 266:1997 sample, for the preferred frequencies (31.5, 63, 125 Hz
  against their exact 31.623, 63.096, 125.89 Hz):
  <https://cdn.standards.iteh.ai/samples/1350/dee3c3da72b444efae95d47dbe35d8fb/ISO-266-1997.pdf>


## The plugin: `src/plugins/spectrum/`

The panel is a plugin ([plugin-system.md](plugin-system.md)), id `spectrum`,
name "Spectrum": it sees playback only through listener events and the decoded
audio through the host's `IAudioHost` ("Audio for plugins" there), so it needs
nothing else from the app. As a plugin it can be disabled, `MainWindow` has no
wiring for it, and other visualization plugins read the same audio. Rejected:
keeping it built in, the one panel `MainWindow` would feed playback state,
positions and a tap by hand, while plugins could not show audio at all. Cost:
without the plugin (disabled, or missing) there is no spectrum.

| File | Qt | Role |
|---|---|---|
| `fft.{h,cpp}` | no | `fft`: in-place radix-2 FFT on `std::complex<float>`. |
| `spectrumbands.{h,cpp}` | no | `SpectrumBands::fractionalOctave(count)`: band centers and edges (see "Bands"); `frequencyLabel(hz)`. |
| `spectrumanalyzer.{h,cpp}` | no | `SpectrumAnalyzer`: samples to band levels (dB). |
| `spectrumsmoother.{h,cpp}` | no | `SpectrumSmoother`: shown levels and peaks over time. Time is passed in. |
| `spectrumwidget.{h,cpp}` | Widgets | `SpectrumWidget`: paints bars, peaks, grid, labels and a message; the band count menu. |
| `spectrumpresenter.{h,cpp}` | Core | `SpectrumPresenter`: drives the widget from the host's audio at about 30 fps. |
| `spectrumplugin.{h,cpp,json}` | Core | `SpectrumPlugin`: the root object (`IPlugin`, `IPanelProvider`). |

Two targets, as the other plugins: `myspectrum` (static, everything but the
root object; the tests link it) and `spectrumplugin` (the module). The files
marked "no" use no Qt.

### `SpectrumAnalyzer`

For `fftSize = 4096` samples (93 ms at 44.1 kHz, 10.8 Hz per bin):

1. Multiply by a Hann window (`0.5 - 0.5 cos(2 pi n / (N - 1))`, computed
   once), so a tone does not leak into every bin.
2. FFT (radix-2 Cooley-Tukey, iterative, in place).
3. Power of bin k: `|X[k]|^2 * 4 / (N * sum(w^2))`. By Parseval, the bins of
   a sine of amplitude A then add up to `A^2`, wherever the sine falls
   between bins, so a full-scale sine reads 0 dB.
4. Each band sums the power of the bins between its edges. A band with no
   bin inside (the lowest ones, see "Bands"; more at 192 kHz, 47 Hz per bin)
   takes the bin nearest its center. A band whose center is above Nyquist
   reads the floor.
5. `10 log10(power)`, clamped to [-60, 0] dB.

A library FFT (KissFFT, FFTW, vDSP) is not needed at this size (0.1 ms per
frame); FFTW is GPL. The FFT sits behind `SpectrumAnalyzer`, so it can be
swapped later.

### `SpectrumSmoother`

* `update(targetsDb, nowSeconds)`: a level rises to its target at once and
  falls toward it at 40 dB/s. A peak follows a level above it, holds 1 s,
  then falls at 20 dB/s, never below its level.
* `isAtFloor()`: every level and peak at -60 dB; `reset()`.

### `SpectrumWidget`

* `setBandLabels(labels, floorDb)`, `setLevels(levels, peaks)`,
  `setMessage(text)` (drawn over the bars when not empty), and their
  getters for tests.
* `kBandCounts` (10, 20, 40, 80): the counts its context menu offers. A pick
  emits `bandCountRequested(count)`; the widget changes nothing itself, and
  checks the count matching its number of labels.
* Emits `shownChanged(bool)` from `showEvent` / `hideEvent`, so it tells
  whether the panel can be seen: it is hidden while out of the layout, and
  on a tab that is not in front (a `QTabWidget` shows only its current
  page).

### `SpectrumPresenter`

* Takes the widget, the host's `IPlayerHost` and `IAudioHost`. Owns the
  analyzer, the smoother and a 33 ms timer.
* An `IPlayerListener` for its lifetime (constructor, destructor):
  `trackStarted` and `resumed` mean playing, `paused` paused, `stopped`
  stopped.
* Active while the widget is shown: `IAudioHost::setWanted(true)` on
  becoming active, `setWanted(false)` on becoming hidden, when it also
  drops the bars to the floor. So no audio is copied while the panel is
  hidden, and the host forgets the audio, so the panel starts clean when
  shown again. Destroyed while still shown (the widget goes without a hide),
  it calls `setWanted(false)` too.
* `setBandCount(count)`, connected to the widget's `bandCountRequested`:
  saves `spectrum/bands`, replaces the analyzer and the smoother, and sends
  the new labels. The constructor reads the saved count.
* Each frame reads the 4096 samples being heard (`IAudioHost::read`, at the
  host's play position estimate) and analyzes them. When the reports of the
  position stop, the estimate passes the newest audio, `read` returns 0,
  and the bars fall.
* The timer runs while active and playing, or while stopped until the bars
  reach the floor. Paused, it stops, so the bars freeze.
* The no-audio message: shown when no audio was read for 1.5 s while
  playing (shorter gaps, after a seek or past the end of a song, are
  normal); it names Settings > Playback > Playback backend. Audio, stop or
  hiding the panel clears it. The plugin names an app page in text only, so
  the text must follow if that page moves.

### `SpectrumPlugin`

* `createPanel` creates the widget (object name `spectrumWidget`, minimum
  size 120 x 240) and its presenter, a child of the widget, so the panel and
  its presenter go together with the window's UI, before the plugin shuts
  down.
* The panel is a layout element named `spectrum`, outside the default
  layout ([layout-editing.md](../layout-editing.md)). Outside the layout it
  waits hidden, so it wants no audio. Its settings key, `spectrum/bands`,
  follows the plugins' `<id>/` rule.

## Edge cases

* **Backend switch, seek, new song**: the tap follows
  ([playback.md](../playback.md), "Audio tap"); just after a seek no audio
  is read for up to 100 ms, and the bars dip.
* **End of song**: the position passes the last decoded sample, `read`
  returns 0 and the bars fall.
* **Mono, 8-bit or 8 kHz files**: converted by the host; bands above Nyquist
  read the floor.
* **Volume 0**: the bars still move (the tap is before the volume).

## Out of scope

Band counts other than the four, range or speed settings; separate left and
right channels; an oscilloscope; a detachable window; a decoder of our own for
backends that send no buffers.

## Tests

* `tst_spectrum` (no GUI): the FFT against a DFT, the bands and labels, the
  analyzer's levels (full scale reads 0 dB, bins narrower than a band,
  above Nyquist), the smoother.
* `tst_spectrumpresenter` (offscreen, test player and audio hosts): audio
  wanted only while shown (and not after the presenter goes), the tone in
  its band, pause, stop, the no-audio message, and the context menu
  changing and saving the band count.
* `tst_mainwindow`: the panel from the plugin in the store by default, in a
  saved layout, "not available" when disabled, and the player's tap set
  only while its tab is in front.
* Check by hand: a high-rate file (96 kHz 24-bit FLAC) shows on the
  GStreamer backend; on the Qt backend without Qt's FFmpeg media backend, the
  panel shows the no-audio message; disabling the plugin and enabling it
  again keeps the panel's place.
