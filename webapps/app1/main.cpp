#include <iostream>
#include <fstream>
#include <filesystem>
#include <unistd.h>

#include "base_class.h"

class Root: public BaseClass {
public:
    Root();
    void get(Request *request, Response *response, const std::string &cur_path) override;
    void post(Request *request, Response *response, const std::string &cur_path) override;
    ~Root() override;
private:
};

Root::Root() {
}

void Root::get(Request *request, Response *response, const std::string &cur_path) {
    std::string s = "调用动态链接库的get方法";
    response->write_data(s.c_str(), s.size());
}

void Root::post(Request *request, Response *response, const std::string &cur_path) {
    std::string s = "调用动态链接库的post方法";
    response->write_data(s.c_str(), s.size());
}

Root::~Root() {
}

extern "C" BaseClass* createClass() {
    return new Root();
}

extern "C" void deleteClass(BaseClass* base) {
    delete base;
}
