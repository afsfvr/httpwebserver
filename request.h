#ifndef REQUEST_H_
#define REQUEST_H_

#include <string>
#include <map>
#include <optional>

#ifdef USE_REDIS
#include "session.h"
#endif
#ifdef HTTPS
#include <openssl/ssl.h>
#endif

#ifndef CASE_INSENSITIVE_COMPARE_STRUCT
#define CASE_INSENSITIVE_COMPARE_STRUCT
struct case_insensitive_compare {
    bool operator()(const std::string &a, const std::string &b) const {
        return std::lexicographical_compare(a.cbegin(), a.cend(), b.cbegin(), b.cend(), [](const char c1, const char c2) {return std::tolower(c1) < std::tolower(c2);});
    }
};
#endif

class HttpConnect;
class Request {
    Request(const Request &) = delete;
    Request &operator=(const Request &) = delete;

public:
    Request(HttpConnect *conn);
#ifdef USE_REDIS
    Session getSession() const;
#endif
    int getPort() const;
    const std::string &getMethod() const;
    const std::string &getUrl() const;
    const std::string &getIp() const;
    const std::map<std::string, std::string, case_insensitive_compare> &getHeaders() const;
    const std::map<std::string, std::string> &getParams() const;
    std::optional<std::string> getHeader(const std::string &key) const;
    std::optional<std::string> getParam(const std::string &key) const;
    size_t read_body(char *dest, size_t len);
private:
    HttpConnect *conn_;
};

#endif
