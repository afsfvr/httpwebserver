#include <fstream>
#include <filesystem>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <cstring>

#include "../../base_class.h"

class Root: public BaseClass {
public:
    Root();
    std::string doGet(Request *request, Response *response, const std::string &cur_path) override;
};

extern "C" BaseClass* createClass() {
    return new Root();
}

extern "C" void deleteClass(BaseClass* base) {
    delete base;
}

Root::Root() {}

std::string Root::doGet(Request *request, Response *response, const std::string &cur_path) {
    std::string url = request->getUrl().substr(5); // /root
    if (url == "/getip") {
        response->write_data(request->getIp());
        response->write_data("\r\n", 2);
        return "";
    }
    if (url == "/getipport") {
        response->write_data(request->getIp());
        response->write_data(":", 1);
        response->write_data(std::to_string(request->getPort()));
        response->write_data("\r\n", 2);
        return "";
    }
    return ".";
    if (url.length() == 0 || url.find_first_not_of('/') == std::string::npos) {
        return "resources/index.html";
    }
    if (url[0] == '/') {
        return "resources" + url;
    }
    return "resources/" + url;
}
