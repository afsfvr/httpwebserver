#include "request.h"
#include "http_connect.h"

Request::Request(HttpConnect *conn): conn_{conn} {}

#ifdef USE_REDIS
Session Request::getSession() const {
    if (conn_) return conn_->session_id_;
    return 0;
}
#endif

int Request::getPort() const {
    if (conn_) return conn_->port_;
    return 0;
}

const std::string &Request::getMethod() const {
    static const std::string empty;
    if (conn_) return conn_->method_;
    return empty;
}

const std::string &Request::getUrl() const {
    static const std::string empty;
    if (conn_) return conn_->url_;
    return empty;
}

const std::string &Request::getIp() const {
    static const std::string empty;
    if (conn_) return conn_->ip_;
    return empty;
}

const std::map<std::string, std::string, case_insensitive_compare> &Request::getHeaders() const {
    static const std::map<std::string, std::string, case_insensitive_compare> empty;
    if (conn_) return conn_->request_headers_;
    return empty;
}

const std::map<std::string, std::string> &Request::getParams() const {
    static const std::map<std::string, std::string> empty;
    if (conn_) return conn_->request_params_;
    return empty;
}

std::optional<std::string> Request::getHeader(const std::string &key) const {
    if (conn_) {
        auto it = conn_->request_headers_.find(key);
        if (it != conn_->request_headers_.end()) {
            return it->second;
        }
    }
    return std::nullopt;
}

std::optional<std::string> Request::getParam(const std::string &key) const {
    if (conn_) {
        auto it = conn_->request_params_.find(key);
        if (it != conn_->request_params_.end()) {
            return it->second;
        }
    }
    return std::nullopt;
}

size_t Request::read_body(char *dest, size_t len) {
    if (len <= 0 || conn_ == nullptr || conn_->request_body_length_ <= 0) return 0;
    size_t size = 0;
    if (conn_->request_read_byte_ > 0) {
        conn_->request_read_byte_ = std::min(static_cast<size_t>(conn_->request_read_byte_), conn_->request_body_length_);
        if (conn_->request_read_byte_ > static_cast<int>(len)) {
            memcpy(dest, conn_->request_buf_, len);
            memmove(conn_->request_buf_, conn_->request_buf_ + len, conn_->request_read_byte_ - len);
            conn_->request_read_byte_ -= len;
            conn_->request_body_length_ -= len;
            return len;
        } else {
            memcpy(dest, conn_->request_buf_, conn_->request_read_byte_);
            size += conn_->request_read_byte_;
            len -= conn_->request_read_byte_;
            conn_->request_body_length_ -= conn_->request_read_byte_;
            conn_->request_read_byte_ = 0;
        }
    }
    size_t min = std::min(len, conn_->request_body_length_);
    while (min > 0) {
#ifdef HTTPS
        size_t tmp;
        int ret = SSL_read_ex(conn_->ssl_, dest + size, min, &tmp);
        if (ret == 0) {
            int err = SSL_get_error(conn_->ssl_, ret);
            if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }
            throw 2;
        }
#else
        ssize_t tmp = recv(conn_->sd_, dest + size, min, 0);
        if (tmp < 0) throw 2;
#endif
        if (tmp == 0) throw 1;
        size += tmp;
        min -= tmp;
        conn_->request_body_length_ -= tmp;
    }
    return size;
}
