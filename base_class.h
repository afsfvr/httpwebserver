#ifndef BASE_CLASS_H_
#define BASE_CLASS_H_

#include "request.h"
#include "response.h"

class BaseClass {
public:
    virtual ~BaseClass()=default;
    virtual void get(Request *request, Response *response, const std::string &cur_path)=0;
    virtual void post(Request *request, Response *response, const std::string &cur_path) {
        response->setStatus(405);
        response->write_data("<h1>405</h1>");
    }
};

extern "C" BaseClass* createClass();
extern "C" void deleteClass(BaseClass* base);

#endif
