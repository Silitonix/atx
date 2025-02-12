#include "engine.h"
#include <argp.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

Task *queue = {0};
Lib runtime = {0};
int max_event = 64;
int event_poll = 0;

volatile sig_atomic_t running = 1;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond = PTHREAD_COND_INITIALIZER;

void server(int port, int thread) {
  pthread_t thread_pool[thread];
  for (int t = 0; t < thread; t++) {
    try(pthread_create(&thread_pool[t], NULL, &work, NULL) < 0,
        "create thread failed");
  }
  int server = listen_socket(port);
  event_poll = epoll_create1(0);
  try(event_poll < 0, "create epoll failed");
  event_add(server);

  struct epoll_event *events =
      (struct epoll_event *)malloc(max_event * sizeof(struct epoll_event));
  while (running) {
    int num_event = epoll_wait(event_poll, events, max_event, -1);

    for (int i = 0; i < num_event; i++) {
      if (events[i].data.fd == server) {
        int client;
        struct sockaddr addr_client = {0};
        socklen_t addr_len = sizeof(addr_client);
        while ((client = accept(server, &addr_client, &addr_len)) > -1) {
          event_add(client);
        }
      } else {
        pthread_mutex_lock(&mutex);
        task_add(events[i].data.fd);
        pthread_cond_signal(&cond);
        pthread_mutex_unlock(&mutex);
      }
    }

    if (num_event == max_event) {
      max_event += 64;
      events = (struct epoll_event *)realloc(
          events, max_event * sizeof(struct epoll_event));
    }
  }

  free(events);
  close(server);
  close(event_poll);
  for (int t = 0; t < thread; t++) {
    pthread_join(thread_pool[t], NULL);
  }
}
int listen_socket(int port) {
  int err = 0;
  int opt = 1;
  struct sockaddr_in addr_server = {0};
  socklen_t addr_len = sizeof(addr_server);
  addr_server.sin_family = AF_INET;
  addr_server.sin_addr.s_addr = INADDR_ANY;
  addr_server.sin_port = htons(port);

  int fd_server = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
  try(fd_server < 0, "failed creating socket");

  err = setsockopt(fd_server, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
  try(err < 0, "failed setting socket option");

  err = bind(fd_server, (struct sockaddr *)&addr_server, sizeof(addr_server));
  try(err < 0, "failed to bind the address");

  err = listen(fd_server, SOMAXCONN);
  try(err < 0, "failed to listen");

  if (port == 0) {
    err = getsockname(fd_server, (struct sockaddr *)&addr_server, &addr_len);
    try(err < 0, "getting new name");
    port = ntohs(addr_server.sin_port);
  }

  printf("\r[\033[34mNotice\033[0m]: listening on http://0.0.0.0:%d\n", port);

  return fd_server;
}

void event_add(int fd) {
  struct epoll_event event;
  event.data.fd = fd;
  event.events = EPOLLIN | EPOLLET;
  int flags = fcntl(fd, F_GETFL, 0);
  fcntl(fd, F_SETFD, flags | O_NONBLOCK);
  epoll_ctl(event_poll, EPOLL_CTL_ADD, fd, &event);
}

void task_add(int client_fd) {
  Task *task = (Task *)malloc(sizeof(Task));
  task->client_fd = client_fd;
  if (queue == NULL) {
    queue = task;
    task->prev = task;
  } else {
    queue->prev->next = task;
    task->prev = queue->prev;
    queue->prev = task;
  }
}
void task_remove(Task *task) {
  if (task->prev == task) {
    queue = NULL;
  } else {
    task->prev->next = task->next;
    task->next->prev = task->prev;
    if (queue == task) {
      queue = task->next;
    }
  }
  free(task);
}
void *work(void *_) {
  Task task;
  while (running) {
    pthread_mutex_lock(&mutex);
    while (queue == NULL && running) {
      pthread_cond_wait(&cond, &mutex);
    }
    task = *queue;
    task_remove(queue);
    pthread_mutex_unlock(&mutex);
    runtime.function(task);
    close(task.client_fd);
  }
  return NULL;
}

Lib load(const char *filename) {
  Lib cache;
  cache.handle = dlopen(filename, RTLD_NOW);

  char *error = dlerror();
  try(error != NULL, error);

  cache.function = (TaskHandler)dlsym(cache.handle, "handle");
  error = dlerror();

  try(error != NULL, error);
  try(!cache.function, "null function");
  return cache;
}

void try(int err, const char *msg) {
  if (err) {
    printf("\r[\033[31mError\033[0m]: %s\n", msg);
    exit(EXIT_FAILURE);
  }
}
int parse(int key, char *arg, struct argp_state *_) {
  static int port = 0, thread = 0;
  static const char *runtime_path = "./runtime.btx";
  switch (key) {
  case 'p': port = atoi(arg); break;
  case 't': thread = atoi(arg); break;
  case 'f': runtime_path = arg; break;
  case ARGP_KEY_END:
    thread = thread == 0 ? sysconf(_SC_NPROCESSORS_ONLN) : thread;
    runtime = load(runtime_path);
    server(port, thread);
    dlclose(runtime.handle);
    break;
  }
  return 0;
}
void signal_hanler(int _) {
  running = 0;
  printf("\r[\033[34mNotice\033[0m]: stoping server please wait ...\n");
}
int main(int argc, char **argv) {
  signal(SIGTERM, &signal_hanler);
  signal(SIGINT, &signal_hanler);
  const struct argp_option options[] = {
      {"port", 'p', "NUM", 0, "Start a server with desired port", 0},
      {"thread", 't', "NUM", 0, "Start a server with desired thread", 0},
      {"file", 'f', "FILENAME", 0, "Runtime file to load", 0},
      {0}};
  struct argp argp = {options, parse, 0, 0, 0, 0, 0};
  return argp_parse(&argp, argc, argv, 0, 0, 0);
}
