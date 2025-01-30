#include "main.h"
#include <argp.h>
#include <arpa/inet.h>
#include <asm-generic/socket.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <linux/limits.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#define MAX_EVENTS 10000
#define TASK_QUEUE_SIZE 256

volatile sig_atomic_t running = 1;
int num_task = 0;
int fd_epoll;
int fd_server;

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
t_task task_queue[256];

void *thread_pool_worker(void *arg);
void (*handle)(t_task task);

void add_to_epoll(int fd) {
  struct epoll_event event;
  event.data.fd = fd;
  event.events = EPOLLIN | EPOLLET;
  int flags = fcntl(fd, F_GETFL, 0);
  fcntl(fd, F_SETFD, flags | O_NONBLOCK);
  epoll_ctl(fd_epoll, EPOLL_CTL_ADD, fd, &event);
}

int server(int port, int num_thread) {
  int err = 0;
  pthread_t thread_pool[num_thread];
  for (int i = 0; i < num_thread; i++) {
    err = pthread_create(&thread_pool[i], NULL, thread_pool_worker, NULL);
    err_check(err, "create thread failed");
  }

  int fd_client;
  struct sockaddr_in addr_server, addr_client;
  memset(&addr_server, 0, sizeof(addr_server));
  addr_server.sin_family = AF_INET;
  addr_server.sin_addr.s_addr = INADDR_ANY;
  addr_server.sin_port = htons(port);

  socklen_t addr_len = sizeof(addr_client);

  fd_server = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
  err_check(fd_server, "failed creating socket");

  int opt = 1;
  setsockopt(fd_server, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

  err = bind(fd_server, (struct sockaddr *)&addr_server, sizeof(addr_server));
  err_check(err, "failed to bind the address");

  err = listen(fd_server, SOMAXCONN);
  err_check(err, "failed to listen");

  if (port == 0) {
    err = getsockname(fd_server, (struct sockaddr *)&addr_server, &addr_len);
    err_check(err, "getting new name");
    port = ntohs(addr_server.sin_port);
  }

  fd_epoll = epoll_create1(0);
  err_check(fd_epoll, "create epoll");
  add_to_epoll(fd_server);

  struct epoll_event events[MAX_EVENTS];

  printf("\033[0;30;42mPROCESS\033[0m: listening on http://0.0.0.0:%d\n", port);

  while (running) {
    int num_events = epoll_wait(fd_epoll, events, MAX_EVENTS, -1);
    for (int i = 0; i < num_events; i++) {
      if (events[i].data.fd == fd_server) {
        while ((fd_client = accept(fd_server, (struct sockaddr *)&addr_client,
                                   &addr_len)) > 0) {
          add_to_epoll(fd_client);
        }
      } else {
        pthread_mutex_lock(&mutex);
        if (num_task < TASK_QUEUE_SIZE) {
          task_queue[num_task].client_fd = events[i].data.fd;
          num_task++;
          pthread_cond_signal(&cond);
        }
        pthread_mutex_unlock(&mutex);
      }
    }
  }
  return 0;
}
void load_runtime(const char *file_name) {
  void *dl = dlopen(file_name, RTLD_NOW);
  null_check(dl, "runtime not found");
  handle = (void (*)(t_task))dlsym(dl, "handle");
  if (dlerror() != NULL) {
     perror("\033[0;30;41mERROR\033[0m: invalid .\n");
    exit(EXIT_FAILURE);
  }
  printf("\033[0;30;42mRUNTIME\033[0m: runtime loaded.\n");
}

// thread worker function
void *thread_pool_worker(void *_) {
  while (1) {
    pthread_mutex_lock(&mutex);
    while (num_task == 0) {
      pthread_cond_wait(&cond, &mutex);
    }

    t_task task = task_queue[--num_task];
    pthread_mutex_unlock(&mutex);
    handle(task);
    close(task.client_fd);
  }
  return NULL;
}

static int parse_opt(int key, char *arg, struct argp_state *_) {
  static int port = 0, num_thread = 0;
  static const char *file_runtime = "./build.btx";

  switch (key) {
  case 'p':port = atoi(arg);break;
  case 't':num_thread = atoi(arg);break;
  case 'f':file_runtime = arg;break;
  case ARGP_KEY_END:
    num_thread = num_thread == 0 ? sysconf(_SC_NPROCESSORS_ONLN) : num_thread;
    load_runtime(file_runtime);
    server(port, num_thread);
    break;
  }
  return 0;
}

void signal_handler(int _) {
  printf("\033[0;30;43mCLEANING\033[0m: closing up ...\n");
  running = 0;
  close(fd_server);
  close(fd_epoll);
}

int main(int argc, char **argv) {
  signal(SIGINT, signal_handler);
  signal(SIGTERM, signal_handler);
  struct argp_option options[] = {
      {"port", 'p', "NUM", 0, "Start a server with desired port", 0},
      {"thread", 't', "NUM", 0, "Start a server with desired thread", 0},
      {"file", 'f', "FILENAME", 0, "Runtime file to load", 0},
      {0}};
  struct argp args = {options, parse_opt};
  return argp_parse(&args, argc, argv, 0, 0, 0);
}
