#include "base_class.h"
#include "blacklist.h"
#include "config.h"
#include "dynlib_cache.h"
#include "http_connect.h"
#include "util.h"

#ifdef USE_REDIS
#include "redis_pool.h"
extern RedisPool *pool;
#endif

extern std::string encoding;

enum class STATE {
    READ = 1,
    WRITE,
    CLOSE
};

/**
 * 1 recv end
 * 2 send head end
 * 3 send end
 * 4 keep-alive=false
 * 5 in black list
 * 101 recv err
 * 102 send head err
 * 103 send err
 */

HttpConnect::HttpConnect(const int &epollfd, const int &pipe,
#ifdef HTTPS
        SSL *ssl,
#endif
        const int &sd, const std::string &ip, const int &port): epollfd_{epollfd}, pipe_{pipe}, sd_{sd}, ip_{ip}, port_{port},
#ifdef HTTPS
        ssl_{ssl},
#endif
    request_{this}, response_{this} {
    setnonblock(sd_);
    int val = 1;
    if (setsockopt(sd_, IPPROTO_TCP, TCP_NODELAY, &val, sizeof(val)) < 0) { // 关闭Nagle
        SPDLOG_WARN("setsockopt TCP_NODELAY: {}", strerror(errno));
    }
    response_file_ptr_ = nullptr;
    keep_alive_ = true;
    init();
#ifdef HTTPS
    handshake_ = false;
#else
    proxy_need_ = Config::getInstance()->isProxyProtocol();
#endif
    struct epoll_event ev;
    ev.data.ptr = this;
    ev.events = EPOLLIN | EPOLLRDHUP | EPOLLET | EPOLLONESHOT;
    epoll_ctl(epollfd_, EPOLL_CTL_ADD, sd_, &ev);
}

HttpConnect::~HttpConnect() {
#ifdef HTTPS
    if (ssl_) {
        int ret = SSL_shutdown(ssl_);
        if (ret == 0) SSL_shutdown(ssl_);
        SSL_free(ssl_);
        ssl_ = nullptr;
    }
#endif
    shutdown(sd_, SHUT_RDWR);
    state_ = STATE::CLOSE;
    epoll_ctl(epollfd_, EPOLL_CTL_DEL, sd_, nullptr);
    if (response_file_ptr_ != nullptr) {
        munmap(response_file_ptr_, response_file_length_);
        response_file_ptr_ = nullptr;
    }
    close(sd_);
    sd_ = -1;
}

void HttpConnect::modfd(int ev) {
    pending_events_ = static_cast<uint32_t>(ev);
    pending_rearm_ = true;
}

void HttpConnect::flushEvents() {
    if (! pending_rearm_) return;
    pending_rearm_ = false;
    struct epoll_event ev;
    ev.data.ptr = this;
    ev.events = pending_events_ | EPOLLET | EPOLLRDHUP | EPOLLONESHOT;
    epoll_ctl(epollfd_, EPOLL_CTL_MOD, sd_, &ev);
}

void HttpConnect::pipeClose() {
    state_ = STATE::CLOSE;
    HttpConnect *conn = this;
    uint8_t data[2];
    data[0] = 0x02; // type
    data[1] = sizeof(HttpConnect*); // length
    struct iovec iov[2];
    iov[0].iov_base = data;
    iov[0].iov_len = sizeof(data);
    iov[1].iov_base = &conn;
    iov[1].iov_len = sizeof(HttpConnect*);
    ssize_t ret = writev(pipe_, iov, 2);
    (void) ret;
}

bool HttpConnect::isKeepAlive() const {
    if (request_headers_.find("transfer-encoding") != request_headers_.end()) return false;
    auto iter = request_headers_.find("connection");
    if (iter == request_headers_.end()) return true;
    return strncasecmp(iter->second.c_str(), "close", 5) != 0;
}

void HttpConnect::applyConnectionHeader() {
    if (isKeepAlive()) {
        keep_alive_ = true;
        response_headers_.emplace("Connection", "keep-alive");
    } else {
        keep_alive_ = false;
        response_headers_.emplace("Connection", "close");
    }
}

void HttpConnect::parseRequestBodyLength() {
    auto iter = request_headers_.find("content-length");
    if (iter != request_headers_.end()) {
        try {
            request_body_length_ = std::stoull(iter->second);
        } catch (const std::exception &e) {
            request_body_length_ = 0;
        }
    } else {
        request_body_length_ = 0;
    }
}

