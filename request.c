//
// request.c: Does the bulk of the work for the web server.
//

#include "segel.h"
#include "request.h"

pthread_mutex_t stat_lock = PTHREAD_MUTEX_INITIALIZER;

//
// Handles errors and sends error response to the client
//
void requestError(int fd, char* cause, char* errnum, char* shortmsg, char* longmsg, struct timeval arrival, struct timeval dispatch, threads_stats t_stats) {
    char buf[MAXLINE], body[MAXBUF];

    // Create the body of the error message
    sprintf(body, "<html><title>OS-HW3 Error</title>");
    sprintf(body, "%s<body bgcolor=\"ffffff\">\r\n", body);
    sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
    sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
    sprintf(body, "%s<hr>OS-HW3 Web Server\r\n", body);

    // Write out the header information for this response
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    sprintf(buf, "%sContent-Type: text/html\r\n", buf);
    sprintf(buf, "%sContent-Length: %lu\r\n\r\n", buf, strlen(body));

    printf("Sending error response (fd=%d):\n%s", fd, buf);
    fflush(stdout);

    Rio_writen(fd, buf, strlen(buf));  // No assignment
    printf("Successfully sent error headers to client (fd=%d)\n", fd);
    fflush(stdout);

    Rio_writen(fd, body, strlen(body));  // No assignment
    printf("Successfully sent error body to client (fd=%d)\n", fd);
    fflush(stdout);
}

//
// Reads and discards everything up to an empty text line
//
void requestReadhdrs(rio_t* rp) {
    char buf[MAXLINE];

    printf("Reading headers...\n"); // Debug print
    fflush(stdout);

    Rio_readlineb(rp, buf, MAXLINE);
    while (strcmp(buf, "\r\n")) {
        printf("Header: %s", buf);  // Debug: Print headers received
        fflush(stdout);
        Rio_readlineb(rp, buf, MAXLINE);
    }

    printf("Finished reading headers.\n"); // Debug print
    fflush(stdout);
}

//
// Determines if the request is static or dynamic
//
int requestParseURI(char* uri, char* filename, char* cgiargs) {
    char* ptr;

    if (strstr(uri, "..")) {
        sprintf(filename, "./public/home.html");
        return 1;  // Static request
    }

    if (!strstr(uri, "cgi")) {
        strcpy(cgiargs, "");
        sprintf(filename, "./public/%s", uri);
        if (uri[strlen(uri) - 1] == '/') {
            strcat(filename, "home.html");
        }
        return 1;
    }
    else {
        ptr = index(uri, '?');
        if (ptr) {
            strcpy(cgiargs, ptr + 1);
            *ptr = '\0';
        }
        else {
            strcpy(cgiargs, "");
        }
        sprintf(filename, "./public/%s", uri);
        return 0;
    }
}

//
// Determines file type based on filename
//
void requestGetFiletype(char* filename, char* filetype) {
    if (strstr(filename, ".html"))
        strcpy(filetype, "text/html");
    else if (strstr(filename, ".gif"))
        strcpy(filetype, "image/gif");
    else if (strstr(filename, ".jpg"))
        strcpy(filetype, "image/jpeg");
    else
        strcpy(filetype, "text/plain");
}

//
// Serves static content (HTML, images, etc.)
//
void requestServeStatic(int fd, char* filename, int filesize, struct timeval arrival, struct timeval dispatch, threads_stats t_stats) {
    int srcfd;
    char* srcp, filetype[MAXLINE], buf[MAXBUF];

    requestGetFiletype(filename, filetype);

    srcfd = Open(filename, O_RDONLY, 0);
    if (srcfd < 0) {
        requestError(fd, filename, "404", "Not Found", "File not found", arrival, dispatch, t_stats);
        return;
    }

    printf("Sending static file: %s (fd=%d)\n", filename, fd);
    fflush(stdout);

    // Memory-map the file for fast serving
    srcp = Mmap(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0);
    Close(srcfd);

    // Send response headers
    sprintf(buf, "HTTP/1.0 200 OK\r\n");
    sprintf(buf, "%sServer: OS-HW3 Web Server\r\n", buf);
    sprintf(buf, "%sContent-Length: %d\r\n", buf, filesize);
    sprintf(buf, "%sContent-Type: %s\r\n\r\n", buf, filetype);

    printf("Sending response headers:\n%s", buf);
    fflush(stdout);

    Rio_writen(fd, buf, strlen(buf));  // No assignment
    printf("Successfully sent response headers to client (fd=%d)\n", fd);
    fflush(stdout);

    // Send the file content
    printf("Sending file content to fd=%d\n", fd);
    fflush(stdout);

    Rio_writen(fd, srcp, filesize);  // No assignment
    printf("Successfully sent file content to client (fd=%d)\n", fd);
    fflush(stdout);

    Munmap(srcp, filesize);
    printf("Finished sending file: %s (fd=%d)\n", filename, fd);
    fflush(stdout);
}

