#ifndef SPECTRUM_FFT_H
#define SPECTRUM_FFT_H

#include <complex>
#include <vector>

// In-place forward FFT (iterative radix-2 Cooley-Tukey, no scaling). The size
// must be a power of two.
void fft(std::vector<std::complex<float>> &data);

#endif // SPECTRUM_FFT_H