bool HttpConnect::waitFd(short events) {
    struct pollfd pfd{};
    pfd.fd = sd_;
    pfd.events = events;
    while (true) {
        int ret = poll(&pfd, 1, 5000);
        if (ret > 0) return true;
        if (ret == -1 && errno == EINTR) continue;
        return false;
    }
}

void HttpConnect::drainRequestBody() {
    if (! keep_alive_) return;
    if (request_body_length_ > 1024 * 1024) { // 大于1M的请求体直接关闭连接
        SPDLOG_WARN("socket:{}请求体过大({}),关闭连接", sd_, request_body_length_);
        throw 4;
    }
    if (request_read_byte_ > 0) { // 先消费缓冲区中残留的请求体字节
        int n = std::min(request_read_byte_, static_cast<int>(request_body_length_));
        if (n > 0) {
            request_read_byte_ -= n;
            request_body_length_ -= static_cast<size_t>(n);
            memmove(request_buf_, request_buf_ + n, static_cast<size_t>(request_read_byte_));
            memset(request_buf_ + request_read_byte_, 0, MAX_BUFSIZE - request_read_byte_);
        }
    }
    char tmp[1024];
    while (request_body_length_ > 0) {
        size_t min = std::min(1024ul, request_body_length_);
#ifdef HTTPS
        size_t len;
        int ret = SSL_read_ex(ssl_, tmp, min, &len);
        if (ret == 0) {
            int err = SSL_get_error(ssl_, ret);
            if (err == SSL_ERROR_ZERO_RETURN) throw 1; // 对端正常关闭了连接
            if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
                if (! waitFd(err == SSL_ERROR_WANT_READ ? POLLIN : POLLOUT)) {
                    SPDLOG_WARN("socket:{}等待请求体超时,关闭连接", sd_);
                    throw 4;
                }
                continue;
            }
            if (err == SSL_ERROR_SYSCALL) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    if (! waitFd(POLLIN)) {
                        SPDLOG_WARN("socket:{}等待请求体超时,关闭连接", sd_);
                        throw 4;
                    }
                    continue;
                }
                SPDLOG_ERROR("sd:{}, ssl: {}, 丢弃请求体syscall error: {}", sd_, err, strerror(errno));
                throw 4;
            }
            if (err == SSL_ERROR_SSL) {
                unsigned long e;
                char buf[256];
                while ((e = ERR_get_error()) != 0) {
                    ERR_error_string_n(e, buf, sizeof(buf));
                    SPDLOG_ERROR("sd:{}, ssl: {}, SSL error: {}", sd_, err, buf);
                }
                throw 4;
            }
            SPDLOG_ERROR("sd:{}, ssl: {}, 丢弃请求体unknown error", sd_, err);
            throw 4;
        }
#else
        ssize_t len = recv(sd_, tmp, min, 0);
        if (len == 0) throw 1; // 对端关闭了连接
        if (len == -1) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                SPDLOG_ERROR("sd:{},丢弃请求体recv:{}", sd_, strerror(errno));
                throw 4;
            }
            if (! waitFd(POLLIN)) {
                SPDLOG_WARN("socket:{}等待请求体超时,关闭连接", sd_);
                throw 4;
            }
            continue;
        }
#endif
        request_body_length_ -= len;
    }
}

void HttpConnect::init() {
    if (response_file_ptr_ != nullptr) {
        munmap(response_file_ptr_, response_file_length_);
        response_file_ptr_ = nullptr;
    }
    if (! keep_alive_) {
        SPDLOG_DEBUG("keep-alive=false,关闭socket={}", sd_);
        throw 4;
    }
    keep_alive_ = false;
    memset(request_buf_, '\0', MAX_BUFSIZE);
    request_read_byte_ = 0;
    request_headers_.clear();
    request_params_.clear();
    state_ = STATE::READ;
    method_.clear();
    url_.clear();
    response_send_byte_ = 0;
    response_have_byte_ = 0;
    response_file_length_ = 0;
    request_body_length_ = 0;
    response_send_head_.clear();
    lib_file_.clear();
    response_write_ = false;
    response_chunk_ = false;
    response_state_ = 0;
    response_size_ = 0;
    response_headers_.clear();
    response_cookies_.clear();
    forward_.clear();
    response_headers_.emplace("Content-Encoding", "identity");
#ifdef HTTPS
    response_headers_.emplace("Strict-Transport-Security", "max-age=31536000; includeSubDomains; preload");
#endif
}

