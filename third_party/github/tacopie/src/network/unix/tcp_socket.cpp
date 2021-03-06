// MIT License
//
// Copyright (c) 2016-2017 Simon Ninon <simon.ninon@gmail.com>
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include <error.hpp>
#include <logger.hpp>
#include <tcp_server.hpp>

#include <cstring>

#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

namespace cpp_redis {

//!
//! ctor & dtor
//!

tcp_socket::tcp_socket(void)
: m_fd(__cpp_redis_INVALID_FD)
, m_host("")
, m_port(0)
, m_type(type::UNKNOWN) { __cpp_redis_LOG(debug, "create tcp_socket"); }

//!
//! custom ctor
//! build socket from existing file descriptor
//!

tcp_socket::tcp_socket(fd_t fd, const std::string& host, std::uint32_t port, type t)
: m_fd(fd)
, m_host(host)
, m_port(port)
, m_type(t) { __cpp_redis_LOG(debug, "create tcp_socket"); }

//!
//! Move constructor
//!

tcp_socket::tcp_socket(tcp_socket&& socket)
: m_fd(std::move(socket.m_fd))
, m_host(socket.m_host)
, m_port(socket.m_port)
, m_type(socket.m_type) {
  socket.m_fd   = __cpp_redis_INVALID_FD;
  socket.m_type = type::UNKNOWN;

  __cpp_redis_LOG(debug, "moved tcp_socket");
}

//!
//! client socket operations
//!

std::vector<char>
tcp_socket::recv(std::size_t size_to_read) {
  create_socket_if_necessary();
  check_or_set_type(type::CLIENT);

  std::vector<char> data(size_to_read, 0);

  ssize_t rd_size = ::recv(m_fd, const_cast<char*>(data.data()), size_to_read, 0);

  if (rd_size == -1) { __cpp_redis_THROW(error, "recv() failure"); }

  if (rd_size == 0) { __cpp_redis_THROW(warn, "nothing to read, socket has been closed by remote host"); }

  data.resize(rd_size);

  return data;
}

std::size_t
tcp_socket::send(const std::vector<char>& data, std::size_t size_to_write) {
  create_socket_if_necessary();
  check_or_set_type(type::CLIENT);

  ssize_t wr_size = ::send(m_fd, data.data(), size_to_write, 0);

  if (wr_size == -1) { __cpp_redis_THROW(error, "send() failure"); }

  return wr_size;
}

void
tcp_socket::connect(const std::string& host, std::uint32_t port) {
  //! Reset host and port
  m_host = host;
  m_port = port;

  create_socket_if_necessary();
  check_or_set_type(type::CLIENT);

  struct sockaddr_un server_addr_un;
  struct sockaddr_in server_addr_in;

  //! Handle case of unix sockets if port is 0
  bool is_unix_socket = m_port == 0;
  if (is_unix_socket) {
    std::memset(&server_addr_un, 0, sizeof(server_addr_un));
    server_addr_un.sun_family = AF_UNIX;
    strncpy(server_addr_un.sun_path, host.c_str(), sizeof(server_addr_un.sun_path) - 1);
  }
  else {
    struct addrinfo* result = nullptr;
    struct addrinfo hints;

    memset(&hints, 0, sizeof(hints));
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_family   = AF_INET;

    if (getaddrinfo(host.c_str(), nullptr, &hints, &result) != 0) { __cpp_redis_THROW(error, "getaddrinfo() failure"); }

    std::memset(&server_addr_in, 0, sizeof(server_addr_in));
    server_addr_in.sin_addr   = ((struct sockaddr_in*) (result->ai_addr))->sin_addr;
    server_addr_in.sin_port   = htons(port);
    server_addr_in.sin_family = AF_INET;

    freeaddrinfo(result);
  }

  socklen_t addr_len                 = is_unix_socket ? sizeof(server_addr_un) : sizeof(server_addr_in);
  const struct sockaddr* server_addr = is_unix_socket ? (const struct sockaddr*) &server_addr_un : (const struct sockaddr*) &server_addr_in;

  if (::connect(m_fd, server_addr, addr_len) == -1) { __cpp_redis_THROW(error, "connect() failure"); }
}

//!
//! server socket operations
//!

void
tcp_socket::bind(const std::string& host, std::uint32_t port) {
  //! Reset host and port
  m_host = host;
  m_port = port;

  create_socket_if_necessary();
  check_or_set_type(type::SERVER);

  struct sockaddr_un server_addr_un;
  struct sockaddr_in server_addr_in;

  //! Handle case of unix sockets if port is 0
  bool is_unix_socket = m_port == 0;
  if (is_unix_socket) {
    std::memset(&server_addr_un, 0, sizeof(server_addr_un));
    server_addr_un.sun_family = AF_UNIX;
    strncpy(server_addr_un.sun_path, host.c_str(), sizeof(server_addr_un.sun_path) - 1);
  }
  else {
    struct addrinfo* result = nullptr;

    if (getaddrinfo(host.c_str(), nullptr, nullptr, &result) != 0) {
      __cpp_redis_THROW(error, "getaddrinfo() failure");
    }

    std::memset(&server_addr_in, 0, sizeof(server_addr_in));
    server_addr_in.sin_addr   = ((struct sockaddr_in*) (result->ai_addr))->sin_addr;
    server_addr_in.sin_port   = htons(port);
    server_addr_in.sin_family = AF_INET;

    freeaddrinfo(result);
  }

  socklen_t addr_len                 = is_unix_socket ? sizeof(server_addr_un) : sizeof(server_addr_in);
  const struct sockaddr* server_addr = is_unix_socket ? (const struct sockaddr*) &server_addr_un : (const struct sockaddr*) &server_addr_in;

  if (::bind(m_fd, server_addr, addr_len) == -1) { __cpp_redis_THROW(error, "bind() failure"); }
}

void
tcp_socket::listen(std::size_t max_connection_queue) {
  create_socket_if_necessary();
  check_or_set_type(type::SERVER);

  if (::listen(m_fd, max_connection_queue) == -1) { __cpp_redis_THROW(debug, "listen() failure"); }
}

tcp_socket
tcp_socket::accept(void) {
  create_socket_if_necessary();
  check_or_set_type(type::SERVER);

  struct sockaddr_in client_info;
  socklen_t client_info_struct_size = sizeof(client_info);

  fd_t client_fd = ::accept(m_fd, (struct sockaddr*) &client_info, &client_info_struct_size);

  if (client_fd == __cpp_redis_INVALID_FD) { __cpp_redis_THROW(error, "accept() failure"); }

  //! TODO: init with real client addr
  return {client_fd, "", client_info.sin_port, type::CLIENT};
}

//!
//! general socket operations
//!

void
tcp_socket::close(void) {
  if (m_fd != __cpp_redis_INVALID_FD) {
    __cpp_redis_LOG(debug, "close socket");
    ::close(m_fd);
  }

  m_fd   = __cpp_redis_INVALID_FD;
  m_type = type::UNKNOWN;
}

//!
//! create a new socket if no socket has been initialized yet
//!

void
tcp_socket::create_socket_if_necessary(void) {
  if (m_fd != __cpp_redis_INVALID_FD) { return; }

  //! new TCP socket
  //! handle case of unix sockets by checking whether the port is 0 or not
  m_fd   = socket(m_port == 0 ? AF_UNIX : AF_INET, SOCK_STREAM, 0);
  m_type = type::UNKNOWN;

  if (m_fd == __cpp_redis_INVALID_FD) { __cpp_redis_THROW(error, "tcp_socket::create_socket_if_necessary: socket() failure"); }
}

//!
//! check whether the current socket has an approriate type for that kind of operation
//! if current type is UNKNOWN, update internal type with given type
//!

void
tcp_socket::check_or_set_type(type t) {
  if (m_type != type::UNKNOWN && m_type != t) { __cpp_redis_THROW(error, "trying to perform invalid operation on socket"); }

  m_type = t;
}

//!
//! get socket name information
//!

const std::string&
tcp_socket::get_host(void) const {
  return m_host;
}

std::uint32_t
tcp_socket::get_port(void) const {
  return m_port;
}

//!
//! get socket type
//!

tcp_socket::type
tcp_socket::get_type(void) const {
  return m_type;
}

//!
//! set type, should be used if some operations determining socket type
//! have been done on the behalf of the tcp_socket instance
//!

void
tcp_socket::set_type(type t) {
  m_type = t;
}

//!
//! direct access to the underlying fd
//!

fd_t
tcp_socket::get_fd(void) const {
  return m_fd;
}

//!
//! comparison operator
//!
bool
tcp_socket::operator==(const tcp_socket& rhs) const {
  return m_fd == rhs.m_fd && m_type == rhs.m_type;
}

bool
tcp_socket::operator!=(const tcp_socket& rhs) const {
  return !operator==(rhs);
}

} //! cpp_redis
