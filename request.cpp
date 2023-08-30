#include "request.h"

Request::Request(int &port, std::string &method, std::string &url, std::string &ip, std::map<std::string, std::string, case_insensitive_compare> &headers, std::map<std::string, std::string> &params): m_port(port), m_method(method), m_url(url), m_ip(ip), m_headers(headers), m_params(params) {}

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

const std::string* Request::getHeader(std::string &key) const {
    auto it = m_headers.find(key);
    if (it == m_headers.end()) {
        return nullptr;
    } else {
        return &it->second;
    }
}

const std::string* Request::getParam(std::string &key) const {
    auto it = m_params.find(key);
    if (it == m_params.end()) {
        return nullptr;
    } else {
        return &it->second;
    }
}
