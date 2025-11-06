#include <fcntl.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/ip.h>
#include <unistd.h>
#include <fstream>
#include <cstring>
#include <string>

#include "http_connect.h"
#include "config.h"
#include "webserver.h"
#include "log.h"

static const unsigned int MAX_EVENT = 60000;

WebServer::WebServer():
    m_run{true},
    m_add_connect{Config::getInstance()->allowIpv6()
        ? (Config::getInstance()->allowIpv4()
                ? std::bind(&WebServer::add_connect_v4_v6, this)
                : std::bind(&WebServer::add_connect_v6, this))
                : std::bind(&WebServer::add_connect_v4, this)} {
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
    if ((m_listenfd = socket(Config::getInstance()->allowIpv6() ? PF_INET6 : PF_INET, SOCK_STREAM, 0)) < 0) {
        LOG_ERROR("socket:%s", strerror(errno));
        exit(1);
    }

    int val = 1;
    if (setsockopt(m_listenfd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val)) < 0) {
        LOG_WARN("setsockopt SO_REUSEADDR:%s", strerror(errno));
    }

    sockaddr_storage addr{};
    socklen_t addr_len;
    if (Config::getInstance()->allowIpv6()) {
        val = Config::getInstance()->allowIpv4() ? 0 : 1;
        int ret = setsockopt(m_listenfd, IPPROTO_IPV6, IPV6_V6ONLY, &val, sizeof(val));
        if (ret < 0) {
            LOG_WARN("setsockopt IPV6_V6ONLY:%s", strerror(errno));
        }
        if (ret == 0 && val == 0) {
            LOG_INFO("同时允许ipv4和ipv6连接");
        } else {
            LOG_INFO("仅允许ipv6连接");
        }
        sockaddr_in6* address = reinterpret_cast<sockaddr_in6*>(&addr);
        address->sin6_family = AF_INET6;
        address->sin6_port = htons(Config::getInstance()->getPort());
        inet_pton(AF_INET6, "::", &address->sin6_addr);
        addr_len = sizeof(*address);
    } else {
        sockaddr_in* address = reinterpret_cast<sockaddr_in*>(&addr);
        address->sin_family = AF_INET;
        address->sin_port = htons(Config::getInstance()->getPort());
        inet_pton(AF_INET, "0.0.0.0", &address->sin_addr);
        addr_len = sizeof(*address);
        LOG_INFO("仅允许ipv4连接");
    }
    if (bind(m_listenfd, reinterpret_cast<sockaddr*>(&addr), addr_len) < 0) {
        close(m_listenfd);
        LOG_ERROR("bind:%s", strerror(errno));
        exit(1);
    }
    getsockname(m_listenfd, reinterpret_cast<sockaddr*>(&addr), &addr_len);
    if (Config::getInstance()->allowIpv6()) {
        sockaddr_in6* address = reinterpret_cast<sockaddr_in6*>(&addr);
        LOG_INFO("监听端口为：%d", ntohs(address->sin6_port));
    } else {
        sockaddr_in* address = reinterpret_cast<sockaddr_in*>(&addr);
        LOG_INFO("监听端口为：%d", ntohs(address->sin_port));
    }

    if (listen(m_listenfd, SOMAXCONN) < 0) {
        close(m_listenfd);
        LOG_ERROR("listen:%s", strerror(errno));
        exit(1);
    }

    if (pipe(m_pipe) < 0) {
        close(m_listenfd);
        LOG_ERROR("pipe:%s", strerror(errno));
        exit(1);
    }
    setnonblock(m_pipe[0]);

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
    loadBlackList();
}

WebServer::~WebServer() {
    saveBlackList();
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
                m_add_connect();
            } else if (fd == m_pipe[0]) {
                handlePipeEvent();
            } else {
                HttpConnect *conn = reinterpret_cast<HttpConnect*>(events[i].data.ptr);
                if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                    LOG_DEBUG("收到事件%d,删除文件描述符%d", events[i].events, static_cast<int>(*conn));
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

void WebServer::add_connect_v4() {
    sockaddr_in address;
    socklen_t addr_len = sizeof(address);
    int sd = accept(m_listenfd, reinterpret_cast<sockaddr*>(&address), &addr_len);
    if (sd < 0) {
        LOG_ERROR("accept:%s", strerror(errno));
        return;
    }
    LOG_DEBUG("收到新连接,sd = %d", sd);
    char ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &address.sin_addr, ip, INET_ADDRSTRLEN);
    if (inBlackList(ip)) {
        LOG_DEBUG("ip %s 在黑名单内，关闭连接", ip);
        close(sd);
        return;
    }
    new HttpConnect(m_epollfd, m_pipe[1], sd, ip, ntohs(address.sin_port));
}

void WebServer::add_connect_v6() {
    sockaddr_in6 address;
    socklen_t addr_len = sizeof(address);
    int sd = accept(m_listenfd, reinterpret_cast<sockaddr*>(&address), &addr_len);
    if (sd < 0) {
        LOG_ERROR("accept:%s", strerror(errno));
        return;
    }
    LOG_DEBUG("收到新连接,sd = %d", sd);
    char ip[INET6_ADDRSTRLEN];
    inet_ntop(AF_INET6, &address.sin6_addr, ip, INET6_ADDRSTRLEN);
    if (inBlackList(ip)) {
        LOG_DEBUG("ip %s 在黑名单内，关闭连接", ip);
        close(sd);
        return;
    }
    new HttpConnect(m_epollfd, m_pipe[1], sd, ip, ntohs(address.sin6_port));
}

