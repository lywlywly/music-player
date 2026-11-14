# Plan: Spectrum panel

Status: implemented.

## Summary

* A third tab, **Spectrum**, in the info panel at the top left (next to
  Lyrics and Properties; see [info-panel.md](info-panel.md)).
* It shows the spectrum of the playing song as 20 bars, log-spaced from 50 Hz
  to 20 kHz, on a 0 dB to -60 dB scale, with peak marks.
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
  When no audio arrives while playing, the tab says so and points to
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
  50 69 94 ... 1.2K ... 11K 15K 20K
```

* 20 bars, one per band. Band centers are log-spaced from 50 Hz to 20 kHz
  (50, 69, 94, 129, 177, ... 7.8K, 11K, 15K, 20K). Labels below the bars:
  whole hertz under 1 kHz, one decimal with `K` under 10 kHz (`1.2K`), whole
  `K` above. When the bars are too narrow for every label, every second (or
  third) one is drawn.
* Scale: 0 dB at the top, -60 dB at the bottom; a grid line and a label on
  the right every 10 dB. 0 dB is a full-scale sine.
* Each bar has a vertical gradient (palette highlight to a mix of highlight
  and text). A thin line above it marks its recent peak.
* Bars rise at once and fall at 40 dB/s; a peak holds 1 s, then falls at
  20 dB/s. Paused: the bars freeze. Stopped: they fall to the bottom.
* Colors come from the widget palette, so the panel follows the light and
  dark themes.

## Module: `src/spectrum/`

| File | Qt | Role |
|---|---|---|
| `fft.{h,cpp}` | no | `fft`: in-place radix-2 FFT on `std::complex<float>`. |
| `spectrumbands.{h,cpp}` | no | `SpectrumBands::logSpaced`: band centers and edges; `frequencyLabel(hz)`. |
| `spectrumanalyzer.{h,cpp}` | no | `SpectrumAnalyzer`: samples to band levels (dB). |
| `spectrumsmoother.{h,cpp}` | no | `SpectrumSmoother`: shown levels and peaks over time. Time is passed in. |
| `pcmtap.{h,cpp}` | no | `PcmTap`: thread-safe store of the last 2 s of decoded audio, mixed to mono, with stream times. |
| `spectrumwidget.{h,cpp}` | Widgets | `SpectrumWidget`: paints bars, peaks, grid, labels and a message. |
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
4. Each band sums the power of the bins between its edges (an edge is the
   geometric mean of the centers around it). A band with no bin inside
   takes the bin nearest its center: at 44.1 kHz every band holds a bin, but
   at 192 kHz (47 Hz per bin) the lowest ones do not. A band whose center is
   above Nyquist reads the floor.
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
* Emits `shownChanged(bool)` from `showEvent` / `hideEvent`. In a
  `QTabWidget` only the current page is shown, so this tells whether the
  Spectrum tab is in front.

### `SpectrumPresenter`

* Owns the `PcmTap` (a `std::shared_ptr`), the analyzer, the smoother and a
  33 ms timer.
* Active while the widget is shown. It emits `tapChanged(tap)` on becoming
  active and `tapChanged(nullptr)` on becoming hidden, when it also clears
  the tap and the bars. `MainWindow` connects it to
  `PlaybackController::setPcmTap`, so no audio is copied while the tab is
  hidden.
* Inputs: `setPosition(ms)` and `setState(Stopped | Paused | Playing)`.
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
  normal). Audio, stop or hiding the tab clears it. The default says "No
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

* A third page `spectrumTab` with `spectrumWidget` (`SpectrumWidget`,
  promoted, header `spectrum/spectrumwidget.h`).
* `infopanel/current_tab` gains `spectrum`.
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

Band count, range or speed settings; separate left and right channels; an
oscilloscope; a detachable window; a decoder of our own for backends that
send no buffers.

## Tests

* `tst_spectrum` (no GUI): FFT against a DFT; band centers, edges and
  labels; a full-scale sine at a band center reads about 0 dB in its band,
  neighbors below -40 dB; half amplitude about -6 dB; silence the floor; a
  band narrower than a bin (192 kHz); bands above Nyquist;
  smoother rise, fall, hold and floor; `PcmTap` downmix and formats, reads
  with zero fill, new runs on a gap or a rate change, dropping old audio.
* `tst_spectrumpresenter` (offscreen): the tap is offered only while the
  widget is shown and cleared when hidden; a tone in the tap reaches the
  widget while playing, from position 0 with no further position report (the
  estimate moves on); pause freezes; stop falls to the floor; the no-audio
  message appears after the delay and goes with audio or stop.
* `tst_audiotaps`: for each format the `volume` element can output, an
  `audiotestsrc` pipeline with the probe fills a tap with the right rate and
  stream times (the tone reads in its band at about 0 dB); big-endian and
  non-interleaved audio is skipped; a `QAudioBuffer` does the same, and one
  without a start time is skipped.
* `tst_playbackcontroller`: the tap moves to the new player on a backend
  switch.
* `tst_mainwindow`: the Spectrum tab exists and is restored; the player has
  the tap only while the Spectrum widget is shown.
* `tst_gstaudioplayer`: a 24-bit WAV tone played through the real
  `GstAudioPlayer` at volume 0 fills the tap at the reported position, in
  its band at the expected level; after a seek, at the new position; stop
  clears it. It opens the default audio output, so it is skipped on a
  machine without one.
* Check by hand: a high-rate file (96 kHz 24-bit FLAC) shows on the
  GStreamer backend; on the Qt backend without Qt's FFmpeg media backend, the
  tab shows the no-audio message.
