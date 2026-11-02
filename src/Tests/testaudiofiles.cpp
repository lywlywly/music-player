#include "testaudiofiles.h"

#include <QDataStream>
#include <QFile>

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

QByteArray id3v2CoverTag(const QByteArray &imageData,
                         const QByteArray &mimeType) {
  QByteArray body;
  body.append('\0'); // text encoding: ISO-8859-1
  body.append(mimeType).append('\0');
  body.append('\x03'); // picture type: front cover
  body.append('\0');   // empty description
  body.append(imageData);

  QByteArray frame("APIC");
  frame.append(bigEndian32(static_cast<quint32>(body.size())));
  frame.append(2, '\0'); // frame flags
  frame.append(body);

  QByteArray tag("ID3");
  tag.append('\x03').append('\0'); // version 2.3.0
  tag.append('\0');                // tag flags
  tag.append(syncSafe32(static_cast<quint32>(frame.size())));
  tag.append(frame);
  return tag;
}

// Writes the silent WAV, followed by extraChunk (a complete RIFF chunk) if
// it is not empty.
bool writeWav(const QString &path, const QByteArray &extraChunk) {
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly)) {
    return false;
  }

  constexpr quint32 sampleRate = 8000;
  constexpr quint16 channels = 1;
  constexpr quint16 bitsPerSample = 8;
  const QByteArray pcm(800, static_cast<char>(0x80));
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
} // namespace

bool writeSilentWav(const QString &path) { return writeWav(path, {}); }

bool writeWavWithCover(const QString &path, const QByteArray &imageData,
                       const QByteArray &mimeType) {
  QByteArray tag = id3v2CoverTag(imageData, mimeType);
  QByteArray chunk("id3 ");
  QDataStream out(&chunk, QIODevice::Append);
  out.setByteOrder(QDataStream::LittleEndian);
  out << static_cast<quint32>(tag.size());
  chunk.append(tag);
  if (tag.size() % 2 != 0) {
    chunk.append('\0'); // RIFF chunks are padded to an even size
  }
  return writeWav(path, chunk);
}
