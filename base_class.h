#ifndef SERVLET_H_
#define SERVLET_H_

#include "request.h"
#include "response.h"

class BaseClass {
public:
    virtual ~BaseClass()=default;
    virtual void get(Request *request, Response *response, const std::string &cur_path)=0;
    virtual void post(Request *request, Response *response, const std::string &cur_path)=0;
};

extern "C" BaseClass* createClass();
extern "C" void deleteClass(BaseClass* base);

#endif
