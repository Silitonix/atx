#include "main.h"
#include <argp.h>
#include <arpa/inet.h>
#include <asm-generic/socket.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
t_task task_queue[256];
int num_task = 0;

void *thread_pool_worker(void *arg);

int Server(int port, int num_thread) {
  int err = 0;
  // initialize thread pool
  pthread_t thread_pool[num_thread];
  for (int i = 0; i < num_thread; i++) {
    pthread_create(&thread_pool[i], NULL, thread_pool_worker, NULL);
  }

  // initialize server
  // 1 - create socket
  // 2 - bind address
  // 3 - listen socket
  // 4 - get new address
  //
  int fd_server, fd_client;
  struct sockaddr_in addr_server, addr_client;
  addr_server.sin_family = AF_INET;
  addr_server.sin_addr.s_addr = INADDR_ANY;
  addr_server.sin_port = port;

  socklen_t addr_len = sizeof(addr_server);

  err = fd_server = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
  err_check(err, "failed creating socket");

  err = bind(fd_server, (struct sockaddr *)&addr_server, addr_len);
  err_check(err, "failed to bind the address");

  err = listen(fd_server, SOMAXCONN);
  err_check(err, "failed to listen");

  if (port == 0) {
    err = getsockname(fd_server, (struct sockaddr *)&addr_server, &addr_len);
    err_check(err, "failed get system associated port");
  }

  printf("Server listening on http://0.0.0.0:%d", addr_server.sin_port);

  return 0;
}

// thread worker function
void *thread_pool_worker(void *arg) {
  while (1) {
    pthread_mutex_lock(&mutex);
    while (num_task == 0) {
      pthread_cond_wait(&cond, &mutex);
    }

    t_task task = task_queue[--num_task];
    pthread_mutex_unlock(&mutex);
    close(task.client_fd);
  }
  return NULL;
}

static int parse_opt(int key, char *arg, struct argp_state *state) {
  static int port = 0;
  static int num_thread = 0;

  switch (key) {
  case 'p':
    port = atoi(arg);
    break;
  case 't':
    num_thread = atoi(arg);
    break;
  case ARGP_KEY_END:
    if (state->arg_num == 0) {
      if (num_thread == 0)
        num_thread = sysconf(_SC_NPROCESSORS_ONLN);
      Server(port, num_thread);
    }
    break;
  }
  return 0;
}

int main(int argc, char **argv) {
  struct argp_option options[] = {
      {"port", 'p', "NUM", 0, "Start a server with desired port", 0},
      {"thread", 't', "NUM", 0, "Start a server with desired thread", 0},
      {0}};
  struct argp args = {options, parse_opt};
  return argp_parse(&args, argc, argv, 0, 0, 0);
}
