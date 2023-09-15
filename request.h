#ifndef REQUEST_H_
#define REQUEST_H_

#include <string>
#include <map>

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
    Request(int &port, std::string &method, std::string &url, std::string &ip, std::map<std::string, std::string, case_insensitive_compare> &headers, std::map<std::string, std::string> &params);
    Request(const Request&) = delete;
    Request& operator=(const Request&) = delete;
    const int& getPort() const;
    const std::string& getMethod() const;
    const std::string& getUrl() const;
    const std::string& getIp() const;
    const std::map<std::string, std::string, case_insensitive_compare>& getHeaders() const;
    const std::map<std::string, std::string>& getParams() const;
    const std::string* getHeader(std::string &key) const;
    const std::string* getParam(std::string &key) const;
private:
    int &m_port;
    std::string &m_method;
    std::string &m_url;
    std::string &m_ip;
    std::map<std::string, std::string, case_insensitive_compare> &m_headers;
    std::map<std::string, std::string> &m_params;
};

#endif
