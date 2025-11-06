#ifndef WEBSERVER_H_
#define WEBSERVER_H_

#include <sys/resource.h>
#include <map>
#include <unordered_set>
#include <functional>
#include <vector>

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
    void add_connect_v4();
    void add_connect_v6();
    void add_connect_v4_v6();
    void stop();
private:
    void handlePipeEvent();
    void addBlackList(const std::string &ip, bool save=true);
    void removeBlackList(const std::string &ip, bool save=true);
    void loadBlackList();
    void saveBlackList();
    bool inBlackList(const std::string &ip);
    std::string trim(const std::string &str);

    std::unordered_set<std::string> m_blackList;
    const std::string m_blackListFile = "black.txt";
    std::vector<char> m_pipeBuf;
    struct rlimit m_limit;
    int m_listenfd;
    int m_epollfd;
    int m_pipe[2];
    ThreadPool m_pool;
    bool m_run;
    const std::function<void()> m_add_connect;
};

#endif
