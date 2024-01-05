#ifndef FILESYSTEMMONITOR_H
#define FILESYSTEMMONITOR_H

#include <map>
#include <vector>

#include "checksumcalculator.h"

using StateDict = std::map<std::string, std::string>;
using FilePathList = std::vector<std::string>;
class FileSystemMonitor {
 public:
  FileSystemMonitor();

  StateDict getDirectoryState(std::string);
  std::tuple<FilePathList, FilePathList, FilePathList> compareTwoStates(
      StateDict, StateDict);

 private:
  ChecksumCalculator checksumCalc;
};

#endif  // FILESYSTEMMONITOR_H
