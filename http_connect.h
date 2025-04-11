#ifndef HTTP_CONNECT_
#define HTTP_CONNECT_

#include <string>
#include <map>

#include "threadpool.h"
#include "request.h"
#include "response.h"

enum class STATE;

class HttpConnect: public Task {
    constexpr static int MAX_BUFSIZE = 2048;

public:
    HttpConnect(const int &epollfd, const int &pipe, const int &sd, const std::string &ip, const int &port);
    HttpConnect(const HttpConnect&) = delete;
    HttpConnect& operator=(const HttpConnect&) = delete;
    ~HttpConnect();
    operator int();
    bool operator==(const Task *task);
    void run();
private:
    unsigned char toHex(unsigned char x) const;
    std::string urlDecode(const std::string& str) const;
    void modfd(int ev);
    void init();
    int setblock(const int &fd);
    int setnonblock(const int &fd);
    void read_data();
    void parse();
    void parse_line(char *data);
    void parse_head(char *data);
    void parse_param(char *data);
    void write_data();
    bool write_head();
    void init_write_lib();
    void init_write_file(const std::string &filename);
    void setCookie();
    void setResponseState(int s, const char *err = nullptr);
    bool run_dynamic_lib();
    bool isFile(const std::string &filename) const;
#ifdef USE_REDIS
    uint64_t res_session_id;
#endif
    bool res_write;
    bool res_chunk;
    int res_state;
    size_t res_size;
    std::map<std::string, std::string, case_insensitive_compare> res_headers;
    std::set<Cookie> res_cookies;
    int epollfd;
    int m_pipe;
    int m_sd;
    char m_buf[MAX_BUFSIZE];
    int m_read_byte;
    char *m_file_data;
    size_t m_send_byte;
    size_t m_have_byte;
    size_t m_file_length;
    size_t m_body_len;
    std::string m_send_head;
    std::string m_dynamic_lib_file;
    std::map<std::string, std::string, case_insensitive_compare> headers;
    std::map<std::string, std::string> params;
    STATE m_state;
    std::string m_ip;
    int m_port;
    std::string m_method;
    std::string m_url;
    int response_state;
    bool m_keep_alive;
    Request request;
    Response response;
};

#endif
