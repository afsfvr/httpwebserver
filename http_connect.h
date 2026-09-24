#ifndef HTTP_CONNECT_
#define HTTP_CONNECT_

#ifdef HTTPS
#include <openssl/ssl.h>
#include <openssl/err.h>
#endif

#include "threadpool.h"
#include "request.h"
#include "response.h"

enum class STATE;

class HttpConnect: public Task {
    constexpr static int MAX_BUFSIZE = 2048;
    friend class Request;
    friend class Response;

public:
    HttpConnect(const int &epollfd, const int &pipe,
#ifdef HTTPS
            SSL *ssl,
#endif
            const int &sd, const std::string &ip, const int &port);
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
    void readData();
    void parse();
    void parseLine(char *data);
    void parseHead(char *data);
    void parseParam(char *data);
    void writeData();
    bool writeHead();
    void initWriteLib();
    void initWriteFile(const std::string &filename);
    void setCookie();
    void setResponseState(int s, const char *err = nullptr);
    bool runDynamicLib();
    bool isFile(const std::string &filename) const;
    std::string trim(const std::string &str) const;
#ifdef HTTPS
    void handshake();
#endif

#ifdef USE_REDIS
    uint64_t session_id_;
#endif
    std::string lib_file_;
    int epollfd_;
    int pipe_;
    int sd_;
    STATE state_;
    std::string ip_;
    int port_;
    std::vector<std::string> forward_;
    std::string method_;
    std::string url_;
    bool keep_alive_;
#ifdef HTTPS
    SSL *ssl_;
    bool handshake_;
#endif

    // request
    Request request_;
    std::map<std::string, std::string, case_insensitive_compare> request_headers_;
    std::map<std::string, std::string> request_params_;
    char request_buf_[MAX_BUFSIZE];
    int request_read_byte_;
    size_t request_body_length_;
    
    // response
    Response response_;
    bool response_write_;
    bool response_chunk_;
    int response_state_;
    char response_buf_[MAX_BUFSIZE];
    size_t response_size_;
    std::map<std::string, std::string, case_insensitive_compare> response_headers_;
    std::set<Cookie> response_cookies_;
    char *response_file_ptr_;
    size_t response_file_length_;
    size_t response_send_byte_;
    size_t response_have_byte_;
    std::string response_send_head_;
};

#endif
