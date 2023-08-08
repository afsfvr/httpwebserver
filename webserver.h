#ifndef WEBSERVER_H_
#define WEBSERVER_H_

#include <sys/resource.h>
#include <map>

#include "threadpool.h"
#include "http_connect.h"

class WebServer {
public:
    WebServer();
    ~WebServer();
    void eventLoop();
    int setnonblock(int fd);
    void add_connect();
    static void quit(int x);
    static bool run;
private:
    struct rlimit limit;
    int listenfd;
    int epollfd;
    int m_pipe[2];
    ThreadPool<HttpConnect> pool;
    std::map<int, HttpConnect*> map;
};

#endif
