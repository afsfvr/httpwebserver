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

extern std::string encoding;

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

HttpConnect::HttpConnect(const int &epollfd, const int &pipe, const int &sd, const std::string &ip, const int &port): epollfd(epollfd), m_pipe(pipe), m_sd(sd), m_ip(ip), m_port(port), request(m_sd, m_read_byte, m_buf, m_body_len, m_port, m_method, m_url, m_ip, headers, params), response(res_write, res_chunk, res_state, res_size, sd, res_headers) {
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

bool HttpConnect::write_data() {
    if (response_state == 0) {
        std::string filename(Config::getInstance()->getRootPath());
        filename.append(m_url);
        struct stat st;
        if (stat(filename.c_str(), &st) < 0) {
            LOG_INFO("文件%s不存在，尝试装载动态链接库", filename.c_str());
            return false;
        } else {
            setResponseState(filename, st);
        }
    }

    if (! write_head()) return true;
    while (true) {
        if (m_have_byte <= 0) {
            init();
            return true;
        }
        int len = send(m_sd, m_file_data + m_send_byte, m_have_byte, 0);
        if (len == 0) throw 3;
        if (len < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                LOG_ERROR("sd:%d,send:%s", m_sd, strerror(errno));
                throw 103;
            } else {
                modfd(EPOLLOUT);
                return true;
            }
        }
        m_send_byte += len;
        m_have_byte -= len;
    }
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
    if (m_method != "GET" && m_method != "POST") {
        m_state = STATE::WRITE;
        setResponseState(501, "<h1>501</h1>");
        LOG_WARN("不支持的方法:%s",data);
        return;
    }
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
    m_url = url;
    for (auto it = m_url.cbegin(); it != m_url.cend(); it++) {
        if (*it != '/') return;
    }
    m_url.append(Config::getInstance()->getMainFile());
}

void HttpConnect::parse_param(char *data) {
    char *param = data;
    while ((param = strchr(data, '&')) != nullptr) {
        *param++ = '\0';
        char *value = strchr(data, '=');
        if (value != nullptr) {
            *value++ = '\0';
            params.insert(std::make_pair(data, value));
        } else {
            LOG_WARN("请求参数%s未包含=", data);
        }
        data = param;
    }
    char *value = strchr(data, '=');
    if (value != nullptr) {
        *value++ = '\0';
        params.insert(std::make_pair(data, value));
    } else {
        LOG_WARN("请求参数%s未包含=", data);
    }
}

void HttpConnect::parse_head(char *data) {
    data += strspn(data, " ");
    char *value = strchr(data, ':');
    if (value == nullptr) {
        m_state = STATE::WRITE;
        if (strlen(data) != 0) {
            setResponseState(400, "<h1>400</h1>");
            LOG_ERROR("解析请求头出错");
        }
        return;
    }
    *value++ = '\0';
    value += strspn(value, " ");
    headers.emplace(data, value);
}

void HttpConnect::run() {
    try {
        if (m_state == STATE::READ) {
            read_data();
        } else if (m_state == STATE::WRITE) {
            if (! write_data()) {
                setblock(m_sd);
                bool b = exec_so();
                setnonblock(m_sd);
                if (b) {
                    init();
                } else {
                    setResponseState(404, "<h1>404</h1>");
                    write_data();
                }
            }
        }
    } catch (int i) {
        m_state = STATE::CLOSE;
        write(m_pipe, &m_sd, 4);
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

void HttpConnect::modfd(int ev) {
    struct epoll_event event;
    event.data.fd = m_sd;
    event.events = ev | EPOLLET | EPOLLRDHUP | EPOLLONESHOT;
    epoll_ctl(epollfd, EPOLL_CTL_MOD, m_sd, &event);
}

void HttpConnect::setResponseState(std::string &filename, struct stat &st) {
    if (! (st.st_mode & S_IROTH)) { // 不可读
        LOG_WARN("%s不可读", filename.c_str());
        setResponseState(403, "<h1>404</h1>");
    } else if (S_ISDIR(st.st_mode)) {
        LOG_WARN("%s是文件夹", filename.c_str());
        setResponseState(403, "<h1>404</h1>");
    } else {
        m_send_byte = 0;
        m_file_length = st.st_size;
        m_have_byte = st.st_size;
        int fd = open(filename.c_str(), O_RDONLY);
        m_file_data = (char*)mmap(nullptr, m_file_length, PROT_READ, MAP_PRIVATE, fd, 0);
        close(fd);

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
                setResponseState(206, nullptr);
                m_send_head.append("Content-Range: bytes ").append(std::to_string(i1)).append("-").append(std::to_string(i2)).append("/").append(std::to_string(m_file_length)).append("\r\n");
                m_send_byte = i1;
                m_have_byte = i2 - i1 + 1;
            } else {
                setResponseState(416, "<h1>416</h1>");
                return;
            }
        }
        if (response_state == 0) {
            setResponseState(200, nullptr);
        }

        m_send_head.append("Content-Length: ").append(std::to_string(m_have_byte));
        m_send_head.append("\r\nAccept-Ranges: bytes\r\nContent-Type: ");
        auto type = Config::getInstance()->getType();
        size_t index = filename.find_last_of('.');
        if (index != std::string::npos) {
            iter = type.find(filename.substr(index + 1));
            if (iter != type.end()) {
                m_send_head.append(iter->second);
            } else {
                m_send_head.append(type.find("other")->second);
            }
        } else {
            m_send_head.append(type.find("other")->second);
        }
        m_send_head.append("\r\n\r\n");
    }
}

void HttpConnect::setResponseState(int s, const char *str) {
    auto iter = headers.find("connection");
    if (iter != headers.end() && (iter->second[0] == 'K' || iter->second[0] == 'k')) {
        m_keep_alive = true;
    }
    response_state = s;
    m_send_head = "HTTP/1.1 ";
    m_send_head.append(std::to_string(s)).append("\r\nConnection: ").append(m_keep_alive?"keep-alive\r\n":"close\r\n");
    for (auto it = res_headers.cbegin(); it != res_headers.cend(); it++) {
        m_send_head.append(it->first).append(":").append(it->second).append("\r\n");
    }
    LOG_INFO("socket:%d response:%s", m_sd, m_send_head.c_str());
    if (str != nullptr) {
        m_send_head.append("Content-Length: ").append(std::to_string(strlen(str))).append("\r\n\r\n").append(str).append("\r\n\r\n");
        m_have_byte = 0;
    }
}

bool HttpConnect::exec_so() {
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
    if (i1 == -1 || i2 == 0) return false;
    Config *config = Config::getInstance();
    std::string so_path = config->getWebappsPath();
    so_path.append(m_url.substr(i1, i2 - i1)).push_back('/');
    void *handle = dlopen(std::string(so_path).append(config->getWebappsSo()).c_str(), RTLD_NOW);
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
                auto c = (reinterpret_cast<BaseClass*(*)()>(createClassFn))();
                auto iter = headers.find("connection");
                if (iter != headers.end() && (iter->second[0] == 'K' || iter->second[0] == 'k')) res_headers.emplace("Connection", "keep-alive");
                res_headers.emplace("content-type", std::string("text/html;charset=").append(encoding));
                iter = headers.find("content-length");
                if (iter != headers.end()) m_body_len = std::stoull(iter->second);
                else m_body_len = 0;
                try {
                    if (this->m_method == "POST") {
                        c->post(&request, &response, so_path);
                    } else {
                        c->get(&request, &response, so_path);
                    }
                    if (! res_write) response.flush();
                    if (res_chunk) response.write_len("0\r\n\r\n", 5, 0);
                    response.flush();
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