int HttpConnect::setblock(const int &fd) {
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option & ~O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

int HttpConnect::setnonblock(const int &fd) {
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

void HttpConnect::readData() {
    if (request_read_byte_ == MAX_BUFSIZE) {
        request_buf_[request_read_byte_ - 1] = '\0';
        request_headers_["connection"] = "close";
        if (url_.empty()) {
            setResponseState(414, "<h1>414</h1>");
            SPDLOG_WARN("请求URI过大，超过缓冲区大小:{}", request_buf_);
        } else {
            setResponseState(431, "<h1>431</h1>");
            SPDLOG_WARN("单行请求头过大，超过缓冲区大小:{}", request_buf_);
        }
        state_ = STATE::WRITE;
        setCookie();
        modfd(EPOLLOUT);
        return;
    }
    while (request_read_byte_ < MAX_BUFSIZE) {
#ifdef HTTPS
        size_t len;
        int ret = SSL_read_ex(ssl_, request_buf_ + request_read_byte_, MAX_BUFSIZE - request_read_byte_, &len);
        if (ret == 0) {
            int err = SSL_get_error(ssl_, ret);
            if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) break;
            if (err == SSL_ERROR_ZERO_RETURN) throw 101; // 对端正常关闭了连接
            if (err == SSL_ERROR_SYSCALL) {
                if (errno != EAGAIN && errno != EWOULDBLOCK) {
                    SPDLOG_ERROR("sd:{}, ssl: {}, recv error: {}", sd_, err, strerror(errno));
                    throw 101;
                }
                break;
            }
            if (err == SSL_ERROR_SSL) {
                unsigned long e;
                char buf[256];
                while ((e = ERR_get_error()) != 0) {
                    ERR_error_string_n(e, buf, sizeof(buf));
                    SPDLOG_ERROR("sd:{}, ssl: {}, SSL error: {}", sd_, err, buf);
                }
                throw 101;
            }
            SPDLOG_ERROR("sd:{}, ssl: {}, unknown error", sd_, err);
            throw 101;
        }
#else
        ssize_t len = recv(sd_, request_buf_ + request_read_byte_, MAX_BUFSIZE - request_read_byte_, 0);
        if (len == 0) throw 1;
        if (len == -1) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                SPDLOG_ERROR("sd:{},recv:{}", sd_, strerror(errno));
                throw 101;
            }
            break;
        }
#endif
        request_read_byte_ += len;
    }
    parse();
}

void HttpConnect::parse() {
    char *str = request_buf_;
    char *s = strstr(str, "\r\n");
#ifndef HTTPS
    if (proxy_need_ && s && url_.empty()) {
        proxy_need_ = false;
        if (s - str >= 6 && memcmp(str, "PROXY ", 6) == 0) {
            *s++ = '\0';
            *s++ = '\0';
            size_t len = strlen(str) + 2;
            request_read_byte_ -= len;
            if (! parseProxyLine(str)) {
                SPDLOG_WARN("socket:{} PROXY协议头非法: {}", sd_, str);
                request_headers_["connection"] = "close";
                state_ = STATE::WRITE;
                setResponseState(400, "<h1>400</h1>");
                setCookie();
                modfd(EPOLLOUT);
                return;
            }
            str = s;
            s = strstr(str, "\r\n");
        }
    }
#endif
    while (s != nullptr && request_read_byte_ > 0) {
        *s++ = '\0';
        *s++ = '\0';
        size_t len = strlen(str) + 2;
        request_read_byte_ -= len;
        if (url_.empty()) {
            parseLine(str);
        } else {
            parseHead(str);
        }
        if (state_ == STATE::WRITE) {
            setCookie();
            str = s;
            break;
        }
        str = s;
        s = strstr(str, "\r\n");
    }
    memmove(request_buf_, str, request_read_byte_);
    memset(request_buf_ + request_read_byte_, 0, MAX_BUFSIZE - request_read_byte_);
    if (state_ == STATE::WRITE) {
        auto iter = request_headers_.find("X-Forwarded-For");
        if (iter != request_headers_.end()) {
            std::stringstream ss{iter->second};
            std::string item;

            while (std::getline(ss, item, ',')) {
                forward_.push_back(util::trim(item));
            }
        }
        modfd(EPOLLOUT);
    } else {
        modfd(EPOLLIN);
    }
}

