#ifndef RESPONSE_H_
#define RESPONSE_H_

#include <map>
#include <string>

#ifndef CASE_INSENSITIVE_COMPARE_STRUCT
#define CASE_INSENSITIVE_COMPARE_STRUCT
struct case_insensitive_compare {
    bool operator()(const std::string& a, const std::string& b) const {
        return std::lexicographical_compare(a.cbegin(), a.cend(), b.cbegin(), b.cend(), [](const char c1, const char c2){return std::tolower(c1) < std::tolower(c2);});
    }
};
#endif

class Response{
public:
    Response(bool &w, bool &chunk, int &status, int &size, int sd, std::map<std::string, std::string, case_insensitive_compare> &headers);
    Response(const Response&) = delete;
    Response& operator=(const Response&) = delete;
    void addHeader(const std::string &key, const std::string &value);
    std::string* getHeader(const std::string &key);
    std::map<std::string, std::string, case_insensitive_compare>& getHeaders();
    void write_data(const void *buf, const size_t size);
    void write_data(const std::string &str);
    void write_data(const void *buf, const size_t size, int flags);
    void write_file(const std::string &filename);
    void write_len(const void *buf,size_t size, int flags);
    void flush();
    void setStatus(int status);
private:
    constexpr static int MAX_BUFSIZE = 2048;
    std::string decimalToHex(int num);
    bool &m_write;
    bool &m_chunk;
    int &m_status;
    char m_buf[MAX_BUFSIZE];
    int &m_size;
    int m_sd;
    std::map<std::string, std::string, case_insensitive_compare> &m_headers;
};

#endif
