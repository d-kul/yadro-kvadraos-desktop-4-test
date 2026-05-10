#include "server.hpp"

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <syncstream>
#include <thread>
#include <utility>

#include <fcntl.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

using namespace std::string_literals;

int ensure_no_err(int value) {
  if (value == -1 &&
      (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR))
    throw std::runtime_error(std::strerror(errno));
  return value;
}

FdHandle::~FdHandle() { close(); }

FdHandle::FdHandle(FdHandle &&other) noexcept
    : fd(std::exchange(other.fd, -1)) {}

FdHandle &FdHandle::operator=(FdHandle &&other) {
  using std::swap;
  swap(fd, other.fd);
  return *this;
}

void FdHandle::close() {
  if (fd != -1) {
    ::close(fd);
    fd = -1;
  }
}

FdHandle::FdHandle(int fd) : fd(ensure_no_err(fd)) {}

Wake::Wake()
    : FdHandle(ensure_no_err(::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC))) {}

Wake::~Wake() { drain(); }

void Wake::send() {
  std::uint64_t value = 1;
  ssize_t n = write(fd, &value, sizeof(value));
}

void Wake::drain() {
  std::uint64_t value;
  while (read(fd, &value, sizeof(value)) == sizeof(value)) {
    // drain all
  }
}

void Socket::set_blocking(bool blocking) {
  int flags = ensure_no_err(fcntl(fd, F_GETFL, 0));
  ensure_no_err(
      fcntl(fd, F_SETFL, blocking ? flags & ~O_NONBLOCK : flags | O_NONBLOCK));
}

ssize_t ClientSocket::send(std::span<const char> data, int flags) {
  return ensure_no_err(::send(fd, data.data(), data.size(), flags));
}

ssize_t ClientSocket::recv(std::span<char> data, int flags) {
  return ensure_no_err(::recv(fd, data.data(), data.size(), flags));
}

ServerSocket::ServerSocket(int domain, int type, int protocol)
    : Socket(::socket(domain, type, protocol)) {
  std::memset(&address, 0, sizeof(address));
}

void ServerSocket::set_reuseaddr() {
  int yes = 1;
  ensure_no_err(::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)));
}

void ServerSocket::bind(in_addr_t addr, in_port_t port) {
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = addr;
  address.sin_port = htons(port);
  ensure_no_err(::bind(fd, reinterpret_cast<const sockaddr *>(&address),
                       sizeof(address)));
}

void ServerSocket::listen(int backlog) { ensure_no_err(::listen(fd, backlog)); }

void ServerSocket::accept(ClientSocket &client_socket) {
  client_socket.fd = ensure_no_err(
      ::accept(fd, reinterpret_cast<sockaddr *>(&client_socket.address),
               &client_socket.len));
}

ClientHandle::ClientHandle(std::stop_token st, ClientSocket socket,
                           MediaStore &store)
    : st(st), socket(std::move(socket)),
      thread(&ClientHandle::run, this, std::ref(store)) {}

void ClientHandle::shutdown() {
  std::lock_guard g(socket_mx);
  if (socket.get_fd() != -1) {
    ::shutdown(socket.get_fd(), SHUT_RDWR);
  }
}

std::string ClientHandle::get_address() {
  std::ostringstream client_address_ss;
  auto port = ntohs(socket.get_address().sin_port);
  auto addr = ntohl(socket.get_address().sin_addr.s_addr);
  client_address_ss << (addr >> 24) << "." << ((addr >> 16) & 0xff) << "."
                    << ((addr >> 8) & 0xff) << "." << (addr & 0xff) << ":"
                    << port;
  return std::move(client_address_ss).str();
}

void ClientHandle::send(std::string_view data, int flags) {
  std::size_t total = 0;
  while (!st.stop_requested() && total < data.length()) {
    total += static_cast<std::size_t>(socket.send(data.substr(total), flags));
  }
}

std::string ClientHandle::http_read_request_line(int flags) {
  std::string request_line(BUFFER_SIZE, '\0');
  std::span line_span = request_line;
  std::size_t total = 0;

  std::string::size_type pos = std::string::npos;
  while (!st.stop_requested() && pos == std::string::npos) {
    if (total == request_line.size()) {
      request_line.resize(request_line.size() * 2);
      line_span = request_line;
    }
    ssize_t read = socket.recv(line_span.subspan(total), flags);
    if (read == 0) {
      break;
    }
    ensure_no_err(read);
    pos = request_line.find("\r\n", total);
    total += static_cast<std::size_t>(read);
  }

  return request_line;
}

