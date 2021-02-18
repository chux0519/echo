#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <event2/event.h>
#include <event2/listener.h>
#include <event2/util.h>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/un.h>
#include <unistd.h>

static void conn_readcb(struct bufferevent *bev, void *user_data) {
  struct evbuffer *input = bufferevent_get_input(bev);
  struct evbuffer *output = bufferevent_get_output(bev);

  size_t len = evbuffer_get_length(input);
  unsigned char *msg = evbuffer_pullup(input, len);

  evbuffer_add(output, msg, len);

  evbuffer_drain(input, len);
}

static void conn_eventcb(struct bufferevent *bev, short events,
                         void *user_data) {
  if (events & BEV_EVENT_EOF) {
    printf("Connection closed.\n");
  } else if (events & BEV_EVENT_ERROR) {
    printf("Got an error on the connection: %s\n",
           strerror(errno)); /*XXX win32*/
  }
  /* None of the other events can happen here, since we haven't enabled
   * timeouts */
  bufferevent_free(bev);
}

static void accept_error_cb(struct evconnlistener *listener, void *ctx) {
  struct event_base *base = ctx;

  int err = EVUTIL_SOCKET_ERROR();

  fprintf(stdout,
          "Got an error %d (%s) on the listener."
          "Shutting down \n",
          err, evutil_socket_error_to_string(err));

  event_base_loopexit(base, NULL);
}

static void accept_conn_cb(struct evconnlistener *listener, int fd,
                           struct sockaddr *address, int socklen, void *ctx) {
  struct event_base *base = ctx;
  struct bufferevent *bev;

  fprintf(stdout, "connected\n");
  bev = bufferevent_socket_new(base, fd, BEV_OPT_CLOSE_ON_FREE);
  if (!bev) {
    fprintf(stderr, "Error constructing bufferevent!");
    event_base_loopbreak(base);
    return;
  }
  bufferevent_setcb(bev, conn_readcb, NULL, conn_eventcb, base);
  bufferevent_enable(bev, EV_READ);
}

static void udp_readcb(int fd, short event, void *ctx) {
  struct event *evt = ctx;

  char buf[4096];
  socklen_t size = sizeof(struct sockaddr);
  struct sockaddr_in client_addr = {0};
  int len =
      recvfrom(fd, buf, sizeof(buf), 0, (struct sockaddr *)&client_addr, &size);
  if (0 == len) {
    fprintf(stderr, "connection closed\n");
    event_free(evt);
    evt = NULL;
  } else if (len > 0) {
    sendto(fd, buf, len, 0, (struct sockaddr *)&client_addr, size);
  }
}

int main(int argc, char **argv) {
  int opt = 0;
  int port = 10086;
  while ((opt = getopt(argc, argv, "p:")) != -1) {
    switch (opt) {
    case 'p':
      port = atoi(optarg);
      break;
    }
  }

  int err = 0;

  struct sockaddr_in sin_tcp, sin_udp = {0};

  sin_tcp.sin_family = AF_INET;
  sin_udp.sin_family = AF_INET;

  err = inet_pton(AF_INET, "0.0.0.0", &sin_tcp.sin_addr);
  err = inet_pton(AF_INET, "0.0.0.0", &sin_udp.sin_addr);

  if (err < 0) {
    perror("inet_pton");
    exit(EXIT_FAILURE);
  }

  sin_tcp.sin_port = htons(port);
  sin_udp.sin_port = htons(port);

  int fd_tcp = socket(AF_INET, SOCK_STREAM, 0);
  int fd_udp = socket(AF_INET, SOCK_DGRAM, 0);

  int reuse_port = 1;

  err = setsockopt(fd_tcp, SOL_SOCKET, SO_REUSEPORT, (const void *)&reuse_port,
                   sizeof(int));
  err = setsockopt(fd_udp, SOL_SOCKET, SO_REUSEPORT, (const void *)&reuse_port,
                   sizeof(int));
  if (err < 0) {
    perror("setsockopt");
    return err;
  }

  int flag = fcntl(fd_tcp, F_GETFL, 0);
  fcntl(fd_tcp, F_SETFL, flag | O_NONBLOCK);

  flag = fcntl(fd_udp, F_GETFL, 0);
  fcntl(fd_udp, F_SETFL, flag | O_NONBLOCK);

  err = bind(fd_tcp, (struct sockaddr *)&sin_tcp, sizeof(sin_tcp));
  err = bind(fd_udp, (struct sockaddr *)&sin_udp, sizeof(sin_udp));
  if (err < 0) {
    perror("bind");
    return err;
  }

  struct event_base *base;
  struct evconnlistener *listener;
  struct event *udp_event = NULL;

  base = event_base_new();
  if (!base) {
    fprintf(stderr, "Could not initialize libevent!\n");
    return 1;
  }

  listener =
      evconnlistener_new(base, accept_conn_cb, base,
                         LEV_OPT_CLOSE_ON_FREE | LEV_OPT_REUSEABLE, -1, fd_tcp);
  evconnlistener_set_error_cb(listener, accept_error_cb);

  // for udp, construct event and add that by hand
  udp_event =
      event_new(base, fd_udp, EV_READ | EV_PERSIST, udp_readcb, udp_event);
  event_add(udp_event, NULL);

  if (!listener) {
    fprintf(stderr, "Could not create a listener!\n");
    return 1;
  }

  signal(SIGPIPE, SIG_IGN);

  event_base_dispatch(base);

  evconnlistener_free(listener);

  if (fd_udp != 0)
    close(fd_udp);

  event_base_free(base);

  printf("done\n");
  return 0;
}
