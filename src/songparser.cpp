#include "songparser.h"
#include "utils.h"
#include <QDebug>
#include <cctype>
#include <string>
#include <taglib/fileref.h>
#include <taglib/tpropertymap.h>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#endif

extern "C" {
#include "libavformat/avformat.h"
}

namespace {
bool isBuiltInTagKey(const std::string &key, const ColumnRegistry &registry) {
  return registry.isBuiltInSongAttributeKey(QString::fromStdString(key));
}

std::string trimAscii(std::string value) {
  size_t first = 0;
  while (first < value.size() &&
         std::isspace(static_cast<unsigned char>(value[first]))) {
    ++first;
  }
  size_t last = value.size();
  while (last > first &&
         std::isspace(static_cast<unsigned char>(value[last - 1]))) {
    --last;
  }
  return value.substr(first, last - first);
}

std::string normalizeCurrentPart(std::string value) {
  const size_t slashPos = value.find('/');
  if (slashPos == std::string::npos) {
    return trimAscii(value);
  }
  return trimAscii(value.substr(0, slashPos));
}

std::string joinTagValuesForDisplay(const TagLib::StringList &values) {
  std::string joined;
  bool first = true;
  for (const TagLib::String &value : values) {
    const std::string part = value.to8Bit(true);
    if (part.empty()) {
      continue;
    }
    if (!first) {
      joined += ", ";
    }
    joined += part;
    first = false;
  }
  return joined;
}

std::vector<std::string> splitTagValues(const std::string &value) {
  std::vector<std::string> values;
  size_t start = 0;
  while (start <= value.size()) {
    const size_t pos = value.find(';', start);
    const size_t end = (pos == std::string::npos) ? value.size() : pos;
    std::string part = trimAscii(value.substr(start, end - start));
    if (!part.empty()) {
      values.push_back(std::move(part));
    }
    if (pos == std::string::npos) {
      break;
    }
    start = pos + 1;
  }
  return values;
}
} // namespace

bool SongParser::writeTags(
    const std::string &filepath,
    const std::unordered_map<std::string, std::string> &updatedFields,
    const ColumnRegistry &columnRegistry) {
  if (updatedFields.empty()) {
    return true;
  }

#ifdef _WIN32
  int wlen = MultiByteToWideChar(CP_UTF8, 0, filepath.c_str(), -1, nullptr, 0);
  std::wstring wpath(wlen, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, filepath.c_str(), -1, wpath.data(), wlen);
  TagLib::FileRef file(wpath.c_str());
#else
  TagLib::FileRef file(filepath.c_str());
#endif
  if (file.isNull() || file.file() == nullptr) {
    qWarning() << "SongParser::writeTags failed to open file:"
               << QString::fromStdString(filepath);
    return false;
  }

  TagLib::PropertyMap properties = file.file()->properties();
  for (const auto &[appKey, value] : updatedFields) {
    if (appKey.empty()) {
      continue;
    }

    const ColumnDefinition *definition =
        columnRegistry.findColumn(QString::fromStdString(appKey));
    if (definition != nullptr && definition->source == ColumnSource::Computed) {
      continue;
    }
    std::string tagKey = util::canonicalizeTagKey(appKey);
    if (tagKey.empty()) {
      continue;
    }

    const TagLib::String propertyKey(tagKey.c_str(), TagLib::String::UTF8);
    if (value.empty()) {
      properties.erase(propertyKey);
      continue;
    }

    TagLib::StringList list;
    for (const std::string &part : splitTagValues(value)) {
      list.append(TagLib::String(part.c_str(), TagLib::String::UTF8));
    }
    if (list.isEmpty()) {
      properties.erase(propertyKey);
      continue;
    }
    properties.replace(propertyKey, list);
  }

  file.file()->setProperties(properties);
  return file.file()->save();
}

MSong SongParser::parse(
    const std::string &filepath, const ColumnRegistry &columnRegistry,
    std::unordered_map<std::string, std::string> *remainingFields) {
#ifdef _WIN32
  // convert UTF-8 std::string → UTF-16 std::wstring
  int wlen = MultiByteToWideChar(CP_UTF8, 0, filepath.c_str(), -1, nullptr, 0);
  std::wstring wpath(wlen, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, filepath.c_str(), -1, wpath.data(), wlen);

  // use wide-character overload on Windows
  TagLib::FileRef file(wpath.c_str());

#else
  TagLib::FileRef file(filepath.c_str());
#endif
  MSong tags;
  if (remainingFields) {
    remainingFields->clear();
  }

  if (!file.isNull() && file.file()) {
    const TagLib::PropertyMap properties = file.file()->properties();
    for (auto it = properties.begin(); it != properties.end(); ++it) {
      if (it->second.isEmpty()) {
        continue;
      }

      const std::string rawKey = it->first.to8Bit(true);
      const std::string key = util::canonicalizeTagKey(rawKey);
      if (key.empty()) {
        continue;
      }
      const std::string value = joinTagValuesForDisplay(it->second);
      if (value.empty()) {
        continue;
      }

      if (isBuiltInTagKey(key, columnRegistry)) {
        const ColumnDefinition *definition =
            columnRegistry.findColumn(QString::fromStdString(key));
        tags[key] = FieldValue(value, definition ? definition->valueType
                                                 : ColumnValueType::Text);
      } else {
        const QString columnId =
            QString::fromStdString(std::string{"attr:"} + key);
        if (columnRegistry.hasColumn(columnId)) {
          const ColumnDefinition *definition =
              columnRegistry.findColumn(columnId);
          tags[columnId.toStdString()] =
              FieldValue(value, definition ? definition->valueType
                                           : ColumnValueType::Text);
        } else if (remainingFields) {
          (*remainingFields)[key] = value;
        }
      }
    }
  }

  auto discIt = tags.find("discnumber");
  if (discIt != tags.end()) {
    discIt->second = FieldValue(normalizeCurrentPart(discIt->second.text),
                                ColumnValueType::Number);
  }
  auto trackIt = tags.find("tracknumber");
  if (trackIt != tags.end()) {
    trackIt->second = FieldValue(normalizeCurrentPart(trackIt->second.text),
                                 ColumnValueType::Number);
  }

  tags["filepath"] = FieldValue(filepath, ColumnValueType::Text);

  return tags;
}

std::pair<std::vector<uint8_t>, size_t>
SongParser::extractCoverImage(const std::string &filepath) {
  AVFormatContext *fmt_ctx = nullptr;

  if (avformat_open_input(&fmt_ctx, filepath.c_str(), nullptr, nullptr) < 0) {
    throw std::runtime_error("Failed to open input file");
  }

  if (avformat_find_stream_info(fmt_ctx, nullptr) < 0) {
    avformat_close_input(&fmt_ctx);
    throw std::runtime_error("Failed to find stream info");
  }

  for (unsigned int i = 0; i < fmt_ctx->nb_streams; ++i) {
    AVStream *stream = fmt_ctx->streams[i];
    if (stream->disposition & AV_DISPOSITION_ATTACHED_PIC) {
      AVPacket &pkt = stream->attached_pic;
      std::vector<uint8_t> image(pkt.data, pkt.data + pkt.size);
      avformat_close_input(&fmt_ctx);
      return {std::move(image), image.size()};
    }
  }

  avformat_close_input(&fmt_ctx);
  // throw std::runtime_error("No cover image found");
  qDebug() << "No cover image found";
  return {{}, 0};
}
