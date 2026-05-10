#pragma once

#include <cstddef>
#include <list>
#include <mutex>
#include <span>
#include <stop_token>
#include <thread>

#include <netinet/in.h>

#include "crawler.hpp"

class FdHandle {
public:
  virtual ~FdHandle();

  FdHandle(const FdHandle &) = delete;
  FdHandle &operator=(const FdHandle &) = delete;

  FdHandle(FdHandle &&other) noexcept;
  FdHandle &operator=(FdHandle &&other);

  void close();

  int get_fd() const { return fd; }

protected:
  FdHandle() = default;

  explicit FdHandle(int fd);

  int fd = -1;
};

class Wake : public FdHandle {
public:
  Wake();

  ~Wake();

  void send();

  using FdHandle::FdHandle;

private:
  void drain();
};

class Socket : public FdHandle {
public:
  void set_blocking(bool blocking);

  using FdHandle::FdHandle;
};

class ClientSocket : public Socket {
public:
  static constexpr std::size_t BUFFER_SIZE = 4096;

  friend class Server;
  friend class ServerSocket;

  sockaddr_in get_address() const { return address; }

  ssize_t send(std::span<const char> data, int flags);

  ssize_t recv(std::span<char> data, int flags);

private:
  sockaddr_in address;
  socklen_t len = sizeof(address);
};

class ServerSocket : public Socket {
public:
  ServerSocket(int domain, int type, int protocol);

  using Socket::FdHandle;

  void set_reuseaddr();

  void bind(in_addr_t addr, in_port_t port);

  void listen(int backlog);

  void accept(ClientSocket &client_socket);

private:
  sockaddr_in address;
};

class ClientHandle {
public:
  static constexpr std::size_t BUFFER_SIZE = 4096;

  ClientHandle(std::stop_token, ClientSocket socket, MediaStore &store);

  ClientHandle(const ClientHandle &) = delete;
  ClientHandle &operator=(const ClientHandle &) = delete;

  ClientHandle(ClientHandle &&) = delete;
  ClientHandle &operator=(ClientHandle &&) = delete;

  void shutdown();

private:
  std::string get_address();

  void send(std::string_view data, int flags);

  std::string http_read_request_line(int flags = 0);

  void http_respond(int status_code, std::string_view status_text,
                    std::string_view content_type, std::string_view body);

  void run(MediaStore &store);

  void handler(MediaStore &store);

  void close();

  std::stop_token st;

  ClientSocket socket;
  mutable std::mutex socket_mx;

  std::jthread thread;
};

class Server {
public:
  static constexpr int LISTEN_BACKLOG = 16;

  Server(MediaStore &store);

  ~Server() = default;

  Server(const Server &) = delete;
  Server &operator=(const Server &) = delete;

  Server(Server &&) = delete;
  Server &operator=(Server &&) = delete;

  void set_timeout(int ms);

  void start(int port);

  void stop();

  void join();

private:
  void loop(std::stop_token st);

  std::atomic<int> timeout_ms = 500;

  ServerSocket socket;
  Wake wake;
  std::list<ClientHandle> clients;

  MediaStore &store;

  std::jthread thread;
};
