#include "checksumcalculator.h"

#include <openssl/evp.h>

#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

ChecksumCalculator::ChecksumCalculator() {}

std::string ChecksumCalculator::calculateSHA1(const std::string &file_path,
                                              const int size) {
  EVP_MD_CTX *md5Context = EVP_MD_CTX_new();
  if (md5Context == nullptr) {
    return "";
  }

  if (EVP_DigestInit_ex(md5Context, EVP_md5(), nullptr) != 1) {
    EVP_MD_CTX_free(md5Context);
    return "";
  }

  const size_t CHUNK_SIZE = size;
  unsigned char buffer[CHUNK_SIZE];

  std::ifstream file(file_path, std::ios::binary);
  if (!file) {
    EVP_MD_CTX_free(md5Context);
    return "";
  }

  while (file) {
    file.read(reinterpret_cast<char *>(buffer), CHUNK_SIZE);
    const auto bytesRead = file.gcount();
    if (EVP_DigestUpdate(md5Context, buffer, bytesRead) != 1) {
      EVP_MD_CTX_free(md5Context);
      return "";
    }
  }

  unsigned char hash[EVP_MAX_MD_SIZE];
  unsigned int hashLength = 0;
  if (EVP_DigestFinal_ex(md5Context, hash, &hashLength) != 1) {
    EVP_MD_CTX_free(md5Context);
    return "";
  }

  EVP_MD_CTX_free(md5Context);

  std::stringstream ss;
  ss << std::hex << std::setfill('0');
  for (unsigned int i = 0; i < hashLength; i++) {
    ss << std::setw(2) << static_cast<unsigned int>(hash[i]);
  }

  return ss.str();
}

std::string ChecksumCalculator::calculateHeaderSHA1(
    const std::string &file_path, int nums) {
  EVP_MD_CTX *md5Context = EVP_MD_CTX_new();
  if (md5Context == nullptr) {
    return "";
  }

  if (EVP_DigestInit_ex(md5Context, EVP_md5(), nullptr) != 1) {
    EVP_MD_CTX_free(md5Context);
    return "";
  }

  const size_t CHUNK_SIZE = 8192;
  unsigned char buffer[CHUNK_SIZE];

  std::ifstream file(file_path, std::ios::binary);
  if (!file) {
    EVP_MD_CTX_free(md5Context);
    return "";
  }

  int i = 0;

  while (file) {
    ++i;
    if (i > nums) {
      break;
    }
    file.read(reinterpret_cast<char *>(buffer), CHUNK_SIZE);
    const auto bytesRead = file.gcount();
    if (EVP_DigestUpdate(md5Context, buffer, bytesRead) != 1) {
      EVP_MD_CTX_free(md5Context);
      return "";
    }
  }

  unsigned char hash[EVP_MAX_MD_SIZE];
  unsigned int hashLength = 0;
  if (EVP_DigestFinal_ex(md5Context, hash, &hashLength) != 1) {
    EVP_MD_CTX_free(md5Context);
    return "";
  }

  EVP_MD_CTX_free(md5Context);

  std::stringstream ss;
  ss << std::hex << std::setfill('0');
  for (unsigned int i = 0; i < hashLength; i++) {
    ss << std::setw(2) << static_cast<unsigned int>(hash[i]);
  }

  return ss.str();
}
