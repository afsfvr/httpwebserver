#ifndef HTTP_CONNECT_
#define HTTP_CONNECT_

#include <string>
#include <map>

#include "request.h"
#include "response.h"

const int MAX_BUFSIZE = 4096;

enum class STATE;

class HttpConnect {
public:
    HttpConnect(const int &epollfd, const int &pipe, const int &sd, const std::string &ip, const int &port);
    ~HttpConnect();
    void run();
private:
    void init();
    int setblock(const int &fd);
    int setnonblock(const int &fd);
    void read_data();
    bool write_data();
    void parse();
    void parse_line(char *data);
    void parse_head(char *data);
    void parse_param(char *data);
    bool write_head();
    void modfd(int ev);
    void setResponseState(int s, const char *err);
    void setResponseState(std::string &filename, struct stat &st);
    bool exec_so();
    bool res_write;
    bool res_chunk;
    int res_state;
    int res_size;
    std::map<std::string, std::string, case_insensitive_compare> res_headers;
    int epollfd;
    int m_pipe;
    int m_sd;
    char m_buf[MAX_BUFSIZE];
    int m_read_byte;
    char *m_file_data;
    int m_send_byte;
    int m_have_byte;
    int m_file_length;
    std::string m_send_head;
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
