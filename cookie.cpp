#include <ctime>
#include <stdexcept>

#include "cookie.h"

Cookie::Cookie(const std::string& name, const std::string& value): m_expires(0), m_http_only(false), m_max_age(-1), m_partitioned(false), m_secure(false) {
    setName(name);
    setValue(value);
}

bool Cookie::operator<(const Cookie& cookie) const {
    if (this->m_domain == cookie.m_domain) {
        return this->m_name < cookie.m_name;
    }
    return m_domain < cookie.m_domain;
}
bool Cookie::operator==(const Cookie& cookie) const {
    return this->m_name == cookie.m_name && this->m_domain == cookie.m_domain;
}

std::string Cookie::to_string() const {
    std::string s;
    s.append(m_name).append("=").append(m_value);
    if (! m_domain.empty()) {
        s.append("; Domain=").append(m_domain);
    }
    if (m_expires > 0) {
        char buf[128] = {'\0'};
        std::strftime(buf, sizeof(buf), "%a, %d %b %Y %H:%M:%S GMT", std::gmtime(&m_expires));
        s.append("; Expires=").append(buf);
    }
    if (m_max_age != -1) {
        s.append("; Max-Age=").append(std::to_string(m_max_age));
    }
    if (! m_path.empty()) {
        s.append("; Path=").append(m_path);
    }
    if (! m_same_site.empty()) {
        s.append("; SameSite=").append(m_same_site);
    }
    if (m_http_only) {
        s.append("; HttpOnly");
    }
    if (m_partitioned) {
        s.append("; Partitioned");
    }
    if (m_secure) {
        s.append("; Secure");
    }

    return s;
}

Cookie& Cookie::setName(const std::string& name) {
    for (const char &c: name) {
        if (c >= 127 || c <= 31 || c == 34 || c == 40 || c == 41 || c == 44 || c == 47 || (c >=58 && c <=64) || (c >= 91 && c <= 93) || c == 123 || c == 125) {
            throw std::invalid_argument(std::string("非法字符:\\").append(std::to_string(c)));
        }
    }
    if (name.empty()) {
        throw std::invalid_argument(std::string("不能为空"));
    }
    if (name.compare(0, 9, "__Secure-") == 0) {
        m_secure = true;
    } else if (name.compare(0, 7, "__Host-") == 0) {
        m_secure = true;
        m_domain = "";
        m_path = "/";
    }
    this->m_name = name;
    return *this;
}

Cookie& Cookie::setValue(const std::string& value) {
    for (const char &c: value) {
        if (c <= 31 || c >= 127 || c == ' ') {
            throw std::invalid_argument(std::string("非法字符:\\").append(std::to_string((int)c)));
        }
    }
    if (value.empty()) {
        throw std::invalid_argument(std::string("不能为空"));
    }
    this->m_value = value;
    return *this;
}

Cookie& Cookie::setDomain(const std::string& domain) {
    if (! domain.empty() && m_name.compare(0, 7, "__Host-")) {
        throw std::invalid_argument(std::string("name以__Host-开头时禁止设置domain"));
    }
    this->m_domain = domain;
    return *this;
}

Cookie& Cookie::setExpires(time_t expires) {
    this->m_expires = expires;
    return *this;
}

Cookie& Cookie::setHttpOnly(bool httpOnly) {
    this->m_http_only = httpOnly;
    return *this;
}

Cookie& Cookie::setMaxAge(int maxAge) {
    if (maxAge > 0) {
        this->m_max_age = maxAge;
    } else {
        this->m_max_age = 0;
    }
    return *this;
}

Cookie& Cookie::setPartinioned(bool partitioned) {
    this->m_partitioned = partitioned;
    return *this;
}

Cookie& Cookie::setPath(const std::string& path) {
    if (path != "/" && m_name.compare(0, 7, "__Host-")) {
        throw std::invalid_argument(std::string("name以__Host-开头时禁止设置path的值必须为/"));
    }
    this->m_path = path;
    return *this;
}

Cookie& Cookie::setSameSite(SameSite site) {
    switch (site) {
        case SameSite::Strict:
            m_same_site = "Strict";
            break;
        case SameSite::Lax:
            m_same_site = "Lax";
            break;
        case SameSite::None:
            m_same_site = "None";
            break;
        case SameSite::Null:
        default:
            m_same_site = "";
            break;
    }
    return *this;
}

Cookie& Cookie::setSecure(bool secure) {
    if (! secure && (m_name.compare(0, 9, "__Secure-") == 0 || m_name.compare(0, 7, "__Host-") == 0)) {
        throw std::invalid_argument(std::string("name以__Secure-或__Host-开头时Secure必须设置为true"));
    }
    this->m_secure = secure;
    return *this;
}

Cookie::SameSite Cookie::sameSite() const {
    if (m_same_site == "Strict") {
        return SameSite::Strict;
    } else if (m_same_site == "Lax") {
        return SameSite::Lax;
    } else if (m_same_site == "None") {
        return SameSite::None;
    } else {
        return SameSite::Null;
    }
}
