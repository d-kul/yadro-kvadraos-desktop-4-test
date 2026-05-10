#include "crawler.hpp"

#include <algorithm>
#include <iostream>
#include <syncstream>
#include <system_error>

void MediaStore::update(MediaFiles files) {
  std::lock_guard g{files_mx};
  this->files = std::move(files);
  json_cache = static_cast<nlohmann::json>(this->files).dump();
}

std::string_view MediaStore::json() const {
  std::lock_guard g{files_mx};
  return json_cache;
}

MediaCrawler::MediaCrawler(fs::path directory,
                           std::chrono::milliseconds interval)
    : directory(directory), interval(interval) {}

void MediaCrawler::start() {
  thread = std::jthread([this](std::stop_token st) { run(st); });
  std::osyncstream(std::cout) << "Media crawler started" << std::endl;
}

void MediaCrawler::stop() {
  if (thread.get_stop_token().stop_possible()) {
    thread.request_stop();
    cv.notify_all();
  }
}

void MediaCrawler::join() {
  if (thread.joinable()) {
    thread.join();
    std::osyncstream(std::cout) << "Closing media crawler" << std::endl;
  }
}

std::string MediaCrawler::relative_or_absolute(const fs::path &file,
                                               const fs::path &root) {
  std::error_code ec;
  fs::path relative = fs::relative(file, root, ec);
  if (!ec && !relative.empty()) {
    return relative.generic_string();
  }
  return file.generic_string();
}

MediaFiles MediaCrawler::scan_media_files(std::stop_token st,
                                          const fs::path &root) {
  MediaFiles result;

  std::error_code ec;
  fs::recursive_directory_iterator it(
      root, fs::directory_options::skip_permission_denied, ec);

  if (ec) {
    std::osyncstream(std::cerr) << "Cannot open scan directory '" << root
                                << "': " << ec.message() << std::endl;
    return result;
  }

  for (const auto &entry : it) {
    if (st.stop_requested()) {
      break;
    }

    if (ec) {
      std::osyncstream(std::cerr)
          << "Skipping entry: " << ec.message() << std::endl;
      ec.clear();
      continue;
    }
    if (!entry.is_regular_file(ec) || ec) {
      ec.clear();
      continue;
    }

    auto ext = entry.path().extension().string();
    std::for_each(ext.begin(), ext.end(), [](char &c) { c = std::tolower(c); });

    std::string path = relative_or_absolute(entry.path(), root);

    if (audio_ext.count(ext) != 0) {
      result.audio.push_back(path);
    } else if (video_ext.count(ext) != 0) {
      result.video.push_back(path);
    } else if (image_ext.count(ext) != 0) {
      result.images.push_back(path);
    }
  }

  return result;
}

void MediaCrawler::run(std::stop_token st) {
  using namespace std::chrono;

  while (!st.stop_requested()) {
    auto t0 = high_resolution_clock::now();
    std::osyncstream(std::cout) << "Crawling media..." << std::endl;
    MediaFiles files = scan_media_files(st, directory);
    auto t1 = high_resolution_clock::now();
    auto dt = duration_cast<milliseconds>(high_resolution_clock::now() - t1);
    std::osyncstream(std::cout)
        << "Finished crawling media in " << dt << std::endl;
    store.update(std::move(files));
    std::unique_lock lock{mx};
    cv.wait_for(lock, st, interval, []() { return false; });
  }
}
