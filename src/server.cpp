#include <arpa/inet.h>
#include <cassert>
#include <errno.h>
#include <fcntl.h>
#include <netinet/ip.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

const int k_max_msg = 4096;

struct Conn {
  int fd = -1;
  // application's intention, for the event loop
  bool want_read = false;
  bool want_write = false;
  bool want_close = false;
  // buffered input and output
  std::vector<uint8_t> incoming; // data to be parsed by the application
  std::vector<uint8_t> outgoing; // responses generated by the application
};

static void die(const char *msg) {
  int err = errno;
  fprintf(stderr, "[%d] %s\n", err, msg);
  abort();
}

static void msg(const char *msg) { fprintf(stderr, "%s\n", msg); }

static void msg_errno(const char *msg) {
  fprintf(stderr, "[errno:%d] %s\n", errno, msg);
}

static void fd_set_nb(int fd) {
  fcntl(fd, F_SETFL,
        fcntl(fd, F_GETFL, 0) |
            O_NONBLOCK); // setting file status flags to non blocking
}

static Conn *handle_accept(int fd) {
  // accept
  struct sockaddr_in client_addr = {};
  socklen_t socklen = sizeof(client_addr);
  int connfd = accept(fd, (struct sockaddr *)&client_addr, &socklen);
  if (connfd < 0) {
    msg_errno("accept() error");
    return NULL;
  }
  uint32_t ip = client_addr.sin_addr.s_addr;
  fprintf(stderr, "new client from %u.%u.%u.%u:%u\n", ip & 255, (ip >> 8) & 255,
          (ip >> 16) & 255, ip >> 24, ntohs(client_addr.sin_port));
  msg("world");

  // set the new connection fd to nonblocking mode
  fd_set_nb(connfd);

  // create a `struct Conn`
  Conn *conn = new Conn();
  conn->fd = connfd;
  conn->want_read = true;
  return conn;
}

static void buf_append(std::vector<uint8_t> &incoming, const uint8_t *buf,
                       ssize_t rv) {
  incoming.insert(incoming.end(), buf, buf + rv);
}

static void buf_consume(std::vector<uint8_t> &incoming, ssize_t len) {
  incoming.erase(incoming.begin(), incoming.begin() + len);
}

static bool try_one_request(Conn *conn) {
  // 3. Try to parse the accumulated buffer.
  // Protocol: message header
  if (conn->incoming.size() < 4) {
    return false; // want read
  }
  uint32_t len = 0;
  memcpy(&len, conn->incoming.data(), 4);
  if (len > k_max_msg) { // protocol error
    conn->want_close = true;
    return false; // want close
  }
  // Protocol: message body
  if (4 + len > conn->incoming.size()) {
    return false; // want read
  }
  const uint8_t *request = &conn->incoming[4];
  // 4. Process the parsed message.
  // ...
  // generate the response (echo)
  buf_append(conn->outgoing, (const uint8_t *)&len, 4);
  buf_append(conn->outgoing, request, len);
  // 5. Remove the message from `Conn::incoming`.
  buf_consume(conn->incoming, 4 + len);
  return true; // success
}

static void handle_read(Conn *conn) {
  // 1. Do a non-blocking read.
  uint8_t buf[64 * 1024];
  ssize_t rv = read(conn->fd, buf, sizeof(buf));
  if (rv <= 0) { // handle IO error (rv < 0) or EOF (rv == 0)
    conn->want_close = true;
    return;
  }
  // 2. Add new data to the `Conn::incoming` buffer.
  buf_append(conn->incoming, buf, (size_t)rv);
  // 3. Try to parse the accumulated buffer.
  // 4. Process the parsed message.
  // 5. Remove the message from `Conn::incoming`.
  while (try_one_request(conn)) {
  }
  if (conn->outgoing.size() > 0) { // has a response
    conn->want_read = false;
    conn->want_write = true;
  } else {
    conn->want_read = true;
    conn->want_write = false;
  }
}

static void handle_write(Conn *conn) {
  assert(conn->outgoing.size() > 0);
  ssize_t rv = write(conn->fd, conn->outgoing.data(), conn->outgoing.size());
  if (rv < 0) {
    conn->want_close = true; // error handling
    return;
  }
  // remove written data from `outgoing`
  buf_consume(conn->outgoing, (size_t)rv);
  if (conn->outgoing.size() == 0) { // has a response
    conn->want_read = true;
    conn->want_write = false;
  } else {
    conn->want_read = false;
    conn->want_write = true;
  }
}

int main() {
  int fd = socket(AF_INET, SOCK_STREAM, 0);

  int val = 1;
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));

  struct sockaddr_in addr;
  addr.sin_family = AF_INET;
  addr.sin_port = htons(1234);     // port
  addr.sin_addr.s_addr = htonl(0); // wildcard IP 0.0.0.0
  int rv = bind(fd, (const struct sockaddr *)&addr, sizeof(addr));
  if (rv) {
    die("bind()");
  }

  rv = listen(fd, SOMAXCONN);
  if (rv) {
    die("listen()");
  }

  // the event loop
  std::vector<struct pollfd> poll_args;
  std::vector<Conn *> fd2conn;
  // the event loop
  while (true) {
    // prepare the arguments of the poll()
    poll_args.clear();
    // put the listening sockets in the first position
    struct pollfd pfd = {fd, POLLIN, 0};
    poll_args.push_back(pfd);
    // the rest are connection sockets
    for (Conn *conn : fd2conn) {
      if (!conn) {
        continue;
      }
      struct pollfd pfd = {conn->fd, POLLERR, 0};
      // poll() flags from the application's intent
      if (conn->want_read) {
        pfd.events |= POLLIN;
      }
      if (conn->want_write) {
        pfd.events |= POLLOUT;
      }
      poll_args.push_back(pfd);
    }
    int rv = poll(poll_args.data(), (nfds_t)poll_args.size(), -1);
    if (rv < 0 && errno == EINTR) {
      continue; // not an error
    }
    if (rv < 0) {
      die("poll");
    }
    if (poll_args[0].revents) {
      if (Conn *conn = handle_accept(fd)) {
        // put it into the map
        if (fd2conn.size() <= (size_t)conn->fd) {
          fd2conn.resize(conn->fd + 1);
        }
        fd2conn[conn->fd] = conn;
      }
    }
    for (size_t i = 1; i < poll_args.size(); ++i) { // note: skip the 1st
      uint32_t ready = poll_args[i].revents;
      Conn *conn = fd2conn[poll_args[i].fd];
      if (ready & POLLIN) {
        handle_read(conn); // application logic
      }
      if (ready & POLLOUT) {
        handle_write(conn); // application logic
      }
    }
  }
}