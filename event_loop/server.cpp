#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/ip.h>
#include <vector>


const size_t k_max_msg = 4096;

static void msg(const char *msg) {
    fprintf(stderr, "%s\n", msg);
}

enum {
    STATE_REQ = 0,
    STATE_RES = 1,
    STATE_END = 2,
};

struct Conn {
    int fd = -1; 
    uint32_t state = 0; // will either be in request or response state
    // buffer for reading 
    size_t rbuf_size = 0;
    uint8_t rbuf[4 + k_max_msg];
    // buffer for writing
    size_t wbuf_size = 0;
    size_t wbuf_sent = 0;
    uint8_t wbuf[4 + k_max_msg];
};

static void die(const char *msg) {
    int err = errno;
    fprintf(stderr, "[%d] %s\n", err, msg);
    abort();
}

static int32_t read_full (int fd, char *buf, size_t n) {
    while (n > 0) {
        ssize_t rv = read(fd, buf, n);
        if (rv <= 0) {
            return -1;
        }
        assert((size_t)rv <= n);
        n -= (size_t)rv;
        buf += rv;
    }

    return 0;
}

static int32_t write_all (int fd, const char *buf, size_t n) {
    while (n > 0) {
        ssize_t rv = write(fd, buf, n);
        if (rv <= 0) {
            return -1;
        }
        assert((size_t)rv <= n);
        n -= (size_t)rv;
        buf += rv;
    }

    return 0;
}

static int32_t one_request (int connfd) {
    // 4 bytes header
    char rbuf[4 + k_max_msg + 1];
    errno = 0;
    // extract the length of the request
    int32_t err = read_full(connfd, rbuf, 4);
    if (err) {
        if (errno == 0) {
            msg("EOF");
        } else {
            msg("read() error");
        }

        return err;
    }

    // pull the length of the request out to grab
    uint32_t len = 0;
    memcpy(&len, rbuf, 4); // assume little endian
    if (len > k_max_msg) {
        msg("too long");
        return -1;
    }

    // request body
    err = read_full(connfd, &rbuf[4], len);
    if (err) {
        msg("read() error");
        return err;
    }

    // write message
    rbuf[4 + len] = '\0';
    printf("client says: %s\n", &rbuf[4]);

    // reply using the same protocol
    const char reply[] = "world";
    char wbuf[4 + sizeof(reply)];
    len = (uint32_t)strlen(reply);
    memcpy(wbuf, &len, 4);
    memcpy(&wbuf[4], reply, len);
    return write_all(connfd, wbuf, 4 + len);
}

int main() {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    
    if (fd < 0) {
        die("socket()");
    }

    int val = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));

    struct sockaddr_in addr = {};

    addr.sin_family = AF_INET;
    addr.sin_port = ntohs(1234);
    addr.sin_addr.s_addr = ntohl(0);
    int rv = bind(fd, (const sockaddr *) &addr, sizeof(addr));

    if (rv) {
        die("bind()");
    }

    rv = listen(fd, SOMAXCONN);
    if (rv) {
        die("listen()");
    }

    rv = listen(fd, SOMAXCONN);

    std::vector<Conn *> fd2conn;

    std::vector<struct pollfd> poll_args;
    while(true) {
        // prepare the arguments of the poll()
        poll_args.clear();
        // for convenience, the listening fd is put in the first position
        struct pollfd pfd = {fd, POLLIN, 0};
        poll_args.push_back(pfd);
        // connection fds
        for (Conn *conn : fd2conn) {
            if (!conn) {
                continue;
            }
            struct pollfd pfd = {};
            pfd.fd = conn -> fd;
            pfd.events = (conn -> state == STATE_REQ) ? POLLIN : POLLOUT;
            pfd.events = pfd.events | POLLERR;
            poll_args.push_back(pfd);
        }

        // poll for active fds
        // the timeout argument 
        int rv = poll(poll_args.data(), (nfds_t)poll_args.size(), 1000);
        if (rv < 0) {
            die("poll");
        }

        // process active connections
        for (size_t i = 1; i < poll_args.size(); i++) {
            if (poll_args[i].revents) {
                Conn *conn = f2dconn[poll_args[i].fd];
                connection_io(conn);
                if (conn -> state == STATE_END) {
                    // close the client or destroy if something bad happened
                    f2dconn[conn -> fd] = NULL;
                    (void)close(conn -> fd);
                    free(conn);
                }
            }
        }
        
    }
}
