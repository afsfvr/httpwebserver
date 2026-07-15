#ifndef RESPONSE_H_
#define RESPONSE_H_

#include <set>
#ifdef HTTPS
#include <openssl/ssl.h>
#endif

#include "cookie.h"

#ifndef CASE_INSENSITIVE_COMPARE_STRUCT
#define CASE_INSENSITIVE_COMPARE_STRUCT
struct case_insensitive_compare {
    bool operator()(const std::string &a, const std::string &b) const {
        return std::lexicographical_compare(a.cbegin(), a.cend(), b.cbegin(), b.cend(), [](const char c1, const char c2) {return std::tolower(c1) < std::tolower(c2);});
    }
};
#endif

class HttpConnect;
class Response {
    Response(const Response &) = delete;
    Response &operator=(const Response &) = delete;
    friend class HttpConnect;

public:
    Response(HttpConnect *conn);
    void setContentLength(size_t len);
    void sendError(int num, const std::string &errmsg = "");
    void sendRedirect(const std::string &url);
    void addCookie(const Cookie &cookie);
    const Cookie *getCookie(const std::string &name, const std::string &domain = "") const;
    std::set<Cookie> *getCookies();
    bool addHeader(const std::string &key, const std::string &value);
    void setHeader(const std::string &key, const std::string &value);
    std::string *getHeader(const std::string &key) const;
    std::map<std::string, std::string, case_insensitive_compare> *getHeaders();
    void writeData(const void *buf, const size_t size);
    void writeData(const std::string &str);
    void writeFile(const std::string &filename);
    void flush();
    void setStatus(int status);
    int getStatus() const;
private:
    void writeLen(const void *buf, size_t size, int flags = 0) const;
    std::string decimalToHex(int num) const;
    std::string time_tToHttpDate(time_t timestamp) const;

    HttpConnect *conn_;
};

#endif
