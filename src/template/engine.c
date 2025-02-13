#include "engine.h"
#include <argp.h>
#include <bits/types/cookie_io_functions_t.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <unistd.h>

Task *queue = NULL;
Lib runtime = {0};
int max_event = 64;
int event_poll = 0;
int min_event = 64;
bool verbose = false;

int event_shutdown = 0;
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
  int client;
  struct sockaddr addr_client = {0};
  socklen_t addr_len = sizeof(addr_client);

  event_poll = epoll_create1(0);
  try(event_poll < 0, "create epoll failed");
  event_add(server);

  event_shutdown = eventfd(0, EFD_NONBLOCK);
  try(event_shutdown < 0, "create event failed");
  event_add(event_shutdown);

  struct epoll_event *events = malloc(max_event * sizeof(struct epoll_event));
  while (running) {
    int num_event = epoll_wait(event_poll, events, max_event, -1);

    for (int i = 0; i < num_event; i++) {
      if (events[i].data.fd == event_shutdown) {
        print("shutdown event triggered");
        running = 0;
        free(events);
        task_clean();
        close(server);
        close(event_poll);

        print("closing threads ...");

        pthread_cond_broadcast(&cond);
        for (int t = 0; t < thread; t++) {
          pthread_join(thread_pool[t], NULL);
        }
        dlclose(runtime.handle);
        exit(EXIT_SUCCESS);
      }
      if (events[i].data.fd == server) {

        client = accept(server, &addr_client, &addr_len);
        print("new connection!");
        event_add(client);
      } else {
        pthread_mutex_lock(&mutex);
        task_add(events[i].data.fd);
        pthread_cond_signal(&cond);
        pthread_mutex_unlock(&mutex);
      }
    }
    if (num_event == max_event) {
      max_event *= 2;
      print("expanding event pool ...");
      events = realloc(events, max_event * sizeof(struct epoll_event));
    } else if (num_event < max_event >> 1 && (max_event / 2) > min_event) {
      max_event /= 2;
      print("shrinking event pool to ...");
      events = realloc(events, max_event * sizeof(struct epoll_event));
    }
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
void event_nonblock(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  fcntl(fd, F_SETFD, flags | O_NONBLOCK);
}

void event_add(int fd) {
  struct epoll_event event;
  event.data.fd = fd;
  event.events = EPOLLIN | EPOLLET;
  event_nonblock(fd);
  epoll_ctl(event_poll, EPOLL_CTL_ADD, fd, &event);
}
void task_clean(void) {
  Task *t = NULL;
  while (queue) {
    t = queue;
    queue = queue->next;
    free(t);
  }
}
void task_add(int client_fd) {
  Task *task = (Task *)malloc(sizeof(Task));
  task->client_fd = client_fd;
  task->next = NULL;
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
void *work(void *unused) {
  (void)unused;
  Task task;
  while (running) {
    pthread_mutex_lock(&mutex);
    while (queue == NULL) {
      pthread_cond_wait(&cond, &mutex);
      if (!running) {
        pthread_mutex_unlock(&mutex);
        return NULL;
      }
    }
    task = *queue;
    task_remove(queue);
    pthread_mutex_unlock(&mutex);
    runtime.function(task);
    print("closing connection ...");
    close(task.client_fd);
  }
  return NULL;
}
Lib load(const char *filename) {
  print("loading runtime file ...");
  Lib cache;
  cache.handle = dlopen(filename, RTLD_NOW);

  char *error = dlerror();
  try(error != NULL, error);

  print("loading runtime function ...");
  union {
    void *ptr;
    task_handler handler;
  } converter;
  converter.ptr = dlsym(cache.handle, "handle");
  cache.function = converter.handler;
  error = dlerror();

  try(error != NULL, error);
  try(!cache.function, "null function");
  return cache;
}

void print(const char *msg) {
  if (verbose) {
    printf("\r[\033[31mNotice\033[0m]: %s\n", msg);
  }
}

void try(int err, const char *msg) {
  if (err) {
    printf("\r[\033[31mError\033[0m]: %s\n", msg);
    exit(EXIT_FAILURE);
  }
}
int parse(int key, char *arg, struct argp_state *unused) {
  (void)unused;
  static int port = 0, thread = 0;
  static const char *runtime_path = "./runtime.btx";
  switch (key) {
  case 'p': port = atoi(arg); break;
  case 't': thread = atoi(arg); break;
  case 'f': runtime_path = arg; break;
  case 'v': verbose = true; break;
  case 'm':
    min_event = atoi(arg);
    if (min_event < 2) {
      printf("\r[\033[31mError\033[0m]: min event must be greater than 1\n");
      exit(EXIT_FAILURE);
    }
    break;
  case ARGP_KEY_END:
    thread = thread == 0 ? sysconf(_SC_NPROCESSORS_ONLN) : thread;
    runtime = load(runtime_path);
    server(port, thread);
    break;
  }
  return 0;
}
void signal_hanler(int unused) {
  (void)unused;
  uint64_t val = 1;
  write(event_shutdown, &val, sizeof(val));
  printf("\r[\033[34mNotice\033[0m]: stoping server please wait ...\n");
}
int main(int argc, char **argv) {
  signal(SIGTERM, &signal_hanler);
  signal(SIGINT, &signal_hanler);
  struct argp argp = {options, parse, 0, doc, 0, 0, 0};
  return argp_parse(&argp, argc, argv, 0, 0, 0);
}
