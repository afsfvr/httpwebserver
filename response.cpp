#include <map>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/sendfile.h>
#include <sys/socket.h>

#include "response.h"

Response::Response(bool &w, bool &chunk, int &status, int &size, int sd, std::map<std::string, std::string, case_insensitive_compare> &headers): m_write(w), m_chunk(chunk), m_status(status), m_size(size), m_sd(sd), m_headers(headers) {}

void Response::addHeader(const std::string &key, const std::string &value) {
    if (! m_write) m_headers.emplace(key, value);
}

std::string* Response::getHeader(const std::string &key) {
    auto it = m_headers.find(key);
    if (it == m_headers.end()) {
        return nullptr;
    } else {
        return &it->second;
    }
}

std::map<std::string, std::string, case_insensitive_compare>& Response::getHeaders() {
    return m_headers;
}

std::string Response::decimalToHex(int num) {
    if (num == 0) return "0";
    bool b = false;
    if (num < 0) {
        b = true;
        num = -num;
    }
    std::string s;
    while (num > 0) {
        int remainder = num % 16;
        if (remainder < 10) {
            s.push_back('0' + remainder);
        } else {
            s.push_back('A' + remainder - 10);
        }
        num /= 16;
    }
    if (b) s.push_back('-');
    for (int i = 0, j = s.size() - 1; i < j; i++, j--) {
        char c = s[i];
        s[i] = s[j];
        s[j] = c;
    }
    return s;
}

void Response::write_data(const void *buf, const size_t size) {
    if (size <= 0) return;
    if (size + m_size > MAX_BUFSIZE) {
        flush();
        if (m_chunk) {
            std::string s = decimalToHex(size).append("\r\n");
            write_len(s.data(), s.size(), 0);
        }
        write_len(buf, size, 0);
        if (m_chunk) write_len("\r\n", 2, 0);
    } else {
        memcpy(m_buf + m_size, buf, size);
        m_size += size;
    }
}

void Response::write_data(const std::string &str) {
    size_t size = str.size();
    if (size + m_size > MAX_BUFSIZE) {
        flush();
        if (m_chunk) {
            std::string s = decimalToHex(size).append("\r\n");
            write_len(s.data(), s.size(), 0);
        }
        write_len(str.data(), size, 0);
        if (m_chunk) write_len("\r\n", 2, 0);
    } else {
        memcpy(m_buf + m_size, str.data(), size);
        m_size += size;
    }
}

void Response::write_data(const void *buf, const size_t size, int flags) {
    if (size <= 0) return;
    flush();
    write_len(buf, size, flags);
}

void Response::write_len(const void *buf, size_t size, int flags) {
    if (size <= 0) return;
    while (size > 0) {
        int len = send(m_sd, buf, size, flags);
        if (len > 0) {
            size -= len;
        } else if (len < 0) {
            throw 103;
        } else {
            throw 3;
        }
    }
}

void Response::write_file(const std::string &filename) {
    struct stat st;
    if (stat(filename.c_str(), &st) == 0) {
        int fd = open(filename.c_str(), O_RDONLY);
        if (fd != -1) {
            flush();
            sendfile(m_sd, fd, 0, st.st_size);
            close(fd);
        }
    }
}
void Response::flush() {
    if (! m_write) {
        m_chunk = false;
        std::string buf = "HTTP/1.1 ";
        buf.append(std::to_string(m_status)).append("\r\n");
        for (auto it = m_headers.cbegin(); it != m_headers.cend(); it++) {
            buf.append(it->first).append(":").append(it->second).append("\r\n");
        }
        auto connection = m_headers.find("connection");
        if (m_headers.find("content-length") == m_headers.end() && connection != m_headers.end() && (connection->second[0] == 'K' || connection->second[0] == 'k')) {
            buf.append("Transfer-Encoding: chunked\r\n");
            m_chunk = true;
        }
        buf.append("\r\n");
        write_len(buf.data(), buf.size(), 0);
        m_write = true;
    }
    if (m_size > 0) {
        if (m_chunk) {
            std::string buf = decimalToHex(m_size).append("\r\n");
            write_len(buf.data(), buf.size(), 0);
        }
        write_len(m_buf, m_size, 0);
        if (m_chunk) write_len("\r\n", 2, 0);
    }
    m_size = 0;
}

void Response::setStatus(int status) {
    m_status = status;
}
