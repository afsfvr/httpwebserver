#include <fstream>
#include <filesystem>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <cstring>

#include "../../base_class.h"
#include "../../log.h"

class Root: public BaseClass {
public:
    Root();
    std::string doGet(Request *request, Response *response, const std::string &cur_path) override;
    void doPost(Request *request, Response *response, const std::string &cur_path) override;
    void doDelete(Request *request, Response *response, const std::string &cur_path) override;
    void service(Request *request, Response *response, const std::string &cur_path, char *filename) override;
    ~Root() override;
private:
    bool validate(Request *request) const;
    std::string getfilename(const std::string& path, const std::string& data) const;
    bool fileExists(const std::string& filename) const;
    bool isFile(const std::string& filename) const;
    bool writeLen(int fd, const void *buf, size_t size) const;
};

extern "C" BaseClass* createClass() {
    return new Root();
}

extern "C" void deleteClass(BaseClass* base) {
    delete base;
}

Root::Root() {}

std::string Root::doGet(Request *request, Response *response, const std::string &cur_path) {
    const std::string& url = request->getUrl();
    if (url.find("..") != std::string::npos || url.find("/upload") != 0) {
        response->sendError(403, "非法请求");
        return {};
    }
    std::string path = url.substr(7);
    if (path.size() == 0 || path.front() != '/') path.insert(path.begin(), '/');
    if (path.find_last_not_of('/') == std::string::npos) {
        path = cur_path + "res";
        if (! fileExists(path)) {
            if (mkdir(path.c_str(), 0755) == -1) {
                LOG_WARN("创建文件夹失败: %s", strerror(errno));
                response->sendError(404, "<h1>文件不存在</h1>");
                return {};
            }
        }
    } else {
        path = cur_path + "res" + path;
        if (! fileExists(path)) {
            response->sendError(404, "<h1>文件不存在</h1>");
            return {};
        }
    }
    if (isFile(path)) {
        std::string value = "attachment;filename=";
        value += url.substr(url.find_last_of('/') + 1);
        response->addHeader("Content-Disposition", value);
        struct stat st;
        if (stat(path.c_str(), &st) == 0) {
            response->setContentLength(st.st_size);
        }
        return path;
    }
    response->write_data("<!DOCTYPE html><html lang='zh'><head><meta charset='UTF-8'><meta content='width=device-width,initial-scale=1.0,maximum-scale=1.0,user-scalable=0'name='viewport'><title>文件上传</title><script>function upload(){const uploadFile=document.getElementsByName('uploadFile')[0];if(uploadFile!==undefined&&uploadFile.value!==''){return true}else{alert('未选择文件!');return false}}function del(id){if(confirm('确认删除吗？')){const req=new XMLHttpRequest();const div=document.getElementById(id);if(div===undefined||div===null||div.getElementsByTagName('a').length!==1)alert('未找到该文件!');const a=div.getElementsByTagName('a')[0];req.open('DELETE',a.href);req.send();req.onreadystatechange=function(){if(req.readyState===4){alert(req.responseText);if(req.status===200&&req.responseText==='删除成功')div.remove()}}}}</script></head><body><form action=''method='post'enctype='multipart/form-data'>选择一个文件:<input type='file'name='uploadFile'multiple='multiple'/><input id='sub'type='submit'onclick='return upload()'value='上传'/></form><br/>");
    std::string urlPrefix = path.substr(path.find("res") + 3);
    if (urlPrefix.size() != 0) {
        response->write_data("<a href='/upload" + urlPrefix.substr(0, urlPrefix.find_last_of('/')) + "' style='color:blue'>返回上一级</a><br/><br/>");
    }
    DIR *dir = opendir(path.c_str());
    int id = 0;
    if (dir != nullptr) {
        urlPrefix.push_back('/');
        struct dirent *d;
        while ((d = readdir(dir)) != nullptr) {
            if (strcmp(d->d_name, ".") != 0 && strcmp(d->d_name, "..") != 0) {
                ++id;
                std::string file = path + "/" + d->d_name;
                if (isFile(file)) {
                    std::string s = "<div id=\"" + std::to_string(id)+ "\"><a download href=\"/upload" + (urlPrefix + d->d_name) + "\" style=\"color: black;font-weight: bold; margin-right: 10px\">" + d->d_name + "</a><input type=\"button\" onclick=\"del('" + std::to_string(id) + "')\" value=\"删除\"><br/><br/></div>";
                    response->write_data(s);
                } else {
                    std::string s = "<div id=\"" + std::to_string(id)+ "\"><a href=\"/upload" + (urlPrefix + d->d_name) + "\" style=\"color: blue; margin-right: 10px\">" + d->d_name + "</a><input type=\"button\" onclick=\"del('" + std::to_string(id) + "')\" value=\"删除\"><br/><br/></div>";
                    response->write_data(s);
                }
            }
        }
        closedir(dir);
    }
    response->write_data("</body></html>");
    return {};
}

