#ifndef BASE_CLASS_H_
#define BASE_CLASS_H_

#include <fcntl.h>
#include <unistd.h>
#include <cstring>

#include "request.h"
#include "response.h"

class BaseClass {
public:
    virtual ~BaseClass()=default;
    virtual std::string doGet(Request *request, Response *response, const std::string &cur_path);
    virtual void doHead(Request *request, Response *response, const std::string &cur_path);
    virtual void doPost(Request *request, Response *response, const std::string &cur_path);
    virtual void doPut(Request *request, Response *response, const std::string &cur_path);
    virtual void doDelete(Request *request, Response *response, const std::string &cur_path);
    virtual void doOptions(Request *request, Response *response, const std::string &cur_path);
    virtual void doTrace(Request *request, Response *response, const std::string &cur_path);
    virtual void service(Request *request, Response *response, const std::string &cur_path, char *filename);
};

std::string BaseClass::doGet(Request *request, Response *response, const std::string &cur_path) {
    response->sendError(405, "<h1>405</h1>");
    return {};
}

void BaseClass::doHead(Request *request, Response *response, const std::string &cur_path) {
    bool res_write = false, res_chunk = false;
    int res_state = 200;
    size_t res_size = 0;
    bool keep_alive = true;
    std::map<std::string, std::string, case_insensitive_compare> res_headers(response->getHeaders());
    std::set<Cookie> res_cookies;
    int fd = open("/dev/null", O_RDWR);
    Response resp(res_write, res_chunk, res_state, res_size, fd, keep_alive, res_headers, res_cookies);
    this->doGet(request, &resp, cur_path);
    close(fd);
    response->setStatus(res_state);
    response->getHeaders() = res_headers;
}

void BaseClass::doPost(Request *request, Response *response, const std::string &cur_path) {
    response->sendError(405, "<h1>405</h1>");
}

void BaseClass::doPut(Request *request, Response *response, const std::string &cur_path) {
    response->sendError(405, "<h1>405</h1>");
}

void BaseClass::doDelete(Request *request, Response *response, const std::string &cur_path) {
    response->sendError(405, "<h1>405</h1>");
}

void BaseClass::doOptions(Request *request, Response *response, const std::string &cur_path) {
    response->sendError(405, "<h1>405</h1>");
}

void BaseClass::doTrace(Request *request, Response *response, const std::string &cur_path) {
    response->sendError(405, "<h1>405</h1>");
}

void BaseClass::service(Request *request, Response *response, const std::string &cur_path, char *filename) {
    const std::string& method = request->getMethod();
    if (method == "GET") {
        std::string name = this->doGet(request, response, cur_path);
        if (! name.empty()) {
            if (name[0] == '/' || name[0] == '\\') {
                strncpy(filename, name.c_str(), 256);
            } else {
                strncat(filename, cur_path.c_str(), 256);
                strncpy(filename, name.c_str(), 256 - cur_path.size());
            }
        }
    } else if (method == "HEAD") {
        this->doHead(request, response, cur_path);
    } else if (method == "POST") {
        this->doPost(request, response, cur_path);
    } else if (method == "PUT") {
        this->doPut(request, response, cur_path);
    } else if (method == "DELETE") {
        this->doDelete(request, response, cur_path);
    } else if (method == "OPTIONS") {
        this->doOptions(request, response, cur_path);
    } else if (method == "TRACE") {
        this->doTrace(request, response, cur_path);
    } else {
        response->sendError(501, "<h1>501</h1>");
    }
}

extern "C" BaseClass* createClass();
extern "C" void deleteClass(BaseClass* base);

#endif
