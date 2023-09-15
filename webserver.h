#ifndef WEBSERVER_H_
#define WEBSERVER_H_

#include <sys/resource.h>
#include <map>

#include "threadpool.h"
#include "http_connect.h"

class WebServer {
public:
    WebServer();
    WebServer(const WebServer&) = delete;
    WebServer& operator=(const WebServer&) = delete;
    ~WebServer();
    void eventLoop();
    int setnonblock(int fd);
    void add_connect();
    void stop();
private:
    struct rlimit m_limit;
    int m_listenfd;
    int m_epollfd;
    int m_pipe[2];
    ThreadPool<HttpConnect> m_pool;
    std::map<int, HttpConnect*> m_map;
    bool m_run;
};

#endif
