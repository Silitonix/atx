#include <string.h>
#include <unistd.h>

typedef struct _task {
  int client_fd;
  struct _task *prev;
  struct _task *next;
} Task;


void handle(Task task) {
    char buffer[1024];
    // Read the request (ignored for simplicity)
    read(task.client_fd, buffer, sizeof(buffer));

    // Prepare the HTTP response
    const char *response =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/html\r\n"
        "\r\n"
        "<h1>Hello, World!</h1>";
    write(task.client_fd, response, strlen(response));
}
