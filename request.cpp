#include <cstring>
#include <sys/socket.h>

#include "request.h"

#if defined (USE_REDIS)
Request::Request(uint64_t &sessionId, int fd, int &read_byte, char *buf, size_t &body_len, int &port, std::string &method, std::string &url, std::string &ip, std::map<std::string, std::string, case_insensitive_compare> &headers, std::map<std::string, std::string> &params): m_session_id(sessionId), m_fd(fd), m_read_byte(read_byte), m_buf(buf), m_body_length(body_len), m_port(port), m_method(method), m_url(url), m_ip(ip), m_headers(headers), m_params(params) {}

Session Request::getSession() const {
    return m_session_id;
}
#else
Request::Request(int fd, int &read_byte, char *buf, size_t &body_len, int &port, std::string &method, std::string &url, std::string &ip, std::map<std::string, std::string, case_insensitive_compare> &headers, std::map<std::string, std::string> &params): m_fd(fd), m_read_byte(read_byte), m_buf(buf), m_body_length(body_len), m_port(port), m_method(method), m_url(url), m_ip(ip), m_headers(headers), m_params(params) {}
#endif

const int& Request::getPort() const {
    return m_port;
}

const std::string& Request::getMethod() const {
    return m_method;
}

const std::string& Request::getUrl() const {
    return m_url;
}

const std::string& Request::getIp() const {
    return m_ip;
}

const std::map<std::string, std::string, case_insensitive_compare>& Request::getHeaders() const {
    return m_headers;
}

const std::map<std::string, std::string>& Request::getParams() const {
    return m_params;
}

const std::string* Request::getHeader(const std::string &key) const {
    auto it = m_headers.find(key);
    if (it == m_headers.end()) {
        return nullptr;
    } else {
        return &it->second;
    }
}

const std::string* Request::getParam(const std::string &key) const {
    auto it = m_params.find(key);
    if (it == m_params.end()) {
        return nullptr;
    } else {
        return &it->second;
    }
}

size_t Request::read_body(char *dest, size_t len) {
    if (len <= 0 || m_body_length <= 0) return 0;
    size_t size = 0;
    if (m_read_byte > 0) {
        m_read_byte = std::min(static_cast<size_t>(m_read_byte), m_body_length);
        if (m_read_byte > static_cast<int>(len)) {
            memcpy(dest, m_buf, len);
            memmove(m_buf, m_buf + len, m_read_byte - len);
            m_read_byte -= len;
            m_body_length -= len;
            return len;
        } else {
            memcpy(dest, m_buf, m_read_byte);
            size += m_read_byte;
            len -= m_read_byte;
            m_body_length -= m_read_byte;
            m_read_byte = 0;
        }
    }
    size_t min = std::min(len, m_body_length);
    while (min > 0) {
        size_t tmp = recv(m_fd, dest + size, min, 0);
        if (tmp == 0) throw 1;
        if (tmp < 0) throw 2;
        size += tmp;
        min -= tmp;
        m_body_length -= tmp;
    }
    return size;
}