void HttpConnect::parseLine(char *data) {
#ifdef USE_NGINX
    SPDLOG_INFO("socket:{} request:{}", sd_, data);
#else
    SPDLOG_INFO("socket:{} request:{}, ip: {}, port: {}", sd_, data, ip_, port_);
#endif
    char *url = strpbrk(data, " \t");
    if (url == nullptr) {
        request_headers_["connection"] = "close";
        state_ = STATE::WRITE;
        setResponseState(400, "<h1>400</h1>");
        SPDLOG_ERROR("解析url出错: nullptr, data: {}", data);
        return;
    }
    *url++ = '\0';
    for (int i = 0; data[i] != '\0'; i++) {
        if (data[i] >= 'a' && data[i] <= 'z')
            data[i] = data[i] - 32;
    }
    method_ = data;
    data = url;
    char *http_version = strpbrk(url, " \t");
    if (http_version == nullptr) {
        request_headers_["connection"] = "close";
        state_ = STATE::WRITE;
        setResponseState(400, "<h1>400</h1>");
        SPDLOG_ERROR("解析http version出错: nullptr, data: {}", data);
        return;
    }
    *http_version++ = '\0';
    data = strchr(url, '?');
    if (data != nullptr) {
        *data++ = '\0';
        parseParam(data);
    }
    url_ = util::urlDecode(url);
    size_t index = url_.find_first_not_of('/');
    if (index == std::string::npos) {
        url_ = Config::getInstance()->getRootUrl();
    } else {
        size_t index2 = url_.find_first_of('/', index);
        std::string path = Config::getInstance()->getWebappsPath();
        if (index2 == std::string::npos) {
            path.append(url_.substr(index));
        } else {
            path.append(url_.substr(index, (index2 - index)));
        }
        path.append("/libmain.so");
        if (access(path.c_str(), F_OK) == -1) {
            url_.erase(0, 1);
            url_.insert(0, Config::getInstance()->getRootUrl());
        }
    }
}

void HttpConnect::parseHead(char *data) {
    data += strspn(data, " ");
    char *value = strchr(data, ':');
    if (value == nullptr) {
        state_ = STATE::WRITE;
        if (data[0] != '\0') {
            request_headers_["connection"] = "close";
            setResponseState(400, "<h1>400</h1>");
            SPDLOG_ERROR("解析请求头出错, 未找到':', data: {}", data);
        }
        return;
    }
    *value++ = '\0';
    value += strspn(value, " ");
    request_headers_.emplace(data, value);
#ifdef USE_NGINX
    if (strncasecmp("x-real-", data, 7) == 0) {
        if (strncasecmp("ip", data + 7, 2) == 0) {
            ip_ = value;
        } else if (strncasecmp("port", data + 7, 4) == 0) {
            port_ = atoi(value);
        }
    }
#endif
}

void HttpConnect::parseParam(char *data) {
    char *param = data;
    while ((param = strchr(data, '&')) != nullptr) {
        *param++ = '\0';
        char *value = strchr(data, '=');
        if (value != nullptr) {
            *value++ = '\0';
            request_params_.emplace(util::urlDecode(data), util::urlDecode(value));
        } else {
            SPDLOG_WARN("请求参数{}未包含=", data);
        }
        data = param;
    }
    char *value = strchr(data, '=');
    if (value != nullptr) {
        *value++ = '\0';
        request_params_.emplace(util::urlDecode(data), util::urlDecode(value));
    } else {
        SPDLOG_WARN("请求参数{}未包含=", data);
    }
}

