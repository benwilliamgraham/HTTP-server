// quickhost
//
// Custom http server
//
// Ben Graham
// benwilliamgraham@gmail.com

#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

// stores client connection information
struct client {
  int fd;
};

// stores server connection information
struct server {
  int port, fd;
} server = {.port = 8000};

// MIME types
struct mimeType {
  char *extension, *content_type;
} mimeTypes[] = {
    {.extension = ".avi", .content_type = "video/x-msvideo"},
    {.extension = ".bin", .content_type = "application/octet-stream"},
    {.extension = ".bmp", .content_type = "image/bmp"},
    {.extension = ".css", .content_type = "text/css"},
    {.extension = ".gif", .content_type = "image/gif"},
    {.extension = ".html", .content_type = "text/html"},
    {.extension = ".ico", .content_type = "image/vnd.microsoft.icon"},
    {.extension = ".jpg", .content_type = "image/jpeg"},
    {.extension = ".jpeg", .content_type = "image/jpeg"},
    {.extension = ".js", .content_type = "text/javascript"},
    {.extension = ".json", .content_type = "application/json"},
    {.extension = ".mpeg", .content_type = "video/mpeg"},
    {.extension = ".png", .content_type = "image/png"}};

// given a connected client, fulfill requests
// returns 1 if successful, 0 otherwise
int serve(struct client client);

// fulfill a GET request
// returns the number of bytes sent or 0 if could not complete request
int serve_get(struct client client, char *request);

// io stuff
static char *NUM = "\033[93m", *STR = "\033[90m", *BYTES = "\033[92m",
            *NONE = "\033[0m";

int serve(struct client client) {
  int requestBytes;
  char request[1024];
  if ((requestBytes = read(client.fd, request, sizeof(request) - 1))) {
    request[requestBytes] = '\0';
    printf("(%s%d%s Bytes)>:\n%s%s%s\n", NUM, requestBytes, NONE, STR, request,
           NONE);

    // determine the type of request
    if (!strncmp(request, "GET", 3)) {
      return (serve_get(client, request) && 1);
    } else {
      fprintf(stderr, "Error: unknown request format\n");
      return 0;
    }
  }
  fprintf(stderr, "Error: unnable to read request\n");
  return 0;
}

int serve_get(struct client client, char *request) {
  // retrieve filename from request
  if ((request = strchr(request, '/')) == NULL) {
    fprintf(stderr, "Error: no filepath provided\n");
    return 0;
  }
  request += 1;
  char *filenameEnd;
  if ((filenameEnd = strchr(request, ' ')) == NULL &&
      (filenameEnd = strchr(request, '\r')) == NULL) {
    fprintf(stderr, "Error: improper filename provided\n");
    return 0;
  }
  int filenameBytes = filenameEnd - request;
  if (!filenameBytes) {
    request = "index.html";
    filenameBytes = strlen(request);
    filenameEnd = request + filenameBytes;
  }
  char filename[filenameBytes + 1];
  strncpy(filename, request, filenameBytes);
  filename[filenameBytes] = '\0';

  // determine the MIME type of the file
  char *content_type = NULL;
  for (int i = 0; i < (sizeof(mimeTypes) / sizeof(mimeTypes[0])); i++) {
    int extensionBytes = strlen(mimeTypes[i].extension);
    if (extensionBytes > strlen(filenameEnd - extensionBytes) ||
        !strncmp(mimeTypes[i].extension, filenameEnd - extensionBytes,
                 extensionBytes)) {
      content_type = mimeTypes[i].content_type;
      break;
    }
  }
  if (content_type == NULL) {
    fprintf(stderr, "Error: unknown file extension\n");
    return 0;
  }

  // open file or send proper message
  FILE *file;
  if ((file = fopen(filename, "r")) == NULL) {
    fprintf(stderr, "unnable to open file %s\n", filename);
    char *message = "HTTP/1.1 404 Not Found\r\n";
    int messageBytes = strlen(message);
    if (send(client.fd, message, messageBytes, 0) != messageBytes)
      fprintf(stderr, "unnable to send failure message\n");
    return 0;
  }

  // get size of file
  fseek(file, 0L, SEEK_END);
  size_t fileBytes;
  if ((fileBytes = ftell(file)) <= 0) {
    fprintf(stderr, "Error: unnable to get file size\n");
    return 0;
  }
  rewind(file);
  printf("Found file is %s%zu%s bytes\n", NUM, fileBytes, NONE);

  // read to buffer and close
  char fileBuffer[fileBytes];
  if (fread(fileBuffer, sizeof(char), fileBytes, file) != fileBytes) {
    fprintf(stderr, "Error: unnable to read file\n");
    return 0;
  }
  fclose(file);

  // create message
  char *format = "\
HTTP/1.1 200 OK\r\n\
Server: quickhost\r\n\
Content-Length: %d\r\n\
Content-Type: %s\r\n\r\n";

  int headerBytes = snprintf(NULL, 0, format, fileBytes, content_type);
  int messageBytes = headerBytes + fileBytes;
  char message[messageBytes];
  snprintf(message, headerBytes + 1, format, fileBytes, content_type);
  message[headerBytes + 1] = '\0';
  printf("<(%s%d%s Bytes):\n%s%s%s[FILE]%s\n", NUM, messageBytes, NONE, STR,
         message, BYTES, NONE);

  // add file to message
  if (!memcpy(message + headerBytes, fileBuffer, fileBytes)) {
    fprintf(stderr, "Error: unnable to write file to message\n");
    return 0;
  }

  // send message
  if (send(client.fd, message, messageBytes, 0) != messageBytes) {
    fprintf(stderr, "Error: message failed to send\n");
    return 0;
  }

  return messageBytes;
}

int main(int argc, char **argv) {
  // parse arguments
  int opt;
  while ((opt = getopt(argc, argv, "p:")) != -1) {
    switch (opt) {
    case 'p':
      server.port = atoi(optarg);
      break;
    default:
      fprintf(stderr, "Usage: %s [-p port]\n", argv[0]);
      exit(EXIT_FAILURE);
    }
  }

  // create socket file descriptor
  if ((server.fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
    fprintf(stderr, "Error: socket creation failed\n");
    exit(EXIT_FAILURE);
  }

  // set socket options
  int option = 1;
  if (setsockopt(server.fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &option,
                 sizeof(option))) {
    fprintf(stderr, "Error: socket setup failed\n");
    exit(EXIT_FAILURE);
  }

  // bind socket to port
  struct sockaddr_in address;
  size_t addressLen = sizeof(address);
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = INADDR_ANY;
  address.sin_port = htons(server.port);
  if (bind(server.fd, (struct sockaddr *)&address, addressLen) < 0) {
    fprintf(stderr, "Error: socket bind failed\n");
    exit(EXIT_FAILURE);
  }

  // wait for a connection, then serve client request
  printf("Serving on port %d\n", server.port);
  while (!listen(server.fd, SOMAXCONN)) {
    // connect with client
    struct client client;
    if ((client.fd = accept(server.fd, (struct sockaddr *)&address,
                            (socklen_t *)&addressLen)) < 0) {
      fprintf(stderr, "Error: failed to accept connection\n");
      continue;
    }
    printf("Client connected\n");
    serve(client);
    close(client.fd);
  }
}