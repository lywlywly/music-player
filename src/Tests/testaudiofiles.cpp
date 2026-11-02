#include "testaudiofiles.h"

#include <QByteArray>
#include <QDataStream>
#include <QFile>

bool writeSilentWav(const QString &path) {
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
  const quint32 chunkSize = 36 + dataSize;

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

  return out.status() == QDataStream::Ok;
}
