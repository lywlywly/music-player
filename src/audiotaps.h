#ifndef AUDIOTAPS_H
#define AUDIOTAPS_H

#include <gst/gst.h>

class PcmTap;
class QAudioBuffer;

// Push one decoded buffer of a playback backend into a PcmTap (see
// docs/spectrum.md). Buffers in a format the tap does not take are skipped.

// A buffer passing pad: the format comes from the pad's current caps
// (interleaved only), the stream time from its PTS and the pad's segment.
void pushGstBuffer(GstPad *pad, GstBuffer *buffer, PcmTap &tap);
// A QAudioBufferOutput buffer; its startTime() is the stream time.
void pushAudioBuffer(const QAudioBuffer &buffer, PcmTap &tap);

#endif // AUDIOTAPS_H
