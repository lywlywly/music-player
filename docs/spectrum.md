# Spectrum panel

## Summary

* A **Spectrum** panel, one of the info panels, inserted into the layout from
  View > Edit Layout (see [info-panel.md](info-panel.md) and
  [layout-editing.md](layout-editing.md)).
* It shows the spectrum of the playing song as 10, 20 (the default), 40 or 80
  bars: the octave, half-, quarter- and eighth-octave bands of IEC 61260-1
  over the ten octaves from 22.4 Hz to 22.4 kHz, on a 0 dB to -60 dB scale,
  with peak marks. The panel's context menu picks the count.
* The analysis and the widget are a separate module, `src/spectrum/`, built
  as its own static library (`myspectrum`). It depends on Qt only, not on
  any app class. The app depends on it, never the other way round.
* No new packages: our own FFT, Qt Multimedia and GStreamer core, which the
  app already links.
* **Backends.** The GStreamer backend feeds the spectrum everywhere. The Qt
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

## Module: `src/spectrum/`

| File | Qt | Role |
|---|---|---|
| `fft.{h,cpp}` | no | `fft`: in-place radix-2 FFT on `std::complex<float>`. |
| `spectrumbands.{h,cpp}` | no | `SpectrumBands::fractionalOctave(count)`: band centers and edges (see "Bands"); `frequencyLabel(hz)`. |
| `spectrumanalyzer.{h,cpp}` | no | `SpectrumAnalyzer`: samples to band levels (dB). |
| `spectrumsmoother.{h,cpp}` | no | `SpectrumSmoother`: shown levels and peaks over time. Time is passed in. |
| `pcmtap.{h,cpp}` | no | `PcmTap`: thread-safe store of the last 2 s of decoded audio, mixed to mono, with stream times. |
| `spectrumwidget.{h,cpp}` | Widgets | `SpectrumWidget`: paints bars, peaks, grid, labels and a message; the band count menu. |
| `spectrumpresenter.{h,cpp}` | Core | `SpectrumPresenter`: drives the widget from the tap at about 30 fps. |

`src/spectrum/CMakeLists.txt` builds `myspectrum` (linking `Qt6::Widgets`);
`MYLib` links it. The files marked "no" use no Qt, so they can move to a
Qt-free core later (the roadmap's "Qt-free backend internals").

### `PcmTap`

* `push(data, frames, channels, format, sampleRate, startUs)`: any thread.
  Interleaved little-endian frames in `UInt8`, `Int8`, `Int16`, `Int24`
  (packed, 3 bytes), `Int32`, `Float32` or `Float64`,
  converted to float in [-1, 1] and mixed to mono (the mean of the
  channels). `startUs` is the stream time of the first frame.
* Audio is kept as one run of samples. A push that does not follow on (more
  than 50 ms off, or a different sample rate) starts a new run; this covers
  buffers still in flight after a seek. Samples more than 2 s before the
  newest are dropped.
* `read(endUs, out)`: the `out.size()` samples ending at stream time `endUs`
  (rounded to the nearest sample), with zeros where there is no audio.
  Returns the sample rate, or 0 when no audio overlaps.
* `clear()`; the players call it on a new source, a seek and stop.
* A mutex guards the samples: the GStreamer streaming thread pushes, the GUI
  thread reads.

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

* Owns the `PcmTap` (a `std::shared_ptr`), the analyzer, the smoother and a
  33 ms timer.
* Active while the widget is shown. It emits `tapChanged(tap)` on becoming
  active and `tapChanged(nullptr)` on becoming hidden, when it also clears
  the tap and the bars. `MainWindow` connects it to
  `PlaybackController::setPcmTap`, so no audio is copied while the panel is
  hidden.
* Inputs: `setPosition(ms)` and `setState(Stopped | Paused | Playing)`.
* `setBandCount(count)`, connected to the widget's `bandCountRequested`:
  saves `spectrum/bands`, replaces the analyzer and the smoother, and sends
  the new labels. The constructor reads the saved count.
* Timing: the players decode ahead of what is heard (the GStreamer probe
  sees audio about 300 ms ahead). So each frame analyzes the 4096 samples
  ending at the estimated play position: the last reported position plus
  the time since, while playing. The GStreamer backend reports the position
  only every 100 ms; the estimate smooths that. If the reports stop, the
  estimate passes the newest audio in the tap, and the bars fall.
* The timer runs while active and playing, or while stopped until the bars
  reach the floor. Paused, it stops, so the bars freeze.
* `setNoAudioMessage(text)`: shown when no audio was read for 1.5 s while
  playing (shorter gaps, after a seek or past the end of a song, are
  normal). Audio, stop or hiding the panel clears it. The default says "No
  audio from the playback backend."; `MainWindow` sets one that points to
  the backend setting.

## App changes

### `AudioPlayer`

* `virtual void setPcmTap(std::shared_ptr<PcmTap>)` and `pcmTap()`. The base
  stores the tap behind a mutex, because the GStreamer streaming thread
  reads it. Protected `clearPcmTap()`.
* With no tap, no audio is copied: the GStreamer probe only finds no tap,
  and the Qt player has no buffer output set.

### `GstAudioPlayer`

* A buffer probe (`onPcmProbe`) on the `src` pad of the ReplayGain `volume`
  element, `playbin`'s audio filter ([playback.md](playback.md),
  "ReplayGain"), installed with the pipeline. That is after ReplayGain and
  before the volume slider, which playsink applies later: the bars move at
  volume 0. It is not removed: teardown sets the pipeline to NULL first, and
  the probe goes with the element.