#ifndef HTTPS
bool HttpConnect::parseProxyLine(const char *line) {
    char proto[8] = {}, src[46] = {}, dst[46] = {};
    unsigned int sport = 0, dport = 0;
    char extra = 0;
    const int n = sscanf(line, "PROXY %7s %45s %45s %5u %5u %c", proto, src, dst, &sport, &dport, &extra);
    if (strcmp(proto, "UNKNOWN") == 0) {
        SPDLOG_INFO("socket:{} PROXY协议:UNKNOWN,保持原ip: {}, port: {}", sd_, ip_, port_);
        return true;
    }
    if (n == 5 && (strcmp(proto, "TCP4") == 0 || strcmp(proto, "TCP6") == 0)) {
        if (sport > 65535 || dport > 65535) {
            SPDLOG_WARN("socket:{} PROXY协议端口非法: {}", sd_, line);
            return false;
        }
        if (strcmp(proto, "TCP4") == 0) {
            in_addr v4src{}, v4dst{};
            if (inet_pton(AF_INET, src, &v4src) != 1 || inet_pton(AF_INET, dst, &v4dst) != 1) {
                SPDLOG_WARN("socket:{} PROXY协议ipv4非法: {}", sd_, line);
                return false;
            }
            ip_ = src;
        } else {
            in6_addr v6src{}, v6dst{};
            if (inet_pton(AF_INET6, src, &v6src) != 1 || inet_pton(AF_INET6, dst, &v6dst) != 1) {
                SPDLOG_WARN("socket:{} PROXY协议ipv6非法: {}", sd_, line);
                return false;
            }
            char text[INET6_ADDRSTRLEN] = {};
            if (IN6_IS_ADDR_V4MAPPED(&v6src)) {
                in_addr v4{};
                memcpy(&v4, v6src.s6_addr + 12, sizeof(v4));
                inet_ntop(AF_INET, &v4, text, sizeof(text));
            } else {
                inet_ntop(AF_INET6, &v6src, text, sizeof(text));
            }
            ip_ = text;
        }
        port_ = static_cast<int>(sport);
        SPDLOG_INFO("socket:{} PROXY协议头解析成功, ip: {}, port: {}", sd_, ip_, port_);
        if (BlackList::getInstance()->contains(ip_)) {
            SPDLOG_INFO("socket:{} 真实ip {} 在黑名单内，关闭连接", sd_, ip_);
            throw 5;
        }
        return true;
    }
    return false;
}
#endif

HttpConnect::operator int() {
    return sd_;
}

bool HttpConnect::operator==(const Task *task) {
    if (this == task) {
        return true;
    }
    const HttpConnect *p = dynamic_cast<const HttpConnect*>(task);
    return p != nullptr && this->sd_ == p->sd_;
}

void HttpConnect::run() {
    try {
#ifdef HTTPS
        if (! handshake_) {
            handshake();
        } else
#endif
        if (state_ == STATE::READ) {
            readData();
        } else if (state_ == STATE::WRITE) {
            writeData();
        }
    } catch (int i) {
        SPDLOG_DEBUG("抛出了异常: {}", i);
        pipeClose();
        return;
    } catch (...) {
        SPDLOG_DEBUG("发生了未知异常");
        pipeClose();
        return;
    }
    flushEvents();
}

void HttpConnect::writeData() {
    if (response_state_ == 0) {
        parseRequestBodyLength();
        initWriteLib();
        if (! lib_file_.empty()) {
            setblock(sd_);
            try {
                bool ret = runDynamicLib();
                setnonblock(sd_);
                if (! ret) {
                    setResponseState(500, "<h1>500</h1>");
                    SPDLOG_DEBUG("socket:{},运行动态库错误", sd_);
                }
            } catch (int ex) {
                setnonblock(sd_);
                throw ex;
            }
        }
        drainRequestBody();
    }

    if (! response_send_head_.empty() && response_have_byte_ > 0 && response_file_ptr_ == nullptr) {
        response_send_head_.append(response_buf_, response_have_byte_);
        response_have_byte_ = 0;
    }
    if (! writeHead()) return;
    while (true) {
        if (response_have_byte_ == 0) {
            init();
            modfd(EPOLLIN);
            return;
        }
        const char *data = response_file_ptr_;
        if (data == nullptr) {
            data = response_buf_;
        }
#ifdef HTTPS
        size_t len;
        int ret = SSL_write_ex(ssl_, data + response_send_byte_, response_have_byte_, &len);
        if (ret == 0) {
            int err = SSL_get_error(ssl_, ret);
            if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
                modfd(EPOLLOUT);
                return;
            } else if (err == SSL_ERROR_ZERO_RETURN) { // 对端正常关闭了连接
                SPDLOG_INFO("sd:{} peer closed connection", sd_);
                throw 103;
            } else if (err == SSL_ERROR_SYSCALL) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    modfd(EPOLLOUT);
                    return;
                }
                SPDLOG_ERROR("sd:{}, ssl: {} send syscall error: {}", sd_, err, strerror(errno));
                throw 103;
            } else if (err == SSL_ERROR_SSL) {
                unsigned long e;
                char buf[256];
                while ((e = ERR_get_error()) != 0) {
                    ERR_error_string_n(e, buf, sizeof(buf));
                    SPDLOG_ERROR("sd:{}, ssl: {} SSL error: {}", sd_, err, buf);
                }
                throw 103;
            } else {
                SPDLOG_ERROR("sd:{}, ssl: {} send unknown error", sd_, err);
                throw 103;
            }
        }
