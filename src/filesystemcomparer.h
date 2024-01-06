#ifndef FILESYSTEMCOMPARER_H
#define FILESYSTEMCOMPARER_H

#include <QFileSystemWatcher>
#include <map>
#include <vector>

#include "checksumcalculator.h"

using StateDict = std::map<std::string, std::string>;
using FilePathList = std::vector<std::string>;
class FileSystemComparer {
 public:
  FileSystemComparer();

  StateDict getDirectoryState(std::string);
  std::tuple<FilePathList, FilePathList, FilePathList> compareTwoStates(
      StateDict, StateDict);

 private:
  ChecksumCalculator checksumCalc;
};

#endif  // FILESYSTEMCOMPARER_H
