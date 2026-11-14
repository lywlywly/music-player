#include "testaudiofiles.h"

#include <QDataStream>
#include <QFile>
#include <cmath>
#include <numbers>

namespace {
// Big-endian 32-bit integer, as ID3v2.3 frame sizes are stored.
QByteArray bigEndian32(quint32 value) {
  QByteArray bytes;
  QDataStream out(&bytes, QIODevice::WriteOnly);
  out.setByteOrder(QDataStream::BigEndian);
  out << value;
  return bytes;
}

// ID3v2 tag size: four bytes of seven bits each.
QByteArray syncSafe32(quint32 value) {
  QByteArray bytes(4, '\0');
  for (int i = 3; i >= 0; --i) {
    bytes[i] = static_cast<char>(value & 0x7F);
    value >>= 7;
  }
  return bytes;
}

QByteArray id3v2Frame(const QByteArray &id, const QByteArray &body) {
  QByteArray frame(id);
  frame.append(bigEndian32(static_cast<quint32>(body.size())));
  frame.append(2, '\0'); // frame flags
  frame.append(body);
  return frame;
}

// An ID3v2.3 tag of frames.
QByteArray id3v2Tag(const QByteArray &frames) {
  QByteArray tag("ID3");
  tag.append('\x03').append('\0'); // version 2.3.0
  tag.append('\0');                // tag flags
  tag.append(syncSafe32(static_cast<quint32>(frames.size())));
  tag.append(frames);
  return tag;
}

// A WAV "id3 " chunk holding an ID3v2.3 tag of frames.
QByteArray id3Chunk(const QByteArray &frames) {
  const QByteArray tag = id3v2Tag(frames);
  QByteArray chunk("id3 ");
  QDataStream out(&chunk, QIODevice::Append);
  out.setByteOrder(QDataStream::LittleEndian);
  out << static_cast<quint32>(tag.size());
  chunk.append(tag);
  if (tag.size() % 2 != 0) {
    chunk.append('\0'); // RIFF chunks are padded to an even size
  }
  return chunk;
}

QByteArray coverFrame(const QByteArray &imageData, const QByteArray &mimeType) {
  QByteArray body;
  body.append('\0'); // text encoding: ISO-8859-1
  body.append(mimeType).append('\0');
  body.append('\x03'); // picture type: front cover
  body.append('\0');   // empty description
  body.append(imageData);
  return id3v2Frame("APIC", body);
}

// A user text frame, as ReplayGain taggers write REPLAYGAIN_TRACK_GAIN.
QByteArray userTextFrame(const QByteArray &description,
                         const QByteArray &value) {
  QByteArray body;
  body.append('\0'); // text encoding: ISO-8859-1
  body.append(description).append('\0');
  body.append(value);
  return id3v2Frame("TXXX", body);
}

QByteArray gainText(double db) {
  return QByteArray::number(db, 'f', 2) + " dB";
}

// Writes a PCM WAV of pcm, followed by extraChunk (a complete RIFF chunk) if
// it is not empty, and preceded by leadingTag (an ID3v2 tag before the RIFF
// header) if it is not empty.
bool writeWav(const QString &path, quint32 sampleRate, quint16 channels,
              quint16 bitsPerSample, const QByteArray &pcm,
              const QByteArray &extraChunk = {},
              const QByteArray &leadingTag = {}) {
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly)) {
    return false;
  }
  file.write(leadingTag);

  const quint32 dataSize = static_cast<quint32>(pcm.size());
  const quint32 byteRate = sampleRate * channels * (bitsPerSample / 8);
  const quint16 blockAlign = channels * (bitsPerSample / 8);
  const quint32 chunkSize =
      36 + dataSize + static_cast<quint32>(extraChunk.size());

  QDataStream out(&file);
  out.setByteOrder(QDataStream::LittleEndian);

  file.write("RIFF", 4);
  out << chunkSize;
  file.write("WAVE", 4);
  file.write("fmt ", 4);
  out << quint32(16);
  out << quint16(1);
  out << channels;
  out << sampleRate;
  out << byteRate;
  out << blockAlign;
  out << bitsPerSample;
  file.write("data", 4);
  out << dataSize;
  file.write(pcm);
  file.write(extraChunk);

  return out.status() == QDataStream::Ok;
}

// 0.1 s of 8 kHz mono 8-bit silence.
const QByteArray kSilence(800, static_cast<char>(0x80));

// The tone writeToneWav writes, preceded by leadingTag (see writeWav).
bool writeToneWavWithTag(const QString &path, double hz, double seconds,
                         const QByteArray &leadingTag) {
  constexpr int rate = 44100;
  const int frames = static_cast<int>(seconds * rate);
  QByteArray pcm;
  pcm.reserve(frames * 2 * 3);
  for (int i = 0; i < frames; ++i) {
    const auto value = static_cast<qint32>(std::lround(
        0.5 * 8388607.0 * std::sin(2.0 * std::numbers::pi * hz * i / rate)));
    for (int channel = 0; channel < 2; ++channel) {
      // Packed little-endian 24-bit.
      pcm.append(static_cast<char>(value & 0xFF));
      pcm.append(static_cast<char>((value >> 8) & 0xFF));
      pcm.append(static_cast<char>((value >> 16) & 0xFF));
    }
  }
  return writeWav(path, rate, 2, 24, pcm, {}, leadingTag);
}
} // namespace

bool writeSilentWav(const QString &path) {
  return writeWav(path, 8000, 1, 8, kSilence);
}

bool writeToneWav(const QString &path, double hz, double seconds) {
  return writeToneWavWithTag(path, hz, seconds, {});
}

bool writeToneWavWithReplayGain(const QString &path, double hz, double seconds,
                                double trackGainDb, double albumGainDb) {
  return writeToneWavWithTag(
      path, hz, seconds,
      id3v2Tag(userTextFrame("REPLAYGAIN_TRACK_GAIN", gainText(trackGainDb)) +
               userTextFrame("REPLAYGAIN_ALBUM_GAIN", gainText(albumGainDb))));
}

bool writeWavWithCover(const QString &path, const QByteArray &imageData,
                       const QByteArray &mimeType) {
  return writeWav(path, 8000, 1, 8, kSilence,
                  id3Chunk(coverFrame(imageData, mimeType)));
}
