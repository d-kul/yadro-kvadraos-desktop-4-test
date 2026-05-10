#pragma once

#include <cctype>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <mutex>
#include <set>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "nlohmann/json.hpp"

using json = nlohmann::json;
namespace fs = std::filesystem;
using namespace std::string_view_literals;

struct MediaFiles {
  std::vector<std::string> audio;
  std::vector<std::string> video;
  std::vector<std::string> images;
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_ONLY_SERIALIZE(MediaFiles, audio, video,
                                                  images);

class MediaStore {
public:
  void update(MediaFiles files);

  std::string_view json() const;

private:
  MediaFiles files;
  mutable std::mutex files_mx;
  std::string json_cache = "{\"audio\":[],\"video\":[],\"images\":[]}\n";
};

class MediaCrawler {
public:
  inline static const std::set audio_ext = {
      ".aac"sv,  ".aiff"sv, ".alac"sv, ".ape"sv,  ".flac"sv, ".m4a"sv, ".mid"sv,
      ".midi"sv, ".mp3"sv,  ".ogg"sv,  ".opus"sv, ".wav"sv,  ".wma"sv};
  inline static const std::set video_ext = {
      ".3gp"sv, ".avi"sv,  ".flv"sv, ".m4v"sv, ".mkv"sv,  ".mov"sv,
      ".mp4"sv, ".mpeg"sv, ".mpg"sv, ".ogv"sv, ".webm"sv, ".wmv"sv};
  inline static const std::set image_ext = {
      ".bmp"sv, ".gif"sv, ".heic"sv, ".heif"sv, ".jpeg"sv, ".jpg"sv,
      ".png"sv, ".svg"sv, ".tif"sv,  ".tiff"sv, ".webp"sv};

  MediaCrawler(fs::path directory, std::chrono::milliseconds interval);

  ~MediaCrawler() = default;

  MediaCrawler(const MediaCrawler &) = delete;
  MediaCrawler &operator=(const MediaCrawler &) = delete;

  void start();

  void stop();

  void join();

  MediaStore &get_store() { return store; }

private:
  static std::string relative_or_absolute(const fs::path &file,
                                          const fs::path &root);

  MediaFiles scan_media_files(std::stop_token st, const fs::path &root);

  void run(std::stop_token st);

  fs::path directory;
  std::chrono::milliseconds interval;

  mutable std::mutex mx;
  std::condition_variable_any cv;

  MediaStore store;

  std::jthread thread;
};
