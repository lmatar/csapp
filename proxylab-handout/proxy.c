#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include "csapp.h"
#include "util.h"
#include "bytes.h"
#include "cache.h"

// #define DEBUG
#undef DEBUG

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

lru_cache_t lru_cache;
sem_t mutex;


/* You won't lose style points for including these long lines in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
static const char *accept_hdr = "Accept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8\r\n";
static const char *accept_encoding_hdr = "Accept-Encoding: gzip, deflate\r\n";


void usage()
{
    // TODO
    exit(-1);
}


void read_response(int infd)
{
    rio_t rio;
    Rio_readinitb(&rio, infd);

    char buf[MAXLINE];

    while (Rio_readlineb(&rio, buf, MAXLINE)) {
        // TODO: empty return

        printf("%s", buf);
    }
}

void forward_response(const char *key, int infd, int outfd)
{
    rio_t rio;
    Rio_readinitb(&rio, infd);

    char buf[MAXLINE];
    struct Bytes response;
    bytes_malloc(&response);

    if (!Rio_readlineb(&rio, buf, MAXLINE)) {
        //TODO: error report
        return;
    }

    bytes_append(&response, buf);

    Rio_readlineb(&rio, buf, MAXLINE);
    char header_name[MAXLINE], header_value[MAXLINE];
    int content_length = 0;
    while (strcmp(buf, "\r\n")) {
        parse_header(buf, header_name, header_value);
        if (strcasecmp(header_name, "Content-length") == 0) {
            content_length = atoi(header_value);
        }
        bytes_append(&response, buf);
        Rio_readlineb(&rio, buf, MAXLINE);
    }
    bytes_append(&response, buf);

    if (content_length == 0) {
        // TODO: error handling
        return;
    } else {
#ifdef DEBUG
        fprintf(stderr, "Content-length: %d\n", content_length);
#endif
        // read content
        char *content_buf = (char *)Malloc((content_length+1)*sizeof(char));
        Rio_readnb(&rio, content_buf, content_length);
        bytes_appendn(&response, content_buf, (size_t)content_length);
        Free(content_buf);
    }
    if (Rio_readlineb(&rio, buf, MAXLINE) != 0) {
        // TODO: error handling
        return;
    }
    

#ifdef DEBUG
    fprintf(stderr, "response length: %zu\n", string_length(response));
    // fprintf(stderr, "response:\n%s", string_cstr(response));
#endif
    
    Rio_writen(outfd,
            bytes_buf(response),
            bytes_length(response));

    if (bytes_length(response) < MAX_OBJECT_SIZE) {
        P(&mutex);
        lru_cache_insert(&lru_cache, key, bytes_buf(response),
                bytes_length(response));
        V(&mutex);
    }
    bytes_free(&response);
}

void forward(int fromfd)
{
    rio_t rio;
    Rio_readinitb(&rio, fromfd);
    char linebuf[MAXLINE], method[MAXLINE], uri[MAXLINE],
         version[MAXLINE], host[MAXLINE],
         port[MAXLINE], dir[MAXLINE],
         request_buf[MAXBUF],
         header_name[MAXLINE],
         header_value[MAXLINE];

    if (!Rio_readlineb(&rio, linebuf, MAXLINE)) {
        return;
    }
    sscanf(linebuf, "%s %s %s", method, uri, version);
    if (strcasecmp(method, "GET")) {
        // TODO
        return;
    }
    parse_uri(uri, host, port, dir);
#ifdef DEBUG
    fprintf(stderr, "host: %s port: %s dir: %s\n", host, port, dir);
#endif

    char formated_uri[MAXLINE];
    sprintf(formated_uri, "%s:%s%s", host, port, dir);
    P(&mutex);
    lru_cache_node_t *pnode = lru_cache_find(&lru_cache, formated_uri);

    if (pnode) {
        Rio_writen(fromfd, pnode->value, pnode->value_len);
        V(&mutex);
        return;
    }
    V(&mutex);
    
    sprintf(request_buf, "%s %s %s\r\n", method, dir, version);
    
    int has_host = 0;
    // request headers
    Rio_readlineb(&rio, linebuf, MAXLINE);
    while (strcmp(linebuf, "\r\n")) {
        parse_header(linebuf, header_name, header_value);
        // printf("header %s : %s\n", header_name, header_value);
        if (strcasecmp(header_name, "Host") == 0) {
            has_host = 1;
            sprintf(request_buf, "%s%s", request_buf, linebuf);
        } else if (strcasecmp(header_name, "User-Agent") == 0 ||
                strcasecmp(header_name, "Accept") == 0 ||
                strcasecmp(header_name, "Accept-Encoding") == 0 ||
                strcasecmp(header_name, "Connection") == 0 ||
                strcasecmp(header_name, "Proxy-Connection") == 0) {
            // ignore
        } else {
            sprintf(request_buf, "%s%s", request_buf, linebuf);
        }
        Rio_readlineb(&rio, linebuf, MAXLINE);
    }
    if (!has_host) {
        sprintf(request_buf, "%s%s: %s:%s\r\n", request_buf, "Host",
                host, port);
    }
    sprintf(request_buf, "%s%s", request_buf,
            user_agent_hdr);
    sprintf(request_buf, "%s%s", request_buf,
            accept_hdr);
    sprintf(request_buf, "%s%s", request_buf,
            accept_encoding_hdr);
    sprintf(request_buf, "%s%s:close\r\n", request_buf,
            "Connection");
    sprintf(request_buf, "%s%s:close\r\n", request_buf,
            "Proxy-Connection");
    sprintf(request_buf, "%s\r\n", request_buf);
#ifdef DEBUG
    fprintf(stderr, "request buf:\n%s\n", request_buf);
#endif
    int clientfd = Open_clientfd(host, port);
    Rio_writen(clientfd, request_buf, strlen(request_buf));
    
    // receive data
    // read_response(clientfd);
    
    forward_response(formated_uri, clientfd, fromfd);
    Close(clientfd);
}

void *thread(void *vargp)
{
    int connfd = *((int*)vargp);
    Pthread_detach(pthread_self());
    Free(vargp);
    forward(connfd);
    Close(connfd);
    return NULL;
}



int main(int argc, char **argv)
{
    // printf("%s%s%s", user_agent_hdr, accept_hdr, accept_encoding_hdr);

    if (argc != 2) {
        usage();
    }
    int listenfd = Open_listenfd(argv[1]);
    struct sockaddr_storage clientaddr;
    socklen_t clientlen = sizeof(clientaddr);
    int *connfdp;
    lru_cache_init(&lru_cache, MAX_CACHE_SIZE);
    Sem_init(&mutex, 0, 1);
    while (1) {
        connfdp = Malloc(sizeof(int));
        *connfdp = Accept(listenfd,
                (SA *)&clientaddr,
                &clientlen);
        if (*connfdp == -1) {
            Free(connfdp);
            // TODO: error report
        } else {
            // TODO: use thread pool
            pthread_t tid;
            Pthread_create(&tid, NULL, thread, connfdp);
        }
    }
    lru_cache_free(&lru_cache);
    return 0;
}
