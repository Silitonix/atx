#include "main.h"
#include <string.h>
#include <unistd.h>

void handle(t_task task) {
    char buffer[1024];
    // Read the request (ignored for simplicity)
    read(task.client_fd, buffer, sizeof(buffer));

    // Prepare the HTTP response
    const char *response =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/plain\r\n"
        "Content-Length: 13\r\n"
        "\r\n"
        "Hello, World!";
    write(task.client_fd, response, strlen(response));
}
