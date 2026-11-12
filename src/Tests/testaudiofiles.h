#ifndef TESTAUDIOFILES_H
#define TESTAUDIOFILES_H

#include <QByteArray>
#include <QString>

// Writes a short silent 8 kHz mono 8-bit PCM WAV file that TagLib can open.
bool writeSilentWav(const QString &path);
// Writes a stereo 24-bit 44.1 kHz PCM WAV of a sine of hz at half scale
// (-6 dB), seconds long.
bool writeToneWav(const QString &path, double hz, double seconds);
// Like writeSilentWav, with an ID3v2.3 tag in an "id3 " chunk holding one
// front-cover APIC frame with imageData (e.g. PNG bytes) of mimeType.
bool writeWavWithCover(const QString &path, const QByteArray &imageData,
                       const QByteArray &mimeType);

#endif // TESTAUDIOFILES_H