#else
        ssize_t len = send(sd_, data + response_send_byte_, response_have_byte_, MSG_NOSIGNAL);
        if (len == 0) throw 3;
        if (len < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                SPDLOG_ERROR("sd:{},send:{}", sd_, strerror(errno));
                throw 103;
            } else {
                modfd(EPOLLOUT);
                return;
            }
        }
#endif
        response_send_byte_ += len;
        response_have_byte_ -= len;
    }
}

bool HttpConnect::writeHead() {
    while (! response_send_head_.empty()) {
#ifdef HTTPS
        size_t len;
        int ret = SSL_write_ex(ssl_, response_send_head_.c_str(), response_send_head_.size(), &len);
        if (ret == 0) {
            int err = SSL_get_error(ssl_, ret);
            if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
                modfd(EPOLLOUT);
                return false;
            } else if (err == SSL_ERROR_ZERO_RETURN) { // 对端正常关闭了连接
                SPDLOG_INFO("sd:{} peer closed connection", sd_);
                throw 102;
            } else if (err == SSL_ERROR_SYSCALL) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    modfd(EPOLLOUT);
                    return false;
                }
                SPDLOG_ERROR("sd:{}, ssl: {} send syscall error: {}", sd_, err, strerror(errno));
                throw 102;
            } else if (err == SSL_ERROR_SSL) {
                unsigned long e;
                char buf[256];
                while ((e = ERR_get_error()) != 0) {
                    ERR_error_string_n(e, buf, sizeof(buf));
                    SPDLOG_ERROR("sd:{}, ssl: {} SSL error: {}", sd_, err, buf);
                }
                throw 102;
            } else {
                SPDLOG_ERROR("sd:{}, ssl: {} send unknown error", sd_, err);
                throw 102;
            }
        }
#else
        ssize_t len = send(sd_, response_send_head_.c_str(), response_send_head_.size(), MSG_NOSIGNAL);
        if (len == 0) throw 2;
        if (len < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                SPDLOG_ERROR("sd:{},send head:{}", sd_, strerror(errno));
                throw 102;
            } else {
                modfd(EPOLLOUT);
            }
            return false;
        }
#endif
        response_send_head_.erase(0, len);
    }
    return true;
}

void HttpConnect::initWriteLib() {
    int i1 = -1, i2 = url_.size();
    for (size_t i = 0; i < url_.size(); i++) {
        if (i1 == -1) {
            if (url_[i] != '/') i1 = i;
        } else {
            if (url_[i] == '/') {
                i2 = i;
                break;
            }
        }
    }
    if (i1 == -1 || i2 == 0) {
        setResponseState(400, "<h1>400</h1>");
        SPDLOG_DEBUG("socket:{},解析动态库路径错误, url: {}", sd_, url_);
        return;
    }
    Config *config = Config::getInstance();
    lib_file_ = config->getWebappsPath();
    lib_file_.append(url_.substr(i1, i2 - i1)).append("/libmain.so");
    if (! util::isFile(lib_file_)) {
        SPDLOG_DEBUG("动态库路径错误:{}", lib_file_);
        lib_file_.clear();
        setResponseState(404, "<h1>404</h1>");
    }
}

