#include <charconv>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <syncstream>

#include "crawler.hpp"
#include "server.hpp"

struct Config {
  fs::path scan_path;
  unsigned interval_ms = 3000;
  int port = 1234;
};

void print_usage(const char *program) {
  std::cerr
      << "Usage: " << program
      << " --directory <directory> [--interval <milliseconds> --port <port>]\n"
      << "Example: " << program
      << " --directory \"$HOME\" --interval 30000 --port 1234\n";
}

unsigned parse_uint(std::string_view value, std::string_view option_name) {
  unsigned i;
  auto [ptr, ec] =
      std::from_chars(value.data(), value.data() + value.size(), i);

  if (ec == std::errc::invalid_argument ||
      ec == std::errc::result_out_of_range ||
      ptr != value.data() + value.size() || ec != std::errc())
    throw std::runtime_error(std::string("Invalid value for ") +
                             option_name.data() + ": " + value.data());
  return i;
}

Config parse_args(int argc, char **argv) {
  Config config;

  for (int i = 1; i < argc; ++i) {
    std::string_view arg = argv[i];
    auto need_value = [&](std::string_view option) -> std::string_view {
      if (i + 1 >= argc) {
        throw std::runtime_error(std::string("Missing value for ") +
                                 option.data());
      }
      return argv[++i];
    };

    if (arg == "--directory" || arg == "-d") {
      config.scan_path = need_value(arg);
    } else if (arg == "--interval" || arg == "-i") {
      config.interval_ms = parse_uint(need_value(arg), arg);
    } else if (arg == "--port" || arg == "-p") {
      config.port = parse_uint(need_value(arg), arg);
      if (config.port > 0xffff) {
        throw std::runtime_error(std::string("Port value is too big: ") +
                                 std::to_string(config.port));
      }
    } else if (arg == "--help" || arg == "-h") {
      print_usage(argv[0]);
      std::exit(EXIT_SUCCESS);
    } else {
      throw std::runtime_error(std::string("Unknown option: ") + arg.data());
    }
  }

  if (config.scan_path.empty()) {
    print_usage(argv[0]);
    std::exit(EXIT_FAILURE);
  }

  std::error_code ec;
  fs::path canonical = fs::weakly_canonical(config.scan_path, ec);
  if (!ec) {
    config.scan_path = canonical;
  }

  if (!fs::exists(config.scan_path) || !fs::is_directory(config.scan_path)) {
    throw std::runtime_error("Scan path is not a directory: " +
                             config.scan_path.string());
  }

  return config;
}

Server *g_server_ptr = nullptr;
MediaCrawler *g_crawler_ptr = nullptr;

void interrupt_handler(int) {
  if (g_server_ptr && g_crawler_ptr) {
    g_server_ptr->stop();
    g_crawler_ptr->stop();
  }
}

int main(int argc, char **argv) {
  std::signal(SIGINT, interrupt_handler);
  std::signal(SIGTERM, interrupt_handler);

  try {
    Config config = parse_args(argc, argv);

    MediaCrawler crawler(config.scan_path,
                         std::chrono::milliseconds{config.interval_ms});
    Server server(crawler.get_store());

    g_server_ptr = &server;
    g_crawler_ptr = &crawler;

    server.start(config.port);
    crawler.start();

    crawler.join();
    server.join();
  } catch (const std::runtime_error &e) {
    std::cerr << "Error: " << e.what() << std::endl;
    print_usage(argv[0]);
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
