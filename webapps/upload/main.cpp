#include <fstream>
#include <filesystem>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <cstring>
#include <random>

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
    std::string getfilename(const std::string& path, char *&buf, const std::string &boundary) const;
    bool fileExists(const std::string& filename) const;
    bool isFile(const std::string& filename) const;
    bool writeLen(int fd, const void *buf, size_t size) const;
    bool writeData(int &fd, char *buf, int &offset, const std::string &filename, const std::string &boundary) const;
    std::string htmlEscape(const std::string &input) const;
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
                std::string d_name = htmlEscape(d->d_name);
                if (isFile(path + "/" + d->d_name)) {
                    std::string s = "<div id=\"" + std::to_string(id)+ "\"><a download href=\"/upload" + (urlPrefix + d_name) + "\" style=\"color: black;font-weight: bold; margin-right: 10px\">" + d_name + "</a><input type=\"button\" onclick=\"del('" + std::to_string(id) + "')\" value=\"删除\"><br/><br/></div>";
                    response->write_data(s);
                } else {
                    std::string s = "<div id=\"" + std::to_string(id)+ "\"><a href=\"/upload" + (urlPrefix + d_name) + "\" style=\"color: blue; margin-right: 10px\">" + d_name + "</a><input type=\"button\" onclick=\"del('" + std::to_string(id) + "')\" value=\"删除\"><br/><br/></div>";
                    response->write_data(s);
                }
            }
        }
        closedir(dir);
    }
    response->write_data("</body></html>");
    return {};
}

void Root::doPost(Request *request, Response *response, const std::string &cur_path) {
    const std::string& url = request->getUrl();
    if (url.find("..") != std::string::npos || url.find("/upload") != 0) {
        response->sendError(403, "非法请求");
        return;
    }
    std::string path = url.substr(7);
    if (path.size() == 0 || path.front() != '/') path.insert(path.begin(), '/');
    path = cur_path + "res" + path;

    const std::string *type = request->getHeader("content-type");
    if (type == nullptr || type->find("multipart/form-data") == std::string::npos || type->find("boundary=") == std::string::npos) {
        response->sendError(403, "非法请求");
    } else {
        std::string boundary = type->substr(type->find("boundary=") + 9);
        std::string endboundary = "\r\n--" + boundary;
        char buf[4096];
        int len = 0;
        int offset = 0;
        int fd = -1;
        int num = 0, success = 0;
        std::string filename;
        while ((len = request->read_body(buf + offset, 4095 - offset)) > 0 || offset > 0) {
            int bufsize = -1;
            if (len > 0) {
                offset += len;
            } else {
                bufsize = offset;
            }
            buf[offset] = '\0';
            if (fd == -1) {
                filename.clear();
                char *data = buf;
                filename = getfilename(path, data, boundary);
                if (filename.length() > 0) {
                    ++ num;
                    fd = open(filename.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0644);
                    offset = offset - (data - buf);
                    memmove(buf, data, offset);
                    if (fd < 0) {
                        LOG_WARN("创建文件%s失败: %s", filename.c_str(), strerror(errno));
                    }
                } else if (offset >= 4000) {
                    offset = 0;
                }
            } else {
                if (writeData(fd, buf, offset, filename, endboundary)) {
                    ++ success;
                }
            }
            if (bufsize == offset) { // 没有收到数据且处理后大小无变化
                break;
            }
        }
        if (fd != -1) {
            close(fd);
            if (filename.size() != 0) {
                remove(filename.c_str());
            }
        }
    response->write_data("<script>alert(\"共计接收到" + std::to_string(num) + "个文件，上传成功" + std::to_string(success) + "个文件\");window.location.href=\"" + request->getUrl() + "\"</script>");
    }
}

