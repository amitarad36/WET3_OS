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

    sprintf(body, "<html><title>OS-HW3 Error</title>");
    sprintf(body, "%s<body bgcolor=\"ffffff\">\r\n", body);
    sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
    sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
    sprintf(body, "%s<hr>OS-HW3 Web Server\r\n", body);

    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    sprintf(buf, "%sContent-Type: text/html\r\n", buf);
    sprintf(buf, "%sContent-Length: %lu\r\n\r\n", buf, strlen(body));

    Rio_writen(fd, buf, strlen(buf));
    Rio_writen(fd, body, strlen(body));
}

//
// Reads and discards everything up to an empty text line
//
void requestReadhdrs(rio_t* rp) {
    char buf[MAXLINE];

    Rio_readlineb(rp, buf, MAXLINE);
    while (strcmp(buf, "\r\n")) {
        Rio_readlineb(rp, buf, MAXLINE);
    }
}

//
// Determines if the request is static or dynamic
//
int requestParseURI(char* uri, char* filename, char* cgiargs) {
    char* ptr;

    if (strstr(uri, "..")) {
        sprintf(filename, "./public/home.html");
        return 1;
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

    srcp = Mmap(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0);
    Close(srcfd);

    sprintf(buf, "HTTP/1.0 200 OK\r\n");
    sprintf(buf, "%sServer: OS-HW3 Web Server\r\n", buf);
    sprintf(buf, "%sContent-Length: %d\r\n", buf, filesize);
    sprintf(buf, "%sContent-Type: %s\r\n\r\n", buf, filetype);

    Rio_writen(fd, buf, strlen(buf));
    Rio_writen(fd, srcp, filesize);
    Munmap(srcp, filesize);
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
        return 1;
    }
    return 0;
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
    if (t_stats == NULL) return;
    if (fd <= 0) return;

    rio_t rio;
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];

    Rio_readinitb(&rio, fd);
    if (Rio_readlineb(&rio, buf, MAXLINE) <= 0) return;

    if (sscanf(buf, "%s %s %s", method, uri, version) != 3) {
        requestError(fd, "Malformed request", "400", "Bad Request", "Server could not understand the request", arrival, dispatch, t_stats);
        return;
    }

    requestReadhdrs(&rio);

    char filename[MAXLINE], cgiargs[MAXLINE];
    int is_static = isStaticRequest(uri);
    requestParseURI(uri, filename, cgiargs);

    struct stat sbuf;
    if (stat(filename, &sbuf) < 0) {
        requestError(fd, filename, "404", "Not Found", "File not found", arrival, dispatch, t_stats);
        return;
    }

    if (is_static) {
        requestServeStatic(fd, filename, sbuf.st_size, arrival, dispatch, t_stats);
    }
    else {
        requestServeDynamic(fd, filename, cgiargs);
    }
}
