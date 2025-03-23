#include <arpa/inet.h>
#include <cassert>
#include <errno.h>
#include <fcntl.h>
#include <iostream>
#include <map>
#include <netinet/ip.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

const size_t k_max_msg = 32 << 20; // bitwise shift operation equal to 32 MB

/*This is created to remove efficiently from the front reducing its complexity
from O(n^2) to O(n). We remove to the front leaving part of the buffer empty and
then to append move the data to the front to make space for the new values
*/
struct Buffer {
  uint8_t *buffer_begin;
  uint8_t *buffer_end;
  uint8_t *data_begin;
  uint8_t *data_end;
};

struct Response {
  uint32_t status = 0;
  std::vector<uint8_t> data;
};

// status of responses
enum {
  RES_OK = 0,
  RES_ERR = 1, // error
  RES_NX = 2,  // key not found
};

struct Conn {
  int fd = -1;
  // application's intention, for the event loop
  bool want_read = false;
  bool want_write = false;
  bool want_close = false;
  // buffered input and output
  Buffer incoming; // data to be parsed by the application
  Buffer outgoing; // responses generated by the application
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

static void buf_append(Buffer *buf, const uint8_t *data, ssize_t rv) {
  ssize_t buffer_size =
      buf->buffer_end - buf->buffer_begin;             // Total allocated size
  ssize_t data_size = buf->data_end - buf->data_begin; // Current data size

  if (data_size + rv > buffer_size) {
    ssize_t new_size = data_size + rv;
    uint8_t *new_buf = new uint8_t[new_size];

    memcpy(new_buf, buf->data_begin, data_size);

    delete[] buf->buffer_begin;
    buf->buffer_begin = new_buf;
    buf->buffer_end = new_buf + new_size;
    buf->data_begin = new_buf;
    buf->data_end = new_buf + data_size;
  }

  memcpy(buf->data_end, data, rv);
  buf->data_end += rv;
}

static void buf_consume(Buffer *buf, ssize_t len) {
  if (len > (ssize_t)(buf->data_end - buf->data_begin)) {
    buf->data_begin = buf->buffer_begin;
    buf->data_end = buf->buffer_begin;
  } else {
    buf->data_begin += len;
  }
  if (buf->data_begin == buf->data_end) {
    buf->data_begin = buf->buffer_begin;
    buf->data_end = buf->buffer_begin;
  }
}

const size_t k_max_args = 200 * 1000;

static bool read_u32(const uint8_t *&cur, const uint8_t *end, uint32_t &out) {
  if (cur + 4 > end) {
    return false;
  }
  memcpy(&out, cur, 4);
  cur += 4;
  return true;
}

static bool read_str(const uint8_t *&cur, const uint8_t *end, size_t n,
                     std::string &out) {
  if (cur + n > end) {
    return false;
  }
  out.assign(cur, cur + n);
  cur += n;
  return true;
}

static int32_t parse_req(const uint8_t *data, size_t size,
                         std::vector<std::string> &out) {
  const uint8_t *end = data + size;
  uint32_t nstr = 0;
  if (!read_u32(data, end, nstr)) {
    return -1;
  }
  if (nstr > k_max_args) {
    return -1; // safety limit
  }

  while (out.size() < nstr) {
    uint32_t len = 0;
    if (!read_u32(data, end, len)) {
      return -1;
    }
    out.push_back(std::string());
    if (!read_str(data, end, len, out.back())) {
      return -1;
    }
  }
  if (data != end) {
    return -1; // trailing garbage
  }
  return 0;
}
static std::map<std::string, std::string> g_data;

static void do_request(std::vector<std::string> &cmd, Response &out) {
  if (cmd.size() == 2 && cmd[0] == "get") {
    auto it = g_data.find(cmd[1]);
    if (it == g_data.end()) {
      out.status = RES_NX; // not found
      return;
    }
    const std::string &val = it->second;
    out.data.assign(val.begin(), val.end());
  } else if (cmd.size() == 3 && cmd[0] == "set") {
    g_data[cmd[1]].swap(cmd[2]);
  } else if (cmd.size() == 2 && cmd[0] == "del") {
    g_data.erase(cmd[1]);
  } else {
    out.status = RES_ERR; // unrecognized command
  }
}

static void make_response(const Response &resp, Buffer *out) {
  uint32_t resp_len = 4 + (uint32_t)resp.data.size();
  buf_append(out, (const uint8_t *)&resp_len, 4);
  buf_append(out, (const uint8_t *)&resp.status, 4);
  buf_append(out, resp.data.data(), resp.data.size());
}

static bool try_one_request(Conn *conn) {
  if ((conn->incoming.data_end - conn->incoming.data_begin) < 4) {
    return false; // want read
  }
  uint32_t len = 0;
  memcpy(&len, conn->incoming.data_begin, 4);
  if (len > k_max_msg) { // protocol error
    conn->want_close = true;
    return false; // want close
  }
  // Protocol: message body
  if (4 + len > conn->incoming.data_end - conn->incoming.data_begin) {
    return false; // want read
  }
  const uint8_t *request = conn->incoming.data_begin + 4;
  // 4. Process the parsed message.
  // ...
  // generate the response (echo)
  std::vector<std::string> cmd;
  if (parse_req(request, len, cmd) < 0) {
    conn->want_close = true;
    return false; // error
  }
  Response resp;
  do_request(cmd, resp);
  make_response(resp, &conn->outgoing);
  buf_append(&conn->outgoing, (const uint8_t *)&len, 4);
  buf_append(&conn->outgoing, request, len);
  // 5. Remove the message from `Conn::incoming`.
  buf_consume(&conn->incoming, 4 + len);
  return true; // success
}

static void handle_write(Conn *conn) {
  assert((conn->outgoing.data_end - conn->outgoing.data_begin) > 0);
  ssize_t rv = write(conn->fd, conn->outgoing.data_begin,
                     conn->outgoing.data_end - conn->outgoing.data_begin);
  if (rv < 0 && errno == EAGAIN) {
    return;
  }
  if (rv < 0) {
    conn->want_close = true; // error handling
    return;
  }
  // remove written data from `outgoing`
  buf_consume(&conn->outgoing, (size_t)rv);
  if ((conn->outgoing.data_end - conn->outgoing.data_begin) ==
      0) { // has a response
    conn->want_read = true;
    conn->want_write = false;
  } else {
    conn->want_read = false;
    conn->want_write = true;
  }
}

static void handle_read(Conn *conn) {
  // 1. Do a non-blocking read.
  uint8_t buf[64 * 1024];
  ssize_t rv = read(conn->fd, buf, sizeof(buf));
  if (rv < 0 && errno == EAGAIN) {
    return;
  }
  if (rv < 0) { // handle IO error (rv < 0)
    msg_errno("read error");
    conn->want_close = true;
    return;
  }

  if (rv == 0) {
    if ((conn->incoming.data_end - conn->incoming.data_begin) == 0) {
      msg("client closed");
    } else {
      msg("unexpected eof");
    }

    conn->want_close = true;
    return;
  }
  buf_append(&conn->incoming, buf, (size_t)rv);
  while (try_one_request(conn)) {
  }
  if ((conn->outgoing.data_end - conn->outgoing.data_begin) >
      0) { // has a response
    conn->want_read = false;
    conn->want_write = true;
  } else {
    conn->want_read = true;
    conn->want_write = false;
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
      if (conn->want_close || ready & POLLERR) {
        (void)close(conn->fd);
        fd2conn[conn->fd] = NULL;
        delete conn;
      }
    }
  }
}