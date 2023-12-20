#include <sys/epoll.h>
#include <dlfcn.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <iostream>
#include <cstring>

#include "base_class.h"
#include "log.h"
#include "config.h"
#include "http_connect.h"
#include "redis_pool.h"

extern std::string encoding;
extern RedisPool *pool;

enum class STATE{
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

HttpConnect::HttpConnect(const int &epollfd, const int &pipe, const int &sd, const std::string &ip, const int &port): epollfd(epollfd), m_pipe(pipe), m_sd(sd), m_ip(ip), m_port(port), request(res_session_id, m_sd, m_read_byte, m_buf, m_body_len, m_port, m_method, m_url, m_ip, headers, params), response(res_write, res_chunk, res_state, res_size, sd, m_keep_alive, res_headers) {
    srand(time(nullptr));
    setnonblock(m_sd);
    m_file_data = nullptr;
    struct epoll_event ev;
    ev.data.fd = m_sd;
    ev.events = EPOLLIN | EPOLLRDHUP | EPOLLET | EPOLLONESHOT;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, m_sd, &ev);
    m_keep_alive = true;
    init();
}

HttpConnect::~HttpConnect() {
    shutdown(m_sd, SHUT_RDWR);
    m_state = STATE::CLOSE;
    epoll_ctl(epollfd, EPOLL_CTL_DEL, m_sd, nullptr);
    if (m_file_data != nullptr) {
        munmap(m_file_data, m_file_length);
        m_file_data = nullptr;
    }
    close(m_sd);
    m_sd = -1;
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
    event.data.fd = m_sd;
    event.events = ev | EPOLLET | EPOLLRDHUP | EPOLLONESHOT;
    epoll_ctl(epollfd, EPOLL_CTL_MOD, m_sd, &event);
}

void HttpConnect::init() {
    if (m_file_data != nullptr) {
        munmap(m_file_data, m_file_length);
        m_file_data = nullptr;
    }
    if (! m_keep_alive) {
        LOG_DEBUG("keep-alive=false,关闭socket=%d", m_sd);
        throw 4;
    }
    m_keep_alive = false;
    memset(m_buf, '\0', MAX_BUFSIZE);
    m_read_byte = 0;
    headers.clear();
    params.clear();
    m_state = STATE::READ;
    m_method.clear();
    m_url.clear();
    m_send_byte = 0;
    m_have_byte = 0;
    m_file_length = 0;
    m_body_len = 0;
    m_send_head.clear();
    m_dynamic_lib_file.clear();
    response_state = 0;
    res_write = false;
    res_chunk = false;
    res_state = 200;
    res_size = 0;
    res_headers.clear();
    modfd(EPOLLIN);
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

void HttpConnect::read_data() {
    if (m_read_byte == MAX_BUFSIZE) {
        m_buf[m_read_byte - 1] = '\0';
        auto iter = headers.find("connection");
        if (iter != headers.end()) headers.erase(iter);
        if (m_url.empty()) {
            setResponseState(414, "<h1>414</h1>");
            LOG_WARN("请求URI过大，超过缓冲区大小:%s", m_buf);
        } else {
            setResponseState(431, "<h1>431</h1>");
            LOG_WARN("单行请求头过大，超过缓冲区大小:%s", m_buf);
        }
        m_state = STATE::WRITE;
        setCookie();
        modfd(EPOLLOUT);
        return;
    }
    while (m_read_byte < MAX_BUFSIZE) {
        int len = recv(m_sd, m_buf + m_read_byte, MAX_BUFSIZE - m_read_byte, 0);
        if (len == 0) throw 1;
        if (len == -1) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                LOG_ERROR("sd:%d,recv:%s", m_sd, strerror(errno));
                throw 101;
            }
            break;
        }
        m_read_byte += len;
    }
    parse();
}


void HttpConnect::parse() {
    char *str = m_buf;
    char *s = strstr(str, "\r\n");
    while (s != nullptr && m_read_byte > 0) {
        *s++ = '\0';
        *s++ = '\0';
        size_t len = strlen(str) + 2;
        m_read_byte -= len;
        if (m_url.empty()) {
            parse_line(str);
        } else {
            parse_head(str);
        }
        if (m_state == STATE::WRITE) {
            setCookie();
            str = s;
            break;
        }
        str = s;
        s = strstr(str, "\r\n");
    }
    for (int i = 0; i < m_read_byte; i++) {
        m_buf[i] = str[i];
    }
    memset(m_buf + m_read_byte, 0, MAX_BUFSIZE - m_read_byte);
    if (m_state == STATE::WRITE) {
        modfd(EPOLLOUT);
    } else {
        modfd(EPOLLIN);
    }
}

