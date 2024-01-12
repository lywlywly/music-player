#include "filesystemcomparer.h"

#include <algorithm>
#include <filesystem>
#include <vector>

#include "qdebug.h"

namespace fs = std::filesystem;

FileSystemComparer::FileSystemComparer() {}

bool isMusicFile(const std::string& filePath) {
  // List of common music file extensions
  std::vector<std::string> musicExtensions = {".mp3", ".wav", ".flac",
                                              ".ogg", ".aac", "alac"};

  // Get the file extension
  std::size_t extensionPos = filePath.find_last_of('.');
  if (extensionPos != std::string::npos) {
    std::string extension = filePath.substr(extensionPos);

    // Check if the extension is in the list of music extensions
    for (const std::string& musicExtension : musicExtensions) {
      if (extension == musicExtension) {
        return true;
      }
    }
  }

  return false;
}

std::map<std::string, std::string> FileSystemComparer::getDirectoryState(
    std::string directoryPath) {
  std::map<std::string, std::string> checksumMap;
  // very slow))
  // for (const auto& entry : fs::directory_iterator(directoryPath)) {
  //   if (entry.is_regular_file()) {
  //     std::string fname = entry.path().filename().string();
  //     checksumMap[fname] = checksumCalc.calculateSHA1(directoryPath + fname,
  //     1);
  //   }
  // }

  std::vector<std::string> filenames;
  for (const auto& entry : fs::directory_iterator(directoryPath)) {
    if (entry.is_regular_file() && isMusicFile(entry.path())) {
      filenames.push_back(entry.path().string());
      // qDebug() << entry.path().string();
    }
  }
  for (const auto& filename : filenames) {
    checksumMap[filename] = checksumCalc.calculateHeaderSHA1(filename, 1);
  }

  return checksumMap;
}

std::tuple<FilePathList, FilePathList, FilePathList>
FileSystemComparer::compareTwoStates(std::map<std::string, std::string> m1,
                                     std::map<std::string, std::string> m2) {
  std::vector<std::string> keys1;
  keys1.reserve(m1.size());
  std::transform(m1.begin(), m1.end(), std::back_inserter(keys1),
                 [](const std::pair<std::string, std::string>& pair) {
                   // std::pair<std::string, std::string> pair
                   return pair.first;
                 });
  std::vector<std::string> keys2;
  keys2.reserve(m2.size());
  std::transform(m2.begin(), m2.end(), std::back_inserter(keys2),
                 [](const std::pair<std::string, std::string>& pair) {
                   return pair.first;
                 });

  std::vector<std::string> removed;
  std::set_difference(keys1.begin(), keys1.end(), keys2.begin(), keys2.end(),
                      std::back_inserter(removed));
  std::vector<std::string> added;
  std::set_difference(keys2.begin(), keys2.end(), keys1.begin(), keys1.end(),
                      std::back_inserter(added));

  for (std::vector<std::string>::iterator it = removed.begin();
       it != removed.end(); ++it) {
  }
  for (std::vector<std::string>::iterator it = added.begin(); it != added.end();
       ++it) {
  }
  std::vector<std::string> commonElements;
  std::set_intersection(keys1.begin(), keys1.end(), keys2.begin(), keys2.end(),
                        std::back_inserter(commonElements));
  std::vector<std::string> changed;
  for (std::vector<std::string>::iterator it = commonElements.begin();
       it != commonElements.end(); ++it) {
    if (m1[*it] != m2[*it]) {
      changed.push_back(*it);
    }
  }

  return {removed, added, changed};
}