void ClientHandle::http_respond(int status_code, std::string_view status_text,
                                std::string_view content_type,
                                std::string_view body) {
  std::ostringstream out;
  out << "HTTP/1.1 " << status_code << " " << status_text << "\r\n"
      << "Content-Type: " << content_type << "\r\n"
      << "Content-Length: " << body.size() << "\r\n"
      << "Connection: close\r\n"
      << "Cache-Control: no-store\r\n"
      << "\r\n"
      << body;
  send(out.view(), MSG_NOSIGNAL);
}

void ClientHandle::run(MediaStore &store) {
  try {
    std::osyncstream(std::cout)
        << "Handling client connection from " << get_address() << std::endl;
    handler(store);
    std::osyncstream(std::cout)
        << "Closing client connection from " << get_address() << std::endl;
    close();
  } catch (const std::runtime_error &e) {
    std::osyncstream(std::cerr) << "Client error: " << e.what() << std::endl;
  }
}

void ClientHandle::handler(MediaStore &store) {
  static constexpr std::string_view method_prefix = "GET ";
  std::string request_line = http_read_request_line();
  if (st.stop_requested()) {
    return;
  }

  if (!request_line.starts_with(method_prefix)) {
    http_respond(405, "Method Not Allowed", "text/plain; charset=utf-8",
                 "Only GET requests are supported.\n");
    return;
  }

  std::string_view target = request_line;
  target = target.substr(method_prefix.length(),
                         request_line.find(' ', method_prefix.length()) -
                             method_prefix.length());

  if (target != "/media_files") {
    http_respond(404, "Not Found", "text/plain; charset=utf-8",
                 "Use GET /media_files\n");
    return;
  }

  http_respond(200, "OK", "application/json; charset=utf-8", store.json());
}

void ClientHandle::close() {
  std::lock_guard g(socket_mx);
  this->socket.close();
}

Server::Server(MediaStore &store)
    : store(store), socket(AF_INET, SOCK_STREAM, 0) {
  socket.set_reuseaddr();
  socket.set_blocking(false);
}

void Server::set_timeout(int ms) { this->timeout_ms = ms; }

void Server::start(int port) {
  socket.bind(INADDR_ANY, port);
  socket.listen(LISTEN_BACKLOG);
  std::osyncstream(std::cout)
      << "Server started listening on port: " << port << std::endl;
  thread = std::jthread([this](std::stop_token st) { loop(st); });
}

void Server::stop() {
  if (thread.get_stop_token().stop_possible()) {
    thread.request_stop();
    wake.send();
    for (auto &client : clients) {
      client.shutdown();
    }
  }
}

void Server::join() {
  if (thread.joinable()) {
    thread.join();
    std::osyncstream(std::cout) << "Closing server" << std::endl;
  }
}

void Server::loop(std::stop_token st) {
  try {
    pollfd fds[] = {{.fd = socket.get_fd(), .events = POLLIN, .revents = 0},
                    {.fd = wake.get_fd(), .events = POLLIN, .revents = 0}};

    ClientSocket client_socket;
    while (!st.stop_requested()) {
      int rc = ensure_no_err(poll(fds, 2, -1));
      if (rc == -1 && errno == EINTR) {
        // interrupted
        continue;
      }
      if (fds[1].revents & POLLIN) {
        // wake
        continue;
      }
      if (rc == 0 || !(fds[0].revents & POLLIN)) {
        // timed out
        continue;
      }
      while (!st.stop_requested()) {
        socket.accept(client_socket);
        if (client_socket.get_fd() == -1) {
          if (errno == EAGAIN || errno == EWOULDBLOCK) {
            break;
          }
          if (errno == EINTR) {
            continue;
          }
        }
        clients.emplace_back(st, std::move(client_socket), store);
        break;
      }
    }
  } catch (const std::runtime_error &e) {
    std::osyncstream(std::cerr) << "Server error: " << e.what() << std::endl;
  }
}
