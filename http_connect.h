#ifndef HTTP_CONNECT_
#define HTTP_CONNECT_

#include <string>
#include <map>

enum class STATE;

class HttpConnect {
public:
    HttpConnect(const int &epollfd, const int &pipe, const int &sd, const std::string &ip, const int &port);
    HttpConnect(const HttpConnect&) = delete;
    HttpConnect& operator=(const HttpConnect&) = delete;
    ~HttpConnect();
    void run();
private:
    void init();
    int setnonblock(const int &fd);
    void read_data();
    void write_data();
    void parse();
    void parse_line(char *data);
    void parse_head(char *data);
    void parse_param(char *data);
    bool write_head();
    void modfd(int ev);
    void setResponseState(int s, const char *err);
    void setResponseState();
    const static int MAX_BUFSIZE = 4096;
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
    std::map<std::string, std::string> headers;
    std::map<std::string, std::string> params;
    STATE m_state;
    std::string m_ip;
    int m_port;
    std::string m_method;
    std::string m_url;
    int response_state;
    bool m_keep_alive;
};

#endif