void HttpConnect::parse_line(char *data) {
    LOG_INFO("socket:%d request:%s", m_sd, data);
    char *url = strpbrk(data, " \t");
    if (url == nullptr) {
        m_state = STATE::WRITE;
        setResponseState(400, "<h1>400</h1>");
        LOG_ERROR("解析请求出错");
        return;
    }
    *url++ = '\0';
    for (int i = 0; data[i] != '\0'; i++) {
        if (data[i] >= 'a' && data[i] <= 'z')
            data[i] = data[i] - 32;
    }
    m_method = data;
    data = url;
    char *http_version = strpbrk(url, " \t");
    if (http_version == nullptr) {
        m_state = STATE::WRITE;
        setResponseState(400, "<h1>400</h1>");
        LOG_ERROR("解析请求出错");
        return;
    }
    *http_version++ = '\0';
    data = strchr(url, '?');
    if (data != nullptr) {
        *data++ = '\0';
        parse_param(data);
    }
    m_url = urlDecode(url);
    for (auto it = m_url.cbegin(); it != m_url.cend(); it++) {
        if (*it != '/') return;
    }
    m_url.append(Config::getInstance()->getMainFile());
}

void HttpConnect::parse_head(char *data) {
    data += strspn(data, " ");
    char *value = strchr(data, ':');
    if (value == nullptr) {
        m_state = STATE::WRITE;
        if (data[0] != '\0') {
            setResponseState(400, "<h1>400</h1>");
            LOG_ERROR("解析请求头出错");
        }
        return;
    }
    *value++ = '\0';
    value += strspn(value, " ");
    headers.emplace(data, value);
}

void HttpConnect::parse_param(char *data) {
    char *param = data;
    while ((param = strchr(data, '&')) != nullptr) {
        *param++ = '\0';
        char *value = strchr(data, '=');
        if (value != nullptr) {
            *value++ = '\0';
            params.emplace(urlDecode(data), urlDecode(value));
        } else {
            LOG_WARN("请求参数%s未包含=", data);
        }
        data = param;
    }
    char *value = strchr(data, '=');
    if (value != nullptr) {
        *value++ = '\0';
        params.emplace(urlDecode(data), urlDecode(value));
    } else {
        LOG_WARN("请求参数%s未包含=", data);
    }
}

void HttpConnect::run() {
    try {
        if (m_state == STATE::READ) {
            read_data();
        } else if (m_state == STATE::WRITE) {
            write_data();
        }
    } catch (int i) {
        m_state = STATE::CLOSE;
        write(m_pipe, &m_sd, 4);
    }
}

void HttpConnect::write_data() {
    init_write_data();
    if (! m_dynamic_lib_file.empty()) {
        setblock(m_sd);
        try {
            bool ret = run_dynamic_lib();
            setnonblock(m_sd);
            if (! ret) {
                setResponseState(501, "<h1>501</h1>");
            }
        } catch (int ex) {
            setnonblock(m_sd);
            throw ex;
        }
    }

    if (! write_head()) return;
    while (true) {
        if (m_have_byte <= 0) {
            init();
            return;
        }
        int len = send(m_sd, m_file_data + m_send_byte, m_have_byte, 0);
        if (len == 0) throw 3;
        if (len < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                LOG_ERROR("sd:%d,send:%s", m_sd, strerror(errno));
                throw 103;
            } else {
                modfd(EPOLLOUT);
                return;
            }
        }
        m_send_byte += len;
        m_have_byte -= len;
    }
}

bool HttpConnect::write_head() {
    while (! m_send_head.empty()) {
        int len = send(m_sd, m_send_head.c_str(), m_send_head.size(), 0);
        if (len == 0) throw 2;
        if (len < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                LOG_ERROR("sd:%d,send head:%s", m_sd, strerror(errno));
                throw 102;
            } else {
                modfd(EPOLLOUT);
            }
            return false;
        }
        m_send_head.erase(0, len);
    }
    return true;
}