void WebServer::add_connect_v4_v6() {
    sockaddr_in6 address;
    socklen_t addr_len = sizeof(address);
    int sd = accept(m_listenfd, reinterpret_cast<sockaddr*>(&address), &addr_len);
    if (sd < 0) {
        LOG_ERROR("accept:%s", strerror(errno));
        return;
    }
    LOG_DEBUG("收到新连接,sd = %d", sd);
    char ip[INET6_ADDRSTRLEN];
    if (IN6_IS_ADDR_V4MAPPED(&address.sin6_addr)) {
        struct in_addr ipv4addr;
        memcpy(&ipv4addr, &address.sin6_addr.s6_addr[12], sizeof(ipv4addr));
        inet_ntop(AF_INET, &ipv4addr, ip, sizeof(ip));
    } else {
        inet_ntop(AF_INET6, &address.sin6_addr, ip, INET6_ADDRSTRLEN);
    }
    if (inBlackList(ip)) {
        LOG_DEBUG("ip %s 在黑名单内，关闭连接", ip);
        close(sd);
        return;
    }
    new HttpConnect(m_epollfd, m_pipe[1], sd, ip, ntohs(address.sin6_port));
}

void WebServer::stop() {
    m_run = false;
    int i = -1;
    write(m_pipe[1], &i, 4);
}

void WebServer::handlePipeEvent() {
    uint8_t tmp[1024];

    while (true) {
        ssize_t len = read(m_pipe[0], tmp, sizeof(tmp));
        if (len > 0) {
            m_pipeBuf.insert(m_pipeBuf.end(), tmp, tmp + len);
        } else if (len == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            break;
        } else if (len == -1 && errno == EINTR) {
            continue;
        } else if (len == 0) {
            LOG_WARN("管道被关闭");
            return;
        } else {
            LOG_WARN("从管道读取出错: %s", strerror(errno));
            return;
        }
    }

    LOG_DEBUG("管道收到数据，共计%d字节", m_pipeBuf.size());
    size_t offset = 0;
    while (true) {
        if (m_pipeBuf.size() - offset < sizeof(uint32_t)) break;

        uint32_t size;
        memcpy(&size, m_pipeBuf.data() + offset, sizeof(uint32_t));
        if (m_pipeBuf.size() - offset < sizeof(uint32_t) + size) break;

        const uint8_t* payload = reinterpret_cast<const uint8_t*>(m_pipeBuf.data() + offset + sizeof(uint32_t));
        if (size == sizeof(HttpConnect*)) {
            HttpConnect* conn = nullptr;
            memcpy(&conn, payload, sizeof(HttpConnect*));
            LOG_DEBUG("管道删除文件描述符 %d", (int)*conn);
            delete conn;
        } else {
            std::string data(reinterpret_cast<const char*>(payload), size);
            LOG_DEBUG("添加黑名单: %s", data.c_str());
            addBlackList(data.c_str());
        }

        offset += sizeof(uint32_t) + size; // 移动到下一帧
    }

    if (offset > 0)
        m_pipeBuf.erase(m_pipeBuf.begin(), m_pipeBuf.begin() + offset);
}

void WebServer::addBlackList(const std::string &ip, bool save) {
    std::string trimmed = trim(ip);
    if (trimmed.length() == 0) return;
    auto iter = m_blackList.insert(trimmed);
    if (iter.second && save) {
        saveBlackList();
    }
}

void WebServer::removeBlackList(const std::string &ip, bool save) {
    int ret = m_blackList.erase(ip);
    if (ret > 0 && save) {
        saveBlackList();
    }
}

void WebServer::loadBlackList() {
    m_blackList.clear();
    std::ifstream fin(m_blackListFile);
    if (!fin.is_open()) {
        LOG_WARN("无法打开黑名单文件%s: %s", m_blackListFile.c_str(), std::strerror(errno));
        return;
    }

    std::string ip;
    while (std::getline(fin, ip)) {
        std::string trimmed = trim(ip);
        if (trimmed.length() != 0) {
            m_blackList.insert(ip);
        }
    }
    LOG_DEBUG("加载黑名单完成，共计%d个ip", m_blackList.size());
    fin.close();
}

void WebServer::saveBlackList() {
    std::ofstream fout(m_blackListFile, std::ofstream::out | std::ofstream::trunc);
    if (! fout.is_open()) {
        LOG_WARN("无法写入黑名单文件%s: %s", m_blackListFile.c_str(), std::strerror(errno));
        return;
    }

    for (const auto &ip : m_blackList) {
        fout << ip << "\n";
    }
    fout.close();
    LOG_DEBUG("保存黑名单完成，共计%d个ip", m_blackList.size());
}

bool WebServer::inBlackList(const std::string &ip) {
    return m_blackList.find(ip) != m_blackList.end();
}

std::string WebServer::trim(const std::string &str) {
    size_t start = str.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = str.find_last_not_of(" \t\r\n");
    return str.substr(start, end - start + 1);
}