* `setSource`, `setPosition` and `stop` clear the tap.

### `QtAudioPlayer`

* One `QAudioBufferOutput` (no requested format, so buffers come in the
  decoded format), set on the `QMediaPlayer` only while a tap is set.
  Connected with `Qt::DirectConnection`: the push runs in whatever thread
  sends the buffer.
* `setSource`, `setPosition` and `stop` clear the tap.
* With the `darwin` backend no buffer arrives (see Summary).

### `audiotaps.{h,cpp}`

* `pushGstBuffer(pad, buffer, tap)`: reads `format`, `rate`, `channels` and
  `layout` from the pad's current caps with core `GstStructure` calls (no
  `gstreamer-audio-1.0`). It takes every format the `volume` element can
  output: `F32LE`, `F64LE`, `S8`, `S16LE`, `S24LE`, `S32LE`, interleaved
  only (the supported platforms are little-endian); a 24-bit FLAC arrives as
  `S24LE`. The buffer PTS becomes stream time through the pad's sticky segment
  (`gst_segment_to_stream_time`), which is what the position query reports.
* `pushAudioBuffer(buffer, tap)`: maps `QAudioFormat` to the tap's formats;
  `QAudioBuffer::startTime()` is the stream time. Buffers without one are
  skipped.
* Free functions, so they are tested without an audio device.

### `PlaybackController`

* `setPcmTap(tap)`: kept, set on the current player, and set again on the
  new player in `setBackend` (as the volume is).

### `MainWindow`, `mainwindow.ui`

* `spectrumWidget` (`SpectrumWidget`, promoted, header
  `spectrum/spectrumwidget.h`) is the layout's `spectrum` element, outside
  the default layout ([layout-editing.md](layout-editing.md)). Outside the
  layout it waits hidden, so it offers no tap.
* Creates `SpectrumPresenter` and sets its no-audio message; connects
  `tapChanged` to `PlaybackController::setPcmTap`, `songStarted` to
  `Playing`, `playbackStateChanged` to the matching state, and
  `positionChanged` to `setPosition`.

## Edge cases

* **Backend switch**: `PlaybackController` sets the tap on the new player.
* **Seek**: the player clears the tap; late buffers from before the seek
  start a run that the first buffer after it replaces.
* **End of song**: the position passes the last decoded sample, `read`
  returns 0 and the bars fall.
* **Mono, 8-bit or 8 kHz files**: converted; bands above Nyquist read the
  floor.
* **Volume 0**: the bars still move.

## Out of scope

Band counts other than the four, range or speed settings; separate left and
right channels; an oscilloscope; a detachable window; a decoder of our own for
backends that send no buffers.

## Tests

* `tst_spectrum` (no GUI): the FFT against a DFT, the bands and labels, the
  analyzer's levels (full scale reads 0 dB, bins narrower than a band,
  above Nyquist), the smoother, and `PcmTap`.
* `tst_spectrumpresenter` (offscreen): the tap only while shown, the
  position estimate, pause, stop, the no-audio message, and the context
  menu changing and saving the band count.
* `tst_audiotaps`: every format the `volume` element can output through a
  real pipeline and a `QAudioBuffer`, and what is skipped.
* `tst_playbackcontroller`: the tap moving to a new player.
* `tst_mainwindow`: the panel in the default and a saved layout, the tap set
  only while it is shown.
* `tst_gstaudioplayer`: the real `GstAudioPlayer` filling the tap at the
  reported position, after a seek, and cleared on stop (skipped without an
  audio output).
* Check by hand: a high-rate file (96 kHz 24-bit FLAC) shows on the
  GStreamer backend; on the Qt backend without Qt's FFmpeg media backend, the
  panel shows the no-audio message.