void HttpConnect::init_write_data(std::string filename, bool load_lib) {
    if (response_state != 0) return;
    if (filename.empty()) {
        filename = Config::getInstance()->getRootPath();
        filename.append(m_url);
    }
    LOG_DEBUG("初始化写的数据，filename：%s", filename.c_str());
    struct stat st;
    if (stat(filename.c_str(), &st) == -1 || S_ISDIR(st.st_mode)) {
        if (load_lib) {
            int i1 = -1, i2 = m_url.size();
            for (size_t i = 0; i < m_url.size(); i++) {
                if (i1 == -1) {
                    if (m_url[i] != '/') i1 = i;
                } else {
                    if (m_url[i] == '/') {
                        i2 = i;
                        break;
                    }
                }
            }
            if (i1 == -1 || i2 == 0) return;
            Config *config = Config::getInstance();
            m_dynamic_lib_file = config->getWebappsPath();
            m_dynamic_lib_file.append(m_url.substr(i1, i2 - i1)).append("/").append(config->getWebappsSo());
            if (stat(m_dynamic_lib_file.c_str(), &st) == -1) {
                m_dynamic_lib_file.clear();
                setResponseState(404, "<h1>404</h1>");
            }
        } else {
            setResponseState(404, "<h1>404</h1>");
        }
    } else {
        char timebuf[32] = {'\0'};
        std::strftime(timebuf, sizeof(timebuf), "%a, %d %b %Y %H:%M:%S GMT", std::gmtime(&st.st_mtim.tv_sec));
        res_headers.emplace("Last-Modified", timebuf);
        auto iter = headers.find("If-Modified-Since");
        if (iter != headers.end() && iter->second == timebuf) {
            setResponseState(304, nullptr);
            return;
        }

        if (! (st.st_mode & S_IROTH)) { // 不可读
            LOG_WARN("%s不可读", filename.c_str());
            setResponseState(403, "<h1>404</h1>");
        } else if (S_ISDIR(st.st_mode)) {
            LOG_WARN("%s是文件夹", filename.c_str());
            setResponseState(403, "<h1>404</h1>");
        } else {
            int fd = open(filename.c_str(), O_RDONLY);
            if (fd < 0) {
                LOG_ERROR("打开文件%s失败%s", filename.c_str(), strerror(errno));
                setResponseState(501, "<h1>501</h1>");
                return;
            }
            m_file_data = (char*)mmap(nullptr, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
            close(fd);
            if (reinterpret_cast<void*>(m_file_data) == reinterpret_cast<void*>(-1)) {
                LOG_ERROR("mmap失败%s", strerror(errno));
                setResponseState(501, "<h1>501</h1>");
                return;
            }

            auto type = Config::getInstance()->getType();
            size_t index = filename.find_last_of('.');
            if (index != std::string::npos) {
                iter = type.find(filename.substr(index + 1));
                if (iter != type.end()) {
                    res_headers.emplace("Content-Type", iter->second);
                } else {
                    res_headers.emplace("Content-Type", type.find("other")->second);
                }
            } else {
                res_headers.emplace("Content-Type", type.find("other")->second);
            }
            res_headers.emplace("Accept-Ranges", "bytes");
            m_send_byte = 0;
            m_file_length = st.st_size;
            m_have_byte = st.st_size;

            auto iter = headers.find("range");
            if (iter != headers.end()) {
                std::string str = iter->second;
                size_t index1 = str.find('='), index2 = str.find('-');
                int i1 = 0, i2 = m_file_length;
                if (index1 != std::string::npos && index2 != std::string::npos) {
                    if (index1 + 1 != index2) {
                        i1 = atoi(str.substr(index1 + 1, index2).c_str());
                    }
                    if (index2 + 1 != str.size()) {
                        i2 = atoi(str.substr(index2 + 1).c_str());
                    }
                }
                if (i2 >= m_file_length) i2--;
                if (i1 <= i2) {
                    std::string value = "bytes ";
                    value.append(std::to_string(i1)).append("-").append(std::to_string(i2)).append("/").append(std::to_string(m_file_length));
                    res_headers.emplace("Content-Range", value);
                    m_send_byte = i1;
                    m_have_byte = i2 - i1 + 1;
                    res_headers.emplace("Content-Length", std::to_string(m_have_byte));
                    setResponseState(206, nullptr);
                } else {
                    setResponseState(416, "<h1>416</h1>");
                    return;
                }
            }
            if (response_state == 0) {
                res_headers.emplace("Content-Length", std::to_string(m_have_byte));
                setResponseState(200, nullptr);
            }
        }
    }
}

void HttpConnect::setCookie() {
    res_session_id = 0;
    RedisConn redis = pool->get();
    if (! redis) return;
    auto iter = headers.find("cookie");
    if (iter == headers.end()) {
        uint64_t u;
        while (true) {
            u = ((static_cast<uint64_t>(time(nullptr)) & 0xffffffff) << 32) | rand();
            if (redis->saveSession(u)) break;
            if (! redis->live()) return;
        }
        res_session_id = u;
        res_headers.emplace("set-cookie", "session=" + std::to_string(u) + ";path=/;");
        LOG_INFO("url:%s添加cookie:%s", m_url.c_str(), res_headers.find("set-cookie")->second.c_str());
    } else {
        std::string s = iter->second;
        size_t index = s.find("session");
        if (index != std::string::npos) {
            size_t index1 = s.find(';', index);
            index = s.find('=', index);
            if (index != std::string::npos) s = s.substr(index + 1, index1 - index - 1);
        }
        if (index == std::string::npos || s.empty()) {
            LOG_WARN("cookie错误:%s", s.c_str());
            uint64_t u;
            while (true) {
                u = ((static_cast<uint64_t>(time(nullptr)) & 0xffffffff) << 32) | rand();
                if (redis->saveSession(u)) break;
                if (! redis->live()) return;
            }
            res_session_id = u;
            res_headers.emplace("set-cookie", "session=" + std::to_string(u) + ";path=/;");
        } else {
            try {
                uint64_t session = std::stoull(s);
                bool l = redis->updateExpire(session);
                if (! l) throw std::invalid_argument("更新过期时间失败");
                res_session_id = session;
            } catch (std::exception &e) {
                uint64_t u;
                while (true) {
                    u = ((static_cast<uint64_t>(time(nullptr)) & 0xffffffff) << 32) | rand();
                    if (redis->saveSession(u)) break;
                    if (! redis->live()) return;
                }
                LOG_WARN("cookie %s错误:%s", iter->second.c_str(), e.what());
                res_session_id = u;
                res_headers.emplace("set-cookie", "session=" + std::to_string(u) + ";path=/;");
            }
        }
    }
}

void HttpConnect::setResponseState(int s, const char *str) {
    auto iter = headers.find("connection");
    if (iter != headers.end() && (iter->second[0] == 'K' || iter->second[0] == 'k')) {
        m_keep_alive = true;
        res_headers.emplace("Connection", "keep-alive");
    }
    response_state = s;
    m_send_head = "HTTP/1.1 ";
    m_send_head.append(std::to_string(s)).append("\r\n");
    for (auto it = res_headers.cbegin(); it != res_headers.cend(); it++) {
        m_send_head.append(it->first).append(":").append(it->second).append("\r\n");
    }
    LOG_INFO("socket:%d response:%s", m_sd, m_send_head.c_str());
    if (str != nullptr) {
        m_send_head.append("Content-Length: ").append(std::to_string(strlen(str))).append("\r\n\r\n").append(str);
        m_have_byte = 0;
    }
    m_send_head.append("\r\n");
}

bool HttpConnect::run_dynamic_lib() {
    if (m_dynamic_lib_file.empty()) return false;
    void *handle = dlopen(m_dynamic_lib_file.c_str(), RTLD_NOW);
    if (handle == nullptr) {
        LOG_WARN("装载动态库出错:%s", dlerror());
        return false;
    } else {
        void *deleteClassFn = dlsym(handle, "deleteClass");
        if (deleteClassFn == nullptr) {
            LOG_WARN("加载函数deleteClass出错:%s", dlerror());
            dlclose(handle);
            return false;
        } else {
            void *createClassFn = dlsym(handle, "createClass");
            if (createClassFn == nullptr) {
                LOG_WARN("加载函数createClass出错:%s", dlerror());
                dlclose(handle);
                return false;
            } else {
                LOG_DEBUG("动态链接库%s装载成功", m_dynamic_lib_file.c_str());
                auto c = (reinterpret_cast<BaseClass*(*)()>(createClassFn))();
                auto iter = headers.find("connection");
                if (iter != headers.end() && (iter->second[0] == 'K' || iter->second[0] == 'k')) {
                    m_keep_alive = true;
                    res_headers.emplace("Connection", m_keep_alive?"keep-alive":"close");
                }
                res_headers.emplace("Content-Type", std::string("text/html;charset=").append(encoding));
                iter = headers.find("content-length");
                if (iter != headers.end()) m_body_len = std::stoull(iter->second);
                else m_body_len = 0;
                try {
                    char filename[256] = {'\0'};
                    c->service(&request, &response, m_dynamic_lib_file.substr(0, m_dynamic_lib_file.find_last_of('/')), filename);
                    if (filename[0] != '\0' && ! res_write) {
                        init_write_data(filename, false);
                        (reinterpret_cast<void(*)(BaseClass*)>(deleteClassFn))(c);
                        dlclose(handle);
                        return true;
                    }
                    if (! res_write) {
                        res_headers.emplace("Content-Length", std::to_string(res_size));
                        response.flush();
                    }
                    if (res_chunk) {
                        const char *buf = "0\r\n\r\n";
                        int size = 5;
                        while (size > 0) {
                            int len = send(m_sd, buf, size, 0);
                            if (len > 0) {
                                size -= len;
                            } else if (len < 0) {
                                throw 103;
                            } else {
                                throw 3;
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
