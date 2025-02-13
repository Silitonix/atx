#ifndef H_ENGINE
#define H_ENGINE

#include <argp.h>

const char *argp_program_version = "0.1";
const char *argp_program_bug_address =
    "<https://github.com/Silitonix/atx/issue>";
static char *doc = "Atx - @x bare metal template engine";
static struct argp_option options[] = {
    {"port", 'p', "PORT", 0, "Port to listen on", 0},
    {"thread", 't', "THREAD", 0, "Number of threads to use", 0},
    {"file", 'f', "RUNTIME", 0, "Path to runtime library", 0},
    {"verbos", 'v', 0, 0, "more detail", 0},
    {"max_event", 'm', "NUM", 0, "minimum event pull array -64-", 0},
    {0}};

typedef struct _task {
  int client_fd;
  struct _task *prev;
  struct _task *next;
} Task;

typedef void (*task_handler)(Task);

typedef struct _lib {
  void *handle;
  task_handler function;
} Lib;

void print(const char *msg);
void event_add(int fd);
void task_add(int client_fd);
void task_remove(Task *task);
void task_clean(void);
void try(int err, const char *msg);
void *work(void *_);
void server(int port, int thread);
int listen_socket(int port);

#endif