void HttpConnect::initWriteFile(const std::string &filename) {
    if (response_state_ != 0) {
        return;
    }
    SPDLOG_DEBUG("init write file: {}", filename);
    struct stat st;
    if (stat(filename.c_str(), &st) == -1 || S_ISDIR(st.st_mode)) {
        setResponseState(404, "<h1>404</h1>");
        return;
    }
    std::string timebuf = util::httpDateOf(st.st_mtim.tv_sec);
    response_headers_.insert_or_assign("Last-Modified", timebuf);
    auto iter = request_headers_.find("If-Modified-Since");
    if (iter != request_headers_.end() && iter->second == timebuf) {
        setResponseState(304);
        return;
    }
    if (st.st_size == 0) {
        response_headers_.insert_or_assign("Content-Length", "0");
        setResponseState(200);
        return;
    }

    if (! (st.st_mode & S_IROTH)) { // 不可读
        SPDLOG_WARN("{}不可读", filename);
        setResponseState(403, "<h1>403</h1>");
    } else {
        int fd = open(filename.c_str(), O_RDONLY | O_CLOEXEC);
        if (fd < 0) {
            SPDLOG_ERROR("打开文件{}失败{}", filename, strerror(errno));
            setResponseState(500, "<h1>500</h1>");
            return;
        }
        response_file_ptr_ = (char*)mmap(nullptr, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
        close(fd);
        if (reinterpret_cast<void*>(response_file_ptr_) == reinterpret_cast<void*>(-1)) {
            SPDLOG_ERROR("mmap失败{}", strerror(errno));
            setResponseState(500, "<h1>500</h1>");
            return;
        }

        const auto &type = Config::getInstance()->getType();
        size_t index = filename.find_last_of('.');
        if (index != std::string::npos) {
            auto citer = type.find(filename.substr(index + 1));
            if (citer != type.cend()) {
                response_headers_.emplace("Content-Type", citer->second);
            } else {
                response_headers_.emplace("Content-Type", type.find("other")->second);
            }
        } else {
            response_headers_.emplace("Content-Type", type.find("other")->second);
        }
        auto ret = response_headers_.emplace("Accept-Ranges", "bytes");
        response_send_byte_ = 0;
        response_file_length_ = st.st_size;
        response_have_byte_ = st.st_size;

        auto iter = request_headers_.find("range");
        if (ret.first->second == "bytes" && iter != request_headers_.end()) {
            std::string str = iter->second;
            size_t index1 = str.find('='), index2 = str.find('-');
            size_t i1 = 0, i2 = response_file_length_;
            if (index1 != std::string::npos && index2 != std::string::npos) {
                try{
                    if (index1 + 1 != index2) {
                        i1 = std::stoull(str.substr(index1 + 1, index2).c_str());
                    }
                    if (index2 + 1 != str.size()) {
                        i2 = std::stoull(str.substr(index2 + 1).c_str());
                    }
                } catch (const std::exception &e) {
                    i1 = 0;
                    i2 = response_file_length_;
                }
            }
            if (i2 >= response_file_length_) i2--;
            if (i1 <= i2) {
                std::string value = "bytes ";
                value.append(std::to_string(i1)).append("-").append(std::to_string(i2)).append("/").append(std::to_string(response_file_length_));
                response_headers_.insert_or_assign("Content-Range", value);
                response_send_byte_ = i1;
                response_have_byte_ = i2 - i1 + 1;
                response_headers_.insert_or_assign("Content-Length", std::to_string(response_have_byte_));
                setResponseState(206);
            } else {
                setResponseState(416, "<h1>416</h1>");
                SPDLOG_DEBUG("sd: {}, range请求的范围错误: {}", sd_, str);
                return;
            }
        }
        if (response_state_ == 0) {
            response_headers_.insert_or_assign("Content-Length", std::to_string(response_have_byte_));
            setResponseState(200);
        }
    }
}

void HttpConnect::setCookie() {
#ifdef USE_REDIS
    session_id_ = 0;
    if (pool == nullptr) return;
    RedisConn redis = pool->get();
    if (! redis) return;
    auto iter = request_headers_.find("cookie");
    if (iter == request_headers_.end()) {
        uint64_t u;
        while (true) {
            u = ((static_cast<uint64_t>(time(nullptr)) & 0xffffffff) << 32) | rand();
            if (redis->saveSession(u)) break;
            if (! redis->live()) return;
        }
        session_id_ = u;
        response_headers_.emplace("set-cookie", "session=" + std::to_string(u) + ";path=/;");
        SPDLOG_INFO("url:{}添加cookie:{}", url_, response_headers_.find("set-cookie")->second);
    } else {
        std::string s = iter->second;
        size_t index = s.find("session");
        if (index != std::string::npos) {
            size_t index1 = s.find(';', index);
            index = s.find('=', index);
            if (index != std::string::npos) s = s.substr(index + 1, index1 - index - 1);
        }
        if (index == std::string::npos || s.empty()) {
            SPDLOG_WARN("cookie错误: {}", s);
            uint64_t u;
            while (true) {
                u = ((static_cast<uint64_t>(time(nullptr)) & 0xffffffff) << 32) | rand();
                if (redis->saveSession(u)) break;
                if (! redis->live()) return;
            }
            session_id_ = u;
            response_headers_.emplace("set-cookie", "session=" + std::to_string(u) + ";path=/;");
        } else {
            try {
                uint64_t session = std::stoull(s);
                bool l = redis->updateExpire(session);
                if (! l) throw std::invalid_argument("更新过期时间失败");
                session_id_ = session;
            } catch (std::exception &e) {
                uint64_t u;
                while (true) {
                    u = ((static_cast<uint64_t>(time(nullptr)) & 0xffffffff) << 32) | rand();
                    if (redis->saveSession(u)) break;
                    if (! redis->live()) return;
                }
                SPDLOG_WARN("cookie {}错误: {}", iter->second, e.what());
                session_id_ = u;
                response_headers_.emplace("set-cookie", "session=" + std::to_string(u) + ";path=/;");
            }
        }
    }
#endif
}

void HttpConnect::setResponseState(int s, const char *str) {
    applyConnectionHeader();
    response_headers_.emplace("Date", util::httpDateNow());
    response_state_ = s;
    response_send_head_ = "HTTP/1.1 ";
    response_send_head_.append(std::to_string(s)).append("\r\n");
    if (str != nullptr) response_headers_.erase("content-length");
    response_headers_.emplace("Content-Type", std::string("text/html;charset=").append(encoding));
    for (auto it = response_headers_.cbegin(); it != response_headers_.cend(); it++) {
        response_send_head_.append(it->first).append(":").append(it->second).append("\r\n");
    }
    for (auto it = response_cookies_.cbegin(); it != response_cookies_.cend(); ++it) {
        response_send_head_.append("Set-Cookie: ").append(it->to_string()).append("\r\n");
    }
    SPDLOG_TRACE("socket:{} response:{}", sd_, response_send_head_);
    if (str != nullptr) {
        response_send_head_.append("Content-Length: ").append(std::to_string(strlen(str))).append("\r\n\r\n").append(str);
        response_have_byte_ = 0;
    } else {
        response_send_head_.append("\r\n");
    }
}

bool HttpConnect::runDynamicLib() {
    if (lib_file_.empty()) return false;
    auto entry = DynamicLibCache::instance().acquire(lib_file_);
    if (entry == nullptr) return false;
    auto c = (entry->create)();
    applyConnectionHeader();
    try {
        char filename[256] = {'\0'};
        c->service(&request_, &response_, lib_file_.substr(0, lib_file_.find_last_of('/') + 1), filename);
        if (filename[0] != '\0' && ! response_write_) {
            std::string name = filename;
            if (! util::isFile(name)) {
                name = lib_file_.substr(0, lib_file_.find_last_of('/') + 1) + name;
            }
            initWriteFile(name);
            (entry->destroy)(c);
            return true;
        }
        if (! response_write_) {
            response_have_byte_ = response_size_;
            response_headers_.insert_or_assign("Content-Length", std::to_string(response_size_));
            if (response_state_ == 0) response_state_ = 200;
            setResponseState(response_state_);
        } else {
            SPDLOG_DEBUG("socket:{} lib response status: {}", sd_, response_state_);
            response_.flush();
            if (response_chunk_) {
                const char *buf = "0\r\n\r\n";
                int size = 5;
                while (size > 0) {
#ifdef HTTPS
                    size_t len;
                    int ret = SSL_write_ex(ssl_, buf, size, &len);
                    if (ret == 0) {
                        int err = SSL_get_error(ssl_, ret);
                        if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
                            std::this_thread::sleep_for(std::chrono::milliseconds(100));
                            continue;
                        }
                        throw 103;
                    }
                    size -= len;
#else
                    ssize_t len = send(sd_, buf, size, MSG_NOSIGNAL);
                    if (len > 0) {
                        size -= len;
                    } else if (len < 0) {
                        throw 103;
                    } else {
                        throw 3;
                    }
#endif
                }
            }
        }
    } catch (int except) {
        (entry->destroy)(c);
        throw except;
    }
    (entry->destroy)(c);
    return true;
}

#ifdef HTTPS
void HttpConnect::handshake() {
    if (handshake_) return;
    int ret = SSL_accept(ssl_);
    if (ret == 1) {
        modfd(EPOLLIN);
        SPDLOG_DEBUG("socket: {} tls握手完成", sd_);
        handshake_ = true;
        return;
    }
    int err = SSL_get_error(ssl_, ret);

    if (err == SSL_ERROR_WANT_READ) {
        modfd(EPOLLIN);
    } else if (err == SSL_ERROR_WANT_WRITE) {
        modfd(EPOLLOUT);
    } else {
        pipeClose();
    }
}
#endif
