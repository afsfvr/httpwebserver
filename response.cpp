#include "http_connect.h"
#include "response.h"

extern std::string encoding;

Response::Response(HttpConnect *conn): conn_{conn} {}

void Response::setContentLength(size_t len) {
    setHeader("Content-Length", std::to_string(len));
}

void Response::sendError(int num, const std::string &errmsg) {
    if (! conn_ || conn_->response_write_) return;
    conn_->response_state_ = num;
    size_t size = errmsg.size();
    setContentLength(errmsg.size());
    conn_->response_size_ = 0;
    flush();
    writeLen(errmsg.data(), size);
}

void Response::sendRedirect(const std::string &url) {
    if (! conn_ || conn_->response_write_) return;
    conn_->response_state_ = 302;
    setContentLength(0);
    conn_->response_size_ = 0;
    setHeader("Location", url);
    flush();
}

void Response::addCookie(const Cookie &cookie) {
    if (conn_ && !conn_->response_write_) conn_->response_cookies_.insert(cookie);
}

const Cookie *Response::getCookie(const std::string &name, const std::string &domain) const {
    if (! conn_ || conn_->response_write_) return nullptr;
    for (auto iter = conn_->response_cookies_.begin(); iter != conn_->response_cookies_.end(); ++iter) {
        if (iter->domain() == domain && iter->name() == name) return &(*iter);
    }
    return nullptr;
}

std::set<Cookie> *Response::getCookies() {
    if (conn_) return &conn_->response_cookies_;
    return nullptr;
}

bool Response::addHeader(const std::string &key, const std::string &value) {
    if (! conn_ || conn_->response_write_) return false;
    return conn_->response_headers_.insert(std::make_pair(key, value)).second;
}

void Response::setHeader(const std::string &key, const std::string &value) {
    if (! conn_ || conn_->response_write_) return;
    conn_->response_headers_.insert_or_assign(key, value);
}

std::string *Response::getHeader(const std::string &key) const {
    if (! conn_) return nullptr;
    auto it = conn_->response_headers_.find(key);
    if (it == conn_->response_headers_.end()) {
        return nullptr;
    } else {
        return &it->second;
    }
}

std::map<std::string, std::string, case_insensitive_compare> *Response::getHeaders() {
    if (! conn_) return nullptr;
    return &conn_->response_headers_;
}

std::string Response::decimalToHex(int num) const {
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

void Response::writeData(const void *buf, const size_t size) {
    if (! conn_ || size == 0) return;
    if (size + conn_->response_size_ > HttpConnect::MAX_BUFSIZE) {
        flush();
        if (conn_->response_chunk_) {
            std::string s = decimalToHex(size).append("\r\n");
            writeLen(s.data(), s.size(), MSG_MORE);
        }
        writeLen(buf, size, MSG_MORE);
        if (conn_->response_chunk_) writeLen("\r\n", 2);
    } else {
        memcpy(conn_->response_buf_ + conn_->response_size_, buf, size);
        conn_->response_size_ += size;
    }
}

void Response::writeData(const std::string &str) {
    writeData(str.data(), str.size());
}

void Response::writeLen(const void *buf, size_t size, int flags) const {
    if (!conn_ || conn_->sd_ < 0) return;
    if (size <= 0) return;
    while (size > 0) {
#ifdef HTTPS
        (void) flags;
        if (conn_->ssl_ == nullptr) return;
        size_t len;
        int ret = SSL_write_ex(conn_->ssl_, buf, size, &len);
        if (ret == 0) {
            int err = SSL_get_error(conn_->ssl_, ret);
            if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }
            throw 103;
        }
#else
        ssize_t len = send(conn_->sd_, buf, size, flags | MSG_NOSIGNAL);
        if (len < 0) throw 103;
#endif
        if (len > 0) {
            size -= len;
        } else {
            throw 3;
        }
    }
}

void Response::writeFile(const std::string &filename) {
    if (!conn_ || conn_->sd_ < 0) return;
    struct stat st;
    if (stat(filename.c_str(), &st) == 0) {
        int fd = open(filename.c_str(), O_RDONLY);
        if (fd != -1) {
            flush();
            if (conn_->response_chunk_) {
                std::string buf = decimalToHex(st.st_size).append("\r\n");
                writeLen(buf.data(), buf.size());
            }
            sendfile(conn_->sd_, fd, 0, st.st_size);
            close(fd);
            if (conn_->response_chunk_) writeLen("\r\n", 2);
        }
    }
}

void Response::flush() {
    if (! conn_) return;
    if (! conn_->response_write_) {
        conn_->response_chunk_ = false;
        std::string buf = "HTTP/1.1 ";
        if (conn_->response_state_ == 0) {
            conn_->response_state_ = 200;
            buf.append("200\r\n");
        } else {
            buf.append(std::to_string(conn_->response_state_)).append("\r\n");
        }
        conn_->response_headers_.emplace("Content-Type", std::string("text/html;charset=").append(encoding));
        for (auto it = conn_->response_headers_.cbegin(); it != conn_->response_headers_.cend(); ++it) {
            buf.append(it->first).append(":").append(it->second).append("\r\n");
        }
        for (auto it = conn_->response_cookies_.cbegin(); it != conn_->response_cookies_.cend(); ++it) {
            buf.append("Set-Cookie: ").append(it->to_string()).append("\r\n");
        }
        if (conn_->response_headers_.find("content-length") == conn_->response_headers_.end() && conn_->keep_alive_) {
            buf.append("Transfer-Encoding: chunked\r\n");
            conn_->response_chunk_ = true;
        }
        char buff[128] = { '\0' };
        time_t timestamp = time(nullptr);
        std::strftime(buff, sizeof(buff), "%a, %d %b %Y %H:%M:%S GMT", std::gmtime(&timestamp));
        buf.append("Date: ").append(buff);
        buf.append("\r\n\r\n");
        if (conn_->response_size_ > 0) {
            writeLen(buf.data(), buf.size());
        } else {
            writeLen(buf.data(), buf.size(), MSG_MORE);
        }
        conn_->response_write_ = true;
    }
    if (conn_->response_size_ > 0) {
        if (conn_->response_chunk_) {
            std::string buf = decimalToHex(conn_->response_size_).append("\r\n");
            writeLen(buf.data(), buf.size(), MSG_MORE);
        }
        writeLen(conn_->response_buf_, conn_->response_size_, MSG_MORE);
        if (conn_->response_chunk_) writeLen("\r\n", 2);
    }
    conn_->response_size_ = 0;
}

void Response::setStatus(int status) {
    if (conn_) conn_->response_state_ = status;
}

int Response::getStatus() const {
    if (conn_) return conn_->response_state_;
    return 0;
}

std::string Response::time_tToHttpDate(time_t timestamp) const {
    char buf[128] = { '\0' };
    std::strftime(buf, sizeof(buf), "%a, %d %b %Y %H:%M:%S GMT", std::gmtime(&timestamp));
    return buf;
}
