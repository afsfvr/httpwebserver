#include <fcntl.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/ip.h>
#include <unistd.h>
#include <cstring>
#include <string>
#include <csignal>

#include "http_connect.h"
#include "config.h"
#include "webserver.h"
#include "log.h"

bool WebServer::run = true;
static const int MAX_EVENT = 60000;

WebServer::WebServer() {
    if (getrlimit(RLIMIT_OFILE, &limit) < 0) {
        LOG_ERROR("getrlimit:%s", strerror(errno));
        exit(1);
    }
    struct rlimit l;
    l.rlim_cur = MAX_EVENT + 20;
    l.rlim_max = limit.rlim_max > (MAX_EVENT + 20) ? limit.rlim_max : (MAX_EVENT + 20);
    if (setrlimit(RLIMIT_OFILE, &l) < 0) {
        LOG_ERROR("setrlimit:%s", strerror(errno));
        exit(1);
    }
    LOG_INFO("max file descriptor is %d", l.rlim_cur);
    signal(SIGPIPE, SIG_IGN);
    signal(SIGINT, quit);
    signal(SIGQUIT, quit);
    signal(SIGTERM, quit);
    if ((listenfd = socket(PF_INET, SOCK_STREAM, 0)) < 0) {
        LOG_ERROR("socket:%s", strerror(errno));
        exit(1);
    }

    int val = 1;
    if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val)) < 0) {
        LOG_ERROR("setsockopt SO_REUSEADDR:%s", strerror(errno));
    }

    sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_port = htons(Config::getInstance()->getPort());
    inet_pton(AF_INET, "0.0.0.0", &address.sin_addr);

    socklen_t addr_len = sizeof(address);
    if (bind(listenfd, reinterpret_cast<sockaddr*>(&address), addr_len) < 0) {
        close(listenfd);
        LOG_ERROR("bind:%s", strerror(errno));
        exit(1);
    }
    LOG_INFO("port is %d", Config::getInstance()->getPort());

    if (listen(listenfd, 32) < 0) {
        close(listenfd);
        LOG_ERROR("listen:%s", strerror(errno));
        exit(1);
    }

    if (pipe(m_pipe) < 0) {
        close(listenfd);
        LOG_ERROR("pipe:%s", strerror(errno));
        exit(1);
    }

    if ((epollfd = epoll_create(8)) < 0) {
        close(listenfd);
        LOG_ERROR("epoll_create:%s", strerror(errno));
        exit(1);
    }

    struct epoll_event ev;
    ev.data.fd = listenfd;
    ev.events = EPOLLIN;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, listenfd, &ev);

    ev.data.fd = m_pipe[0];
    ev.events = EPOLLIN;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, m_pipe[0], &ev);
}
WebServer::~WebServer() {
    LOG_INFO("当前有%d个连接", map.size());
    for (auto iter = map.begin(); iter != map.end(); ++iter) {
        delete iter->second;
    }
    epoll_ctl(listenfd, EPOLL_CTL_DEL, listenfd, nullptr);
    epoll_ctl(listenfd, EPOLL_CTL_DEL, m_pipe[0], nullptr);
    close(listenfd);
    close(m_pipe[0]);
    close(m_pipe[1]);
    close(epollfd);
    setrlimit(RLIMIT_OFILE, &limit);
}

void WebServer::eventLoop() {
    struct epoll_event events[MAX_EVENT];
    while (run) {
        int count = epoll_wait(epollfd, events, MAX_EVENT, -1);
        if (count < 0) {
            LOG_WARN("epoll_wait:%s", strerror(errno));
            continue;
        }
        for (int i = 0; i < count; i++) {
            int fd = events[i].data.fd;
            LOG_DEBUG("文件描述符%d收到事件%d", fd, events[i].events);
            if (fd == listenfd) {
                add_connect();
            } else if (fd == m_pipe[0]) {
                read(m_pipe[0], &fd, 4);
                auto iter = map.find(fd);
                if (iter == map.end()) {
                    LOG_WARN("未找到文件描述符%d对应的连接", fd);
                    continue;
                }
                HttpConnect *conn = iter->second;
                map.erase(iter);
                delete conn;
                LOG_INFO("管道删除文件描述符%d", fd);
            } else {
                auto iter = map.find(fd);
                if (iter == map.end()) {
                    LOG_WARN("未找到文件描述符%d对应的连接", fd);
                    continue;
                }
                if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                    LOG_INFO("收到事件%d,删除文件描述符%d", events[i].events, fd);
                    HttpConnect *conn = iter->second;
                    map.erase(iter);
                    delete conn;
                } else if (events[i].events & (EPOLLIN | EPOLLOUT)) {
                    pool.addjob(iter->second);
                }
            }
        }
    }
}

int WebServer::setnonblock(int fd) {
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

void WebServer::add_connect() {
    sockaddr_in address;
    socklen_t addr_len = sizeof(address);
    int sd = accept(listenfd, reinterpret_cast<sockaddr*>(&address), &addr_len);
    if (sd < 0) {
        LOG_ERROR("accept:%s", strerror(errno));
        return;
    }
    if (map.size() + 2 >= MAX_EVENT) {
        close(sd);
        LOG_WARN("达到epoll上限,关闭文件描述符%d", sd);
        return;
    }
    LOG_INFO("收到新连接,sd = %d", sd);
    auto iter = map.find(sd);
    if (iter != map.end()) {
        LOG_WARN("存在文件描述符%d,删除,关闭本次连接", sd);
        close(sd);
        HttpConnect *conn = iter->second;
        map.erase(iter);
        delete conn;
        return;
    }
    char ip[32];
    inet_ntop(AF_INET, &address.sin_addr, ip, 32);
    HttpConnect *conn = new HttpConnect(epollfd, m_pipe[1], sd, ip, ntohs(address.sin_port));
    if (! map.insert(std::make_pair(sd, conn)).second) {
        LOG_WARN("插入文件描述符%d失败", sd);
        delete conn;
    }
}

void WebServer::quit(int x) {
    run = false;
    switch (x) {
    case 2:
        LOG_WARN("收到信号SIGINT,退出程序", x);
        break;
    case 3:
        LOG_WARN("收到信号SIGQUIT,退出程序", x);
        break;
    case 15:
        LOG_WARN("收到信号SIGTERM,退出程序", x);
        break;
    default:
        LOG_WARN("收到信号%d,退出程序", x);
        break;
    }
}