void Root::doDelete(Request *request, Response *response, const std::string &cur_path) {
    const std::string& url = request->getUrl();
    if (url.find("..") != std::string::npos || url.find("/upload") != 0) {
        BaseClass::doDelete(request, response, cur_path);
        return;
    }
    std::string path = url.substr(7);
    if (path.size() == 0 || path.front() != '/') path.insert(path.begin(), '/');
    path = cur_path + "res" + path;
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

Root::~Root() {
}

bool Root::validate(Request *request) const {
    const std::string *authValue = request->getHeader("Authorization");
    if (authValue == nullptr) return false;
    return true;
}

std::string Root::getfilename(const std::string& path, char *&buf, const std::string &boundary) const {
    const char *data = strstr(buf, boundary.c_str());
    if (data == nullptr) {
        return {};
    }

    const char *name = strstr(data, "filename");
    if (name == nullptr) {
        return {};
    }

    name += 9; // filename=
    data = strstr(name, "\r\n\r\n");
    if (data == nullptr) {
        return {};
    }
    buf = buf + (data - buf + 4);

    if (! fileExists(path)) {
        mkdir(path.c_str(), 0755);
    }

    std::string filename;
    for (;; ++name) {
        if (*name == '\0') {
            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_int_distribution<> dist(0, 10000000);
            filename = std::to_string(time(nullptr)) + std::to_string(dist(gen));
            break;
        } else if (*name == '"') {
            ++ name;
            break;
        }
    }

    if (filename.length() == 0) {
        for (int i = 1; ; ++i) {
            const char *tmp = name + i;
            if (*tmp == '\0') {
                std::random_device rd;
                std::mt19937 gen(rd());
                std::uniform_int_distribution<> dist(0, 10000000);
                filename = std::to_string(time(nullptr)) + std::to_string(dist(gen));
                break;
            } else if (*tmp == '"') {
                filename = std::string(name, i);
                for (auto &c: filename) {
                    if (! std::isalnum(c) && c != '.' && c != '_') c = '_';
                }
                break;
            }
        }
    }

    std::string tmp = path + "/" + filename;
    int count = 0;
    while (fileExists(tmp)) {
        tmp = path + "/" + filename + "(" + std::to_string(++count) + ")";
    }
    return tmp;
}

bool Root::fileExists(const std::string &filename) const {
    struct stat st;
    return (stat(filename.c_str(), &st) == 0);
}

bool Root::isFile(const std::string &filename) const {
    struct stat st;
    return (stat(filename.c_str(), &st) == 0 && S_ISREG(st.st_mode));
}

bool Root::writeLen(int fd, const void *buf, size_t size) const {
    const char *p = static_cast<const char *>(buf);
    while (size > 0) {
        ssize_t len = write(fd, p, size);
        if (len > 0) {
            size -= len;
            p += len;
        } else if (len < 0) {
            if (errno == EINTR) continue;  // 被信号中断，重试
            return false;
        } else {
            return false; // write 返回 0，一般表示错误，返回 false
        }
    }
    return true;
}

bool Root::writeData(int &fd, char *buf, int &offset, const std::string &filename, const std::string &boundary) const {
    if (fd < 0 || offset < 0) {
        return false;
    }

    char *data = nullptr;
    for (size_t i = 0; i + boundary.size() <= offset; ++i) {
        if (memcmp(buf + i, boundary.c_str(), boundary.size()) == 0) {
            data = buf + i;
            break;
        }
    }
    if (data != nullptr) {
        int length = data - buf;
        bool ret = writeLen(fd, buf, length);
        offset -= length;
        memmove(buf, data, offset);
        close(fd);
        fd = -1;
        if (! ret && filename.size() > 0) {
            remove(filename.c_str());
        }
        return ret;
    }
    
    for (int i = std::max<int>(0, offset - boundary.size()); i < offset; ++i) {
        if (buf[i] == boundary[0]) {
            bool equals = true;
            for (int j = i + 1; j < offset; ++j) {
                if (buf[j] != boundary[j - i]) {
                    equals = false;
                    break;
                }
            }
            if (equals) {
                -- i;
                if (i >= 0) {
                    if (! writeLen(fd, buf, i)) {
                        close(fd);
                        fd = -1;
                        if (filename.size() > 0) {
                            remove(filename.c_str());
                        }
                    }
                    offset -= i;
                    memmove(buf, buf + i, offset);
                }
                return false;
            }
        }
    }
    if (! writeLen(fd, buf, offset)) {
        close(fd);
        fd = -1;
        if (filename.size() > 0) {
            remove(filename.c_str());
        }
    }
    offset = 0;

    return false;
}

std::string Root::htmlEscape(const std::string &input) const {
    std::string escaped;
    for (char c : input) {
        switch (c) {
            case '&':  escaped += "&amp;";  break;
            case '<':  escaped += "&lt;";   break;
            case '>':  escaped += "&gt;";   break;
            case '"':  escaped += "&quot;"; break;
            case '\'': escaped += "&#39;";  break;
            default:   escaped += c;        break;
        }
    }
    return escaped;
}

