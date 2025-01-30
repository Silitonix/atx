#ifndef _PROGRAM_
#define _PROGRAM_

typedef struct s_task {
  int client_fd;
} t_task;

const char *argp_program_version = "0.0.0";
const char *argp_program_bug_address = "http://github.com/Silitonix/atx/issue";

void *thread_pool_worker(void *arg);

#define err_check(value, msg)                                                  \
  if ((value) < 0) {                                                           \
    perror(msg);                                                               \
    exit(EXIT_FAILURE);                                                        \
  }

#define null_check(value, msg)                                                 \
  if ((value) == NULL) {                                                       \
    perror(msg);                                                               \
    exit(EXIT_FAILURE);                                                        \
  }

#endif
