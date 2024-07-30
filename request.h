#ifndef REQUEST_H_
#define REQUEST_H_

#include <string>
#include <map>

#ifndef NO_REDIS
#include "session.h"
#endif

#ifndef CASE_INSENSITIVE_COMPARE_STRUCT
#define CASE_INSENSITIVE_COMPARE_STRUCT
struct case_insensitive_compare {
    bool operator()(const std::string& a, const std::string& b) const {
        return std::lexicographical_compare(a.cbegin(), a.cend(), b.cbegin(), b.cend(), [](const char c1, const char c2){return std::tolower(c1) < std::tolower(c2);});
    }
};
#endif

class Request {
public:
#if defined (NO_REDIS)
    Request(int fd, int &read_byte, char *buf, size_t &body_len, int &port, std::string &method, std::string &url, std::string &ip, std::map<std::string, std::string, case_insensitive_compare> &headers, std::map<std::string, std::string> &params);
#else
    Request(uint64_t &sessionId, int fd, int &read_byte, char *buf, size_t &body_len, int &port, std::string &method, std::string &url, std::string &ip, std::map<std::string, std::string, case_insensitive_compare> &headers, std::map<std::string, std::string> &params);
    Session getSession() const;
#endif
    Request(const Request&) = delete;
    Request& operator=(const Request&) = delete;
    const int& getPort() const;
    const std::string& getMethod() const;
    const std::string& getUrl() const;
    const std::string& getIp() const;
    const std::map<std::string, std::string, case_insensitive_compare>& getHeaders() const;
    const std::map<std::string, std::string>& getParams() const;
    const std::string* getHeader(const std::string &key) const;
    const std::string* getParam(const std::string &key) const;
    size_t read_body(char *dest, size_t len);
private:
#ifndef NO_REDIS
    uint64_t &m_session_id;
#endif
    int m_fd;
    int &m_read_byte;
    char *m_buf;
    size_t &m_body_length;
    int &m_port;
    std::string &m_method;
    std::string &m_url;
    std::string &m_ip;
    std::map<std::string, std::string, case_insensitive_compare> &m_headers;
    std::map<std::string, std::string> &m_params;
};

#endif
