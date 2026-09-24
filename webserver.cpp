#ifdef HTTPS
#include <openssl/err.h>
#include <openssl/crypto.h>
#endif

#include "http_connect.h"
#include "config.h"
#include "webserver.h"

static const unsigned int MAX_EVENT = 60000;

WebServer::WebServer():
    m_run{ true }
#ifdef HTTPS
    , m_ctx{ nullptr } {
    OPENSSL_init_ssl(0, nullptr);

    m_ctx = SSL_CTX_new(TLS_server_method());
    if (!m_ctx) {
        int err = ERR_get_error();
        if (err) SPDLOG_ERROR("SSL_CTX对象创建失败: {}", ERR_error_string(err, nullptr));
        exit(1);
    }

    SSL_CTX_set_min_proto_version(m_ctx, TLS1_2_VERSION);
    SSL_CTX_set_cipher_list(m_ctx, "HIGH:!aNULL:!MD5");

    /* if (SSL_CTX_use_certificate_file(m_ctx, Config::getInstance()->getCertPath().c_str(), SSL_FILETYPE_PEM) <= 0) { // 加载服务器证书 */
    if (SSL_CTX_use_certificate_chain_file(m_ctx, Config::getInstance()->getCertPath().c_str()) <= 0) { // 加载服务器证书和证书链
        int err = ERR_get_error();
        if (err) SPDLOG_ERROR("服务器证书错误: {} {}", Config::getInstance()->getCertPath(), ERR_error_string(err, nullptr));
        ERR_print_errors_fp(stderr);
        exit(1);
    }

    if (SSL_CTX_use_PrivateKey_file(m_ctx, Config::getInstance()->getKeyPath().c_str(), SSL_FILETYPE_PEM) <= 0) {
        int err = ERR_get_error();
        if (err) SPDLOG_ERROR("服务器证书私钥错误: {} {}", Config::getInstance()->getKeyPath(), ERR_error_string(err, nullptr));
        ERR_print_errors_fp(stderr);
        exit(1);
    }

    if (!SSL_CTX_check_private_key(m_ctx)) {
        SPDLOG_ERROR("证书和私钥不匹配");
        ERR_print_errors_fp(stderr);
        exit(1);
    }

    SSL_CTX_set_alpn_select_cb(m_ctx, &WebServer::alpnSelectCb, nullptr);
#else
    {
#endif
    if (getrlimit(RLIMIT_OFILE, &m_limit) < 0) {
        SPDLOG_ERROR("getrlimit: {}", strerror(errno));
        exit(1);
    }
    struct rlimit l;
    l.rlim_cur = std::max((unsigned int) m_limit.rlim_cur, MAX_EVENT);
    l.rlim_max = std::max((unsigned int) m_limit.rlim_max, MAX_EVENT);
    if (setrlimit(RLIMIT_OFILE, &l) < 0) {
        SPDLOG_ERROR("setrlimit: {}", strerror(errno));
        exit(1);
    }
    SPDLOG_INFO("最多可存在{}个文件描述符", l.rlim_cur);
    if ((m_listenfd = socket(Config::getInstance()->allowIpv6() ? PF_INET6 : PF_INET, SOCK_STREAM, 0)) < 0) {
        SPDLOG_ERROR("socket: {}", strerror(errno));
        exit(1);
    }

    int val = 1;
    if (setsockopt(m_listenfd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val)) < 0) {
        SPDLOG_WARN("setsockopt SO_REUSEADDR: {}", strerror(errno));
    }

    sockaddr_storage addr{};
    socklen_t addr_len;
    if (Config::getInstance()->allowIpv6()) {
        val = Config::getInstance()->allowIpv4() ? 0 : 1;
        int ret = setsockopt(m_listenfd, IPPROTO_IPV6, IPV6_V6ONLY, &val, sizeof(val));
        if (ret < 0) {
            SPDLOG_WARN("setsockopt IPV6_V6ONLY: {}", strerror(errno));
        }
        if (ret == 0 && val == 0) {
            SPDLOG_INFO("同时允许ipv4和ipv6连接");
        } else {
            SPDLOG_INFO("仅允许ipv6连接");
        }
        sockaddr_in6 *address = reinterpret_cast<sockaddr_in6 *>(&addr);
        address->sin6_family = AF_INET6;
        address->sin6_port = htons(Config::getInstance()->getPort());
        inet_pton(AF_INET6, "::", &address->sin6_addr);
        addr_len = sizeof(*address);
    } else {
        sockaddr_in *address = reinterpret_cast<sockaddr_in *>(&addr);
        address->sin_family = AF_INET;
        address->sin_port = htons(Config::getInstance()->getPort());
        inet_pton(AF_INET, "0.0.0.0", &address->sin_addr);
        addr_len = sizeof(*address);
        SPDLOG_INFO("仅允许ipv4连接");
    }
    if (bind(m_listenfd, reinterpret_cast<sockaddr *>(&addr), addr_len) < 0) {
        close(m_listenfd);
        SPDLOG_ERROR("bind {}: {}", Config::getInstance()->getPort(), strerror(errno));
        exit(1);
    }
    getsockname(m_listenfd, reinterpret_cast<sockaddr *>(&addr), &addr_len);
    if (Config::getInstance()->allowIpv6()) {
        sockaddr_in6 *address = reinterpret_cast<sockaddr_in6 *>(&addr);
        SPDLOG_INFO("监听端口为：{}", ntohs(address->sin6_port));
    } else {
        sockaddr_in *address = reinterpret_cast<sockaddr_in *>(&addr);
        SPDLOG_INFO("监听端口为：{}", ntohs(address->sin_port));
    }

    if (listen(m_listenfd, SOMAXCONN) < 0) {
        close(m_listenfd);
        SPDLOG_ERROR("listen: {}", strerror(errno));
        exit(1);
    }

    if (pipe(m_pipe) < 0) {
        close(m_listenfd);
        SPDLOG_ERROR("pipe: {}", strerror(errno));
        exit(1);
    }
    setnonblock(m_pipe[0]);

    if ((m_epollfd = epoll_create1(EPOLL_CLOEXEC)) < 0) {
        close(m_listenfd);
        SPDLOG_ERROR("epoll_create: {}", strerror(errno));
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

    m_pool = new ThreadPool;
}

WebServer::~WebServer() {
    delete m_pool;
    m_pool = nullptr;
    saveBlackList();
    epoll_ctl(m_epollfd, EPOLL_CTL_DEL, m_listenfd, nullptr);
    epoll_ctl(m_epollfd, EPOLL_CTL_DEL, m_pipe[0], nullptr);
    close(m_listenfd);
    close(m_pipe[0]);
    close(m_pipe[1]);
    close(m_epollfd);
    setrlimit(RLIMIT_OFILE, &m_limit);
#ifdef HTTPS
    if (m_ctx) SSL_CTX_free(m_ctx);
#endif
}

void WebServer::eventLoop() {
    struct epoll_event events[MAX_EVENT];
    while (m_run) {
        int count = epoll_wait(m_epollfd, events, MAX_EVENT, -1);
        if (count < 0) {
            if (errno != EINTR) { SPDLOG_WARN("epoll_wait: {}", strerror(errno)); }
            continue;
        }
        for (int i = 0; i < count; i++) {
            int fd = events[i].data.fd;
            if (fd == m_listenfd) {
                add_connect();
            } else if (fd == m_pipe[0]) {
                handlePipeEvent();
            } else {
                uint32_t ev = events[i].events;
                HttpConnect *conn = reinterpret_cast<HttpConnect *>(events[i].data.ptr);
                if (ev & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                    SPDLOG_DEBUG("收到事件{},删除文件描述符{}", ev, static_cast<int>(*conn));
                    bool ret = m_pool->cancelAndDeleteJob(conn);
                    if (! ret) delete conn;
                } else if (ev & (EPOLLIN | EPOLLOUT)) {
                    m_pool->addJob(conn);
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
    sockaddr_storage address{};
    socklen_t addr_len = sizeof(address);
    int sd = accept(m_listenfd, reinterpret_cast<sockaddr *>(&address), &addr_len);
    if (sd < 0) {
        SPDLOG_ERROR("accept:{}", strerror(errno));
        return;
    }
    char ip[INET6_ADDRSTRLEN]{};
    uint16_t port;
    if (address.ss_family == AF_INET) {
        auto *addr = reinterpret_cast<sockaddr_in *>(&address);
        inet_ntop(AF_INET, &addr->sin_addr, ip, sizeof(ip));
        port = ntohs(addr->sin_port);
    } else if (address.ss_family == AF_INET6) {
        auto *addr = reinterpret_cast<sockaddr_in6 *>(&address);
        if (IN6_IS_ADDR_V4MAPPED(&addr->sin6_addr)) {
            struct in_addr ipv4addr;
            memcpy(&ipv4addr, &addr->sin6_addr.s6_addr[12], sizeof(ipv4addr));
            inet_ntop(AF_INET, &ipv4addr, ip, sizeof(ip));
        } else {
            inet_ntop(AF_INET6, &addr->sin6_addr, ip, INET6_ADDRSTRLEN);
        }
        port = ntohs(addr->sin6_port);
    } else {
        SPDLOG_ERROR("未知网络协议: {}", address.ss_family);
        close(sd);
        return;
    }
    SPDLOG_DEBUG("收到新连接,sd = {}, ip={}, port={}", sd, ip, port);
    if (inBlackList(ip)) {
        SPDLOG_DEBUG("ip {} 在黑名单内，关闭连接", ip);
        close(sd);
        return;
    }
#ifdef HTTPS
    SSL *ssl = SSL_new(m_ctx);
    if (! ssl) {
        SPDLOG_ERROR("SSL_new return nullptr");
        close(sd);
        return;
    }
    if (SSL_set_fd(ssl, sd) != 1) {
        SPDLOG_ERROR("SSL_set_fd failed");
        SSL_free(ssl);
        close(sd);
        return;
    }
    new HttpConnect(m_epollfd, m_pipe[1], ssl, sd, ip, port);
#else
    new HttpConnect(m_epollfd, m_pipe[1], sd, ip, port);
#endif
}

void WebServer::stop() {
    m_run = false;
    uint8_t head[2];
    head[0] = 0x00; // type
    head[1] = 0x00; // length
    ssize_t ret = write(m_pipe[1], head, sizeof(head));
    (void) ret;
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
            SPDLOG_WARN("管道被关闭");
            return;
        } else {
            SPDLOG_WARN("从管道读取出错: {}", strerror(errno));
            return;
        }
    }

    SPDLOG_DEBUG("管道收到数据，共计{}字节", m_pipeBuf.size());
    size_t offset = 0;
    while (true) {
        auto size = m_pipeBuf.size() - offset;
        if (size < 2) break;

        const char *data = m_pipeBuf.data() + offset;
        uint8_t type = static_cast<uint8_t>(data[0]);
        uint8_t length = static_cast<uint8_t>(data[1]);
        data += 2;

        if (size - 2 < length) break;

        switch (type) {
        case 1: {
            std::string ip{ data, length };
            SPDLOG_DEBUG("添加黑名单: {}", ip);
            addBlackList(ip);
            break;
        }
        case 2: {
            if (length != sizeof(HttpConnect *)) {
                SPDLOG_ERROR("删除文件描述符时长度错误，预期: {}， 实际: {}", sizeof(HttpConnect *), length);
            } else {
                HttpConnect *conn = nullptr;
                memcpy(&conn, data, sizeof(HttpConnect *));
                SPDLOG_DEBUG("管道删除文件描述符 {}", (int) *conn);
                bool ret = m_pool->cancelAndDeleteJob(conn);
                if (! ret) delete conn;
            }
            break;
        }
        }

        offset += length + 2; // 移动到下一帧
    }

    if (offset == m_pipeBuf.size()) {
        m_pipeBuf.clear();
    } else  if (offset > 0) {
        m_pipeBuf.erase(m_pipeBuf.begin(), m_pipeBuf.begin() + offset);
    }
}

void WebServer::addBlackList(const std::string & ip, bool save) {
    std::string trimmed = trim(ip);
    if (trimmed.length() == 0) return;
    if (trimmed == "localhost" || trimmed == "::1" || trimmed == "127.0.0.1") return;
    auto iter = m_blackList.insert(trimmed);
    if (iter.second && save) {
        saveBlackList();
    }
}

void WebServer::removeBlackList(const std::string & ip, bool save) {
    int ret = m_blackList.erase(ip);
    if (ret > 0 && save) {
        saveBlackList();
    }
}

void WebServer::loadBlackList() {
    m_blackList.clear();
    std::ifstream fin(m_blackListFile);
    if (!fin.is_open()) {
        SPDLOG_WARN("无法打开黑名单文件{}: {}", m_blackListFile, std::strerror(errno));
        return;
    }

    std::string ip;
    while (std::getline(fin, ip)) {
        std::string trimmed = trim(ip);
        if (trimmed.length() != 0) {
            m_blackList.insert(trimmed);
        }
    }
    SPDLOG_DEBUG("加载黑名单完成，共计{}个ip", m_blackList.size());
    fin.close();
}

void WebServer::saveBlackList() {
    std::ofstream fout(m_blackListFile, std::ofstream::out | std::ofstream::trunc);
    if (!fout.is_open()) {
        SPDLOG_WARN("无法写入黑名单文件{}: {}", m_blackListFile, std::strerror(errno));
        return;
    }

    for (const auto &ip : m_blackList) {
        fout << ip << "\n";
    }
    fout.close();
    SPDLOG_DEBUG("保存黑名单完成，共计{}个ip", m_blackList.size());
}

bool WebServer::inBlackList(const std::string & ip) {
    return m_blackList.find(ip) != m_blackList.end();
}

std::string WebServer::trim(const std::string & str) {
    size_t start = str.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = str.find_last_not_of(" \t\r\n");
    return str.substr(start, end - start + 1);
}

#ifdef HTTPS
int WebServer::alpnSelectCb(SSL * ssl, const unsigned char **out, unsigned char *outlen,
    const unsigned char *in, unsigned int inlen, void *arg) {
    (void) ssl;
    (void) arg;

    static const unsigned char proto[] = { 8, 'h','t','t','p','/','1','.','1' };
    constexpr const int len = sizeof(proto);
    int ret = SSL_select_next_proto((unsigned char **) out, outlen, proto, len, in, inlen);
    if (ret == OPENSSL_NPN_NEGOTIATED) {
        return SSL_TLSEXT_ERR_OK;
    }
    return SSL_TLSEXT_ERR_ALERT_FATAL;
}
#endif
