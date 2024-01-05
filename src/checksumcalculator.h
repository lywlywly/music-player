#ifndef CHECKSUMCALCULATOR_H
#define CHECKSUMCALCULATOR_H

#include <string>
class ChecksumCalculator {
 public:
  ChecksumCalculator();
  std::string calculateSHA1(const std::string &, const int size = 8192);
  std::string calculateHeaderSHA1(const std::string &, int nums = 10);
};

#endif  // CHECKSUMCALCULATOR_H
