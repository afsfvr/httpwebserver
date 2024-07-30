#include <fcntl.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/ip.h>
#include <unistd.h>
#include <cstring>
#include <string>

#include "http_connect.h"
#include "config.h"
#include "webserver.h"
#include "log.h"

static const unsigned int MAX_EVENT = 60000;

WebServer::WebServer(): m_run(true) {
    if (getrlimit(RLIMIT_OFILE, &m_limit) < 0) {
        LOG_ERROR("getrlimit:%s", strerror(errno));
        exit(1);
    }
    struct rlimit l;
    l.rlim_cur = std::max((unsigned int)m_limit.rlim_cur, MAX_EVENT);
    l.rlim_max = std::max((unsigned int)m_limit.rlim_max, MAX_EVENT);
    if (setrlimit(RLIMIT_OFILE, &l) < 0) {
        LOG_ERROR("setrlimit:%s", strerror(errno));
        exit(1);
    }
    LOG_INFO("最多可存在%u个文件描述符", l.rlim_cur);
    if ((m_listenfd = socket(PF_INET, SOCK_STREAM, 0)) < 0) {
        LOG_ERROR("socket:%s", strerror(errno));
        exit(1);
    }

    int val = 1;
    if (setsockopt(m_listenfd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val)) < 0) {
        LOG_ERROR("setsockopt SO_REUSEADDR:%s", strerror(errno));
    }

    sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_port = htons(Config::getInstance()->getPort());
    inet_pton(AF_INET, "0.0.0.0", &address.sin_addr);

    socklen_t addr_len = sizeof(address);
    if (bind(m_listenfd, reinterpret_cast<sockaddr*>(&address), addr_len) < 0) {
        close(m_listenfd);
        LOG_ERROR("bind:%s", strerror(errno));
        exit(1);
    }
    getsockname(m_listenfd, reinterpret_cast<sockaddr*>(&address), &addr_len);
    LOG_INFO("监听端口为：%d", ntohs(address.sin_port));

    if (listen(m_listenfd, 32) < 0) {
        close(m_listenfd);
        LOG_ERROR("listen:%s", strerror(errno));
        exit(1);
    }

    if (pipe(m_pipe) < 0) {
        close(m_listenfd);
        LOG_ERROR("pipe:%s", strerror(errno));
        exit(1);
    }

    if ((m_epollfd = epoll_create(8)) < 0) {
        close(m_listenfd);
        LOG_ERROR("epoll_create:%s", strerror(errno));
        exit(1);
    }

    struct epoll_event ev;
    ev.data.fd = m_listenfd;
    ev.events = EPOLLIN;
    epoll_ctl(m_epollfd, EPOLL_CTL_ADD, m_listenfd, &ev);

    ev.data.fd = m_pipe[0];
    ev.events = EPOLLIN;
    epoll_ctl(m_epollfd, EPOLL_CTL_ADD, m_pipe[0], &ev);
}

WebServer::~WebServer() {
    epoll_ctl(m_epollfd, EPOLL_CTL_DEL, m_listenfd, nullptr);
    epoll_ctl(m_epollfd, EPOLL_CTL_DEL, m_pipe[0], nullptr);
    close(m_listenfd);
    close(m_pipe[0]);
    close(m_pipe[1]);
    close(m_epollfd);
    setrlimit(RLIMIT_OFILE, &m_limit);
}

void WebServer::eventLoop() {
    struct epoll_event events[MAX_EVENT];
    while (m_run) {
        int count = epoll_wait(m_epollfd, events, MAX_EVENT, -1);
        if (count < 0) {
            if (errno != EINTR) {LOG_WARN("epoll_wait:%s", strerror(errno));}
            continue;
        }
        for (int i = 0; i < count; i++) {
            int fd = events[i].data.fd;
            if (fd == m_listenfd) {
                add_connect();
            } else if (fd == m_pipe[0]) {
                HttpConnect *conn = nullptr;
                ssize_t len = read(m_pipe[0], &conn, sizeof(HttpConnect*));
                if (len == sizeof(HttpConnect*)) {
                    LOG_DEBUG("管道删除文件描述符%d", (int)*conn);
                    delete conn;
                } else {
                    LOG_WARN("管道读数据时返回%d，应为%d, error:%d", len, sizeof(HttpConnect*), errno);
                }
            } else {
                HttpConnect *conn = reinterpret_cast<HttpConnect*>(events[i].data.ptr);
                if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                    LOG_DEBUG("收到事件%d,删除文件描述符%d", events[i].events, fd);
                    delete conn;
                } else if (events[i].events & (EPOLLIN | EPOLLOUT)) {
                    m_pool.addjob(conn);
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
    int sd = accept(m_listenfd, reinterpret_cast<sockaddr*>(&address), &addr_len);
    if (sd < 0) {
        LOG_ERROR("accept:%s", strerror(errno));
        return;
    }
    LOG_DEBUG("收到新连接,sd = %d", sd);
    char ip[32];
    inet_ntop(AF_INET, &address.sin_addr, ip, 32);
    new HttpConnect(m_epollfd, m_pipe[1], sd, ip, ntohs(address.sin_port));
}

void WebServer::stop() {
    m_run = false;
    int i = -1;
    write(m_pipe[1], &i, 4);
}
