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
    Session session = request->getSession();
    auto m = session.getAttributes();
    std::string s = "当前有" + std::to_string(m.size()) + "个属性";
    for (auto it = m.cbegin(); it != m.cend(); it++) {
        s.append("\n").append(it->first).append(": ").append(it->second);
    }
    response->write_data(s);
}

void Root::post(Request *request, Response *response, const std::string &cur_path) {
    auto m = request->getParams();
    Session session = request->getSession();
    for (auto it = m.cbegin(); it != m.cend(); it++) {
        session.setAttribute(it->first, it->second);
    }
    std::string s = "添加了" + std::to_string(m.size()) + "个属性";
    response->write_data(s);
}

Root::~Root() {
}

extern "C" BaseClass* createClass() {
    return new Root();
}

extern "C" void deleteClass(BaseClass* base) {
    delete base;
}
