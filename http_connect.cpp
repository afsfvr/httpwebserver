#include "base_class.h"
#include "config.h"
#include "http_connect.h"

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
    srand(time(nullptr));
    setnonblock(sd_);
    response_file_ptr_ = nullptr;
    keep_alive_ = true;
    init();
#ifdef HTTPS
    handshake_ = false;
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

unsigned char HttpConnect::toHex(unsigned char x) const {
    if (x >= 'A' && x <= 'Z') return x - 'A' + 10;
    else if (x >= 'a' && x <= 'z') return x - 'a' + 10;
    else if (x >= '0' && x <= '9') return x - '0';
    else return 0;
}

std::string HttpConnect::urlDecode(const std::string& str) const {
    std::string ret;
    size_t length = str.length();
    for (size_t i = 0; i < length; i++) {
        if (i + 2 < length && str[i] == '%') {
            ret += (toHex(str[i + 1]) << 4) | toHex(str[i + 2]);
            i += 2;
        } else {
            ret += str[i];
        }
    }
    return ret;
}

void HttpConnect::modfd(int ev) {
    struct epoll_event event;
    event.data.ptr = this;
    event.events = ev | EPOLLET | EPOLLRDHUP | EPOLLONESHOT;
    epoll_ctl(epollfd_, EPOLL_CTL_MOD, sd_, &event);
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
    response_headers_.emplace("Content-Type", std::string("text/html;charset=").append(encoding));
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
        auto iter = request_headers_.find("connection");
        if (iter != request_headers_.end()) request_headers_.erase(iter);
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
            if (err == SSL_ERROR_ZERO_RETURN) throw 101;
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                SPDLOG_ERROR("sd:{}, ssl: {}, recv error: {}", sd_, err, strerror(errno));
                throw 101;
            }
            break;
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
                forward_.push_back(trim(item));
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
    url_ = urlDecode(url);
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
            request_params_.emplace(urlDecode(data), urlDecode(value));
        } else {
            SPDLOG_WARN("请求参数{}未包含=", data);
        }
        data = param;
    }
    char *value = strchr(data, '=');
    if (value != nullptr) {
        *value++ = '\0';
        request_params_.emplace(urlDecode(data), urlDecode(value));
    } else {
        SPDLOG_WARN("请求参数{}未包含=", data);
    }
}

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
            return;
        }
#endif
        if (state_ == STATE::READ) {
            readData();
        } else if (state_ == STATE::WRITE) {
            writeData();
        }
    } catch (int i) {
        SPDLOG_DEBUG("抛出了异常: {}", i);
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
        (void)ret;
    }
}

void HttpConnect::writeData() {
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
            // 处理请求体
            if (request_body_length_ > 1024 * 1024) { // 未处理请求体大于1M直接关闭连接
                throw 4;
            } else if (request_body_length_ > 0) {
                char tmp[1024];
                while (request_body_length_ > 0) {
                    size_t min = std::min(1024ul, request_body_length_);
#ifdef HTTPS
                    size_t len;
                    int ret = SSL_read_ex(ssl_, tmp, min, &len);
                    if (ret == 0) throw 4;
#else
                    ssize_t len = recv(sd_, tmp, min, 0);
                    if (len == 0) throw 1;
                    if (len == -1) throw 4;
#endif
                    request_body_length_ -= len;
                }
            }
        } catch (int ex) {
            setnonblock(sd_);
            throw ex;
        }
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
            } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
                SPDLOG_ERROR("sd:{}, ssl: {} send: {}", sd_, err, strerror(errno));
                throw 103;
            } else {
                modfd(EPOLLOUT);
                return;
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
            } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
                SPDLOG_ERROR("sd:{}, ssl: {}, send head: {}", sd_, err, strerror(errno));
                throw 102;
            } else {
                modfd(EPOLLOUT);
            }
            return false;
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
    if (response_state_ != 0) {
        return;
    }
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
    if (! isFile(lib_file_)) {
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
    char timebuf[32] = {'\0'};
    std::strftime(timebuf, sizeof(timebuf), "%a, %d %b %Y %H:%M:%S GMT", std::gmtime(&st.st_mtim.tv_sec));
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
        int fd = open(filename.c_str(), O_RDONLY);
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
    auto iter = request_headers_.find("connection");
    if (iter != request_headers_.end() && (iter->second[0] == 'K' || iter->second[0] == 'k')) {
        keep_alive_ = true;
        response_headers_.emplace("Connection", "keep-alive");
    }
    char buf[64] = {'\0'};
    time_t timestamp = time(nullptr);
    std::strftime(buf, sizeof(buf), "%a, %d %b %Y %H:%M:%S GMT", std::gmtime(&timestamp));
    response_headers_.emplace("Date", buf);
    response_state_ = s;
    response_send_head_ = "HTTP/1.1 ";
    response_send_head_.append(std::to_string(s)).append("\r\n");
    if (str != nullptr) response_headers_.erase("content-length");
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
    void *handle = dlopen(lib_file_.c_str(), RTLD_NOW);
    if (handle == nullptr) {
        SPDLOG_WARN("装载动态库出错:{}", dlerror());
        return false;
    } else {
        void *deleteClassFn = dlsym(handle, "deleteClass");
        if (deleteClassFn == nullptr) {
            SPDLOG_WARN("加载函数deleteClass出错:{}", dlerror());
            dlclose(handle);
            return false;
        } else {
            void *createClassFn = dlsym(handle, "createClass");
            if (createClassFn == nullptr) {
                SPDLOG_WARN("加载函数createClass出错:{}", dlerror());
                dlclose(handle);
                return false;
            } else {
                SPDLOG_DEBUG("动态链接库{}装载成功", lib_file_);
                auto c = (reinterpret_cast<BaseClass*(*)()>(createClassFn))();
                auto iter = request_headers_.find("connection");
                if (iter != request_headers_.end() && (iter->second[0] == 'K' || iter->second[0] == 'k')) {
                    keep_alive_ = true;
                    response_headers_.emplace("Connection", keep_alive_?"keep-alive":"close");
                }
                iter = request_headers_.find("content-length");
                if (iter != request_headers_.end()) {
                    try {
                        request_body_length_ = std::stoull(iter->second);
                    } catch (const std::exception &e) {
                        request_body_length_ = 0;
                    }
                } else {
                    request_body_length_ = 0;
                }
                try {
                    char filename[256] = {'\0'};
                    c->service(&request_, &response_, lib_file_.substr(0, lib_file_.find_last_of('/') + 1), filename);
                    if (filename[0] != '\0' && ! response_write_) {
                        std::string name = filename;
                        if (! isFile(name)) {
                            name = lib_file_.substr(0, lib_file_.find_last_of('/') + 1) + name;
                        }
                        initWriteFile(name);
                        (reinterpret_cast<void(*)(BaseClass*)>(deleteClassFn))(c);
                        dlclose(handle);
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
                    (reinterpret_cast<void(*)(BaseClass*)>(deleteClassFn))(c);
                    dlclose(handle);
                    throw except;
                }
                (reinterpret_cast<void(*)(BaseClass*)>(deleteClassFn))(c);
            }
        }
    }
    dlclose(handle);
    return true;
}

bool HttpConnect::isFile(const std::string &filename) const {
    struct stat st;
    return stat(filename.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

std::string HttpConnect::trim(const std::string & str) const {
    size_t start = str.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = str.find_last_not_of(" \t\r\n");
    return str.substr(start, end - start + 1);
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
        (void)ret;
        return;
    }
    return;
}
#endif