//
// Serves dynamic content (CGI execution)
//
void requestServeDynamic(int fd, char* filename, char* cgiargs) {
    char buf[MAXLINE], * emptylist[] = { NULL };

    sprintf(buf, "HTTP/1.0 200 OK\r\n");
    sprintf(buf, "%sServer: OS-HW3 Web Server\r\n\r\n", buf);
    Rio_writen(fd, buf, strlen(buf));

    if (fork() == 0) {
        setenv("QUERY_STRING", cgiargs, 1);
        dup2(fd, STDOUT_FILENO);
        execve(filename, emptylist, environ);
    }
    wait(NULL);
}

int isStaticRequest(char* uri) {
    if (strstr(uri, ".html") || strstr(uri, ".jpg") || strstr(uri, ".png") ||
        strstr(uri, ".gif") || strstr(uri, ".css") || strstr(uri, ".js")) {
        return 1; // Static request
    }
    return 0; // Dynamic request
}

//
// Determines if a request is VIP
//
int getRequestType(int fd) {
    rio_t rio;
    char buf[MAXLINE];

    Rio_readinitb(&rio, fd);
    Rio_readlineb(&rio, buf, MAXLINE);

    return (strstr(buf, "REAL") != NULL) ? 1 : 0;
}

//
// Handles HTTP requests, updates statistics, and serves content
//
void requestHandle(int fd, struct timeval arrival, struct timeval dispatch, threads_stats t_stats) {
    if (t_stats == NULL) {
        fprintf(stderr, "Error: Received NULL thread stats\n");
        return;
    }

    printf("Handling request for fd=%d\n", fd);
    fflush(stdout);

    // Check if the file descriptor is valid
    if (fd <= 0) {
        printf("ERROR: Received invalid fd=%d in requestHandle!\n", fd);
        fflush(stdout);
        return;
    }

    rio_t rio;
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];

    Rio_readinitb(&rio, fd);

    // Read request line
    if (Rio_readlineb(&rio, buf, MAXLINE) <= 0) {
        fprintf(stderr, "Error: Failed to read request line (fd=%d)\n", fd);
        return;
    }
    sscanf(buf, "%s %s %s", method, uri, version);

    printf("Parsed request - fd=%d, Method: %s, URI: %s, Version: %s\n", fd, method, uri, version);
    fflush(stdout);

    // Only support GET requests
    if (strcasecmp(method, "GET") != 0) {
        printf("ERROR: Unsupported method %s received (fd=%d)\n", method, fd);
        fflush(stdout);
        requestError(fd, method, "501", "Not Implemented", "Server does not support this method", arrival, dispatch, t_stats);
        return;
    }

    // Read and discard HTTP request headers
    printf("Reading headers for fd=%d...\n", fd);
    fflush(stdout);
    requestReadhdrs(&rio);
    printf("Finished reading headers for fd=%d.\n", fd);
    fflush(stdout);

    // Determine if the request is static or dynamic
    char filename[MAXLINE], cgiargs[MAXLINE];
    int is_static = isStaticRequest(uri);
    requestParseURI(uri, filename, cgiargs);

    printf("Requested filename: %s (fd=%d)\n", filename, fd);
    fflush(stdout);

    // Check if the requested file exists
    struct stat sbuf;
    if (stat(filename, &sbuf) < 0) {
        printf("ERROR: File not found: %s (fd=%d)\n", filename, fd);
        fflush(stdout);
        requestError(fd, filename, "404", "Not Found", "File not found", arrival, dispatch, t_stats);
        return;
    }

    // Serve the request
    if (is_static) {
        printf("Serving static content: %s (fd=%d)\n", filename, fd);
        fflush(stdout);
        requestServeStatic(fd, filename, sbuf.st_size, arrival, dispatch, t_stats);
    }
    else {
        printf("Serving dynamic content: %s (fd=%d)\n", filename, fd);
        fflush(stdout);
        requestServeDynamic(fd, filename, cgiargs);
    }
}
