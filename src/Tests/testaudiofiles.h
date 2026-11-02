#ifndef TESTAUDIOFILES_H
#define TESTAUDIOFILES_H

#include <QString>

// Writes a short silent 8 kHz mono 8-bit PCM WAV file that TagLib can open.
bool writeSilentWav(const QString &path);

#endif // TESTAUDIOFILES_H