bool Root::writeLen(int fd, const void *buf, size_t size) const {
    if (size <= 0) return true;
    while (size > 0) {
        int len = write(fd, buf, size);
        if (len > 0) {
            size -= len;
        } else if (len < 0) {
            return false;
        } else {
            break;
        }
    }
    return true;
}

void Root::doPost(Request *request, Response *response, const std::string &cur_path) {
    const std::string& url = request->getUrl();
    if (url.find("..") != std::string::npos || url.find("/upload") != 0) {
        response->sendError(403, "非法请求");
        return;
    }
    std::string path = url.substr(7);
    if (path.size() == 0 || path.front() != '/') path.insert(path.begin(), '/');
    path = cur_path + "/res" + path;

    const std::string *type = request->getHeader("content-type");
    if (type == nullptr || type->find("multipart/form-data") == std::string::npos || type->find("boundary=") == std::string::npos) {
        response->sendError(403, "非法请求");
    } else {
        std::string boundary = type->substr(type->find("boundary=") + 9);
        char buf[1024];
        size_t len;
        std::string data;
        while ((len = request->read_body(buf, 1024)) > 0) {
            data.append(buf, len);
        }
        int num = 0, success = 0;
        while (data.size() > 0) {
            size_t i1 = data.find(boundary);
            size_t i2 = data.find(boundary, boundary.size());
            if (i1 == std::string::npos || i2 == std::string::npos) break;
            const std::string filename = getfilename(path, data);
            size_t index = data.find("\r\n\r\n", i1 + boundary.size());
            if (index == std::string::npos) break;
            data.erase(0, index + 4);
            size_t size = i2 - index - 4 - 4;
            int fd = open(filename.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0644);
            if (fd > 0) {
                if (writeLen(fd, data.data(), size)) ++success;
                close(fd);
            }
            data.erase(0, size);
            ++num;
        }
    response->write_data("<script>alert(\"共计接收到" + std::to_string(num) + "个文件，上传成功" + std::to_string(success) + "个文件\");window.location.href=\"" + request->getUrl() + "\"</script>");
    }
}

void Root::doDelete(Request *request, Response *response, const std::string &cur_path) {
    const std::string& url = request->getUrl();
    if (url.find("..") != std::string::npos || url.find("/upload") != 0) {
        Root::doDelete(request, response, cur_path);
        return;
    }
    std::string path = url.substr(7);
    if (path.size() == 0 || path.front() != '/') path.insert(path.begin(), '/');
    path = cur_path + "/res" + path;
    if (! fileExists(path)) {
        response->sendError(404, "文件不存在");
        return;
    }
        if (remove(path.c_str()) == 0) {
            response->write_data("删除成功");
        } else {
            response->write_data(strerror(errno));
        }
        return;
    response->sendError(403, "非法请求");
}

void Root::service(Request *request, Response *response, const std::string &cur_path, char *filename) {
    if (validate(request)) {
        BaseClass::service(request, response, cur_path, filename);
    } else {
        response->addHeader("WWW-Authenticate", "Basic realm=\"upload\"");
        response->sendError(401, "<h1>401</h1>");
    }
}
bool Root::validate(Request *request) const {
    const std::string *authValue = request->getHeader("Authorization");
    if (authValue == nullptr) return false;
    return true;
}

std::string Root::getfilename(const std::string& path, const std::string& data) const {
    mkdir(path.c_str(), 0755);
    size_t index = data.find("filename");
    std::string filename;
    if (index == std::string::npos) {
        return std::to_string(time(nullptr));
    } else {
        size_t i1 = data.find('"', index + 8);
        size_t i2 = data.find('"', i1 + 1);
        if (i1 == std::string::npos || i2 == std::string::npos) {
            filename = std::to_string(time(nullptr));
        } else {
            filename = data.substr(i1 + 1, i2 - i1 - 1);
        }
        std::string tmp = path + "/" + filename;
        int count = 0;
        while (fileExists(tmp)) {
            tmp = path + "/" + filename + "(" + std::to_string(++count) + ")";
        }
        return tmp;
    }
}

bool Root::fileExists(const std::string &filename) const {
    struct stat st;
    if (stat(filename.c_str(), &st) < 0) {
        return false;
    }
    return true;
}

bool Root::isFile(const std::string &filename) const {
    struct stat st;
    if (stat(filename.c_str(), &st) == 0 && S_ISREG(st.st_mode)) {
        return true;
    }
    return false;
}

Root::~Root() {
}
