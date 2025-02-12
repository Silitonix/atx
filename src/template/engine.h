#ifndef H_ENGINE
#define H_ENGINE

typedef struct _task {
  int client_fd;
  struct _task *prev;
  struct _task *next;
} Task;

typedef void (*TaskHandler)(Task);

typedef struct _lib {
  void *handle;
  TaskHandler function;
} Lib;

void event_add(int fd);
void task_add(int client_fd);
void task_remove(Task *task);
void try(int err, const char *msg);
void *work(void *_);
void server(int port, int thread);
int listen_socket(int port);

#endif
