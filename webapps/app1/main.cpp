#include <iostream>
#include <fstream>
#include <filesystem>
#include <unistd.h>

#include "../../base_class.h"


class Root: public BaseClass {
public:
    Root();
    std::string doGet(Request *request, Response *response, const std::string &cur_path) override;
    void doPost(Request *request, Response *response, const std::string &cur_path) override;
    ~Root() override;
private:
};

Root::Root() {
}

std::string Root::doGet(Request *request, Response *response, const std::string &cur_path) {
    Session session = request->getSession();
    std::string s;
    if (session) {
        auto m = session.getAttributes();
        s = "当前有" + std::to_string(m.size()) + "个属性";
        for (auto it = m.cbegin(); it != m.cend(); it++) {
            s.append("\n").append(it->first).append(": ").append(it->second);
        }
    } else {
        s = "cookie错误";
    }
    response->write_data(s);
    return {};
}

void Root::doPost(Request *request, Response *response, const std::string &cur_path) {
    auto m = request->getParams();
    Session session = request->getSession();
    int add = 0, set = 0;
    for (auto it = m.cbegin(); it != m.cend(); it++) {
        if (session.addAttribute(it->first, it->second)) {
            ++add;
        } else {
            set += session.setAttribute(it->first, it->second);
        }
    }
    std::string s = "添加了" + std::to_string(add) + "个属性";
    if (set != 0) {
        s.append("，修改了").append(std::to_string(set)).append("个属性");
    }
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
