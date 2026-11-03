#include "dynlib_cache.h"

#include <cerrno>
#include <cstring>

#include <dirent.h>
#include <sys/inotify.h>

// webapps根目录：监听应用目录的创建与删除
static constexpr uint32_t kRootMask = IN_ONLYDIR | IN_CREATE | IN_MOVED_TO | IN_MOVED_FROM | IN_DELETE;
// 应用目录：监听其中 libmain.so 的变更
static constexpr uint32_t kLibMask = IN_ONLYDIR | IN_CLOSE_WRITE | IN_MODIFY | IN_MOVED_TO | IN_MOVED_FROM | IN_DELETE;
static constexpr const char *kLibName = "libmain.so";

DynamicLibCache::LibEntry::~LibEntry() {
    if (handle != nullptr) {
        dlclose(handle);
        handle = nullptr;
    }
}

DynamicLibCache& DynamicLibCache::instance() {
    static DynamicLibCache cache;
    return cache;
}

void DynamicLibCache::start(const std::string &webappsPath) {
    std::lock_guard lock(mutex_);
    if (watcher_.joinable()) {
        SPDLOG_WARN("动态库缓存已启动");
        return;
    }
    webapps_root_ = webappsPath.empty() ? "./" : webappsPath;
    if (webapps_root_.back() != '/') webapps_root_.push_back('/');
    struct stat st;
    if (::stat(webapps_root_.c_str(), &st) == -1 || !S_ISDIR(st.st_mode)) {
        SPDLOG_WARN("webapps目录{}不存在，动态库将不会热更新", webapps_root_);
        return;
    }
    int ifd = inotify_init1(IN_CLOEXEC);
    if (ifd == -1) {
        SPDLOG_WARN("inotify初始化失败: {}, 动态库将不会热更新", strerror(errno));
        return;
    }
    if (pipe(stop_pipe_) == -1) {
        SPDLOG_WARN("创建动态库监听唤醒管道失败: {}, 动态库将不会热更新", strerror(errno));
        close(ifd);
        stop_pipe_[0] = stop_pipe_[1] = -1;
        return;
    }
    watchDirectoryLocked(ifd, webapps_root_, kRootMask);
    if (watches_.empty()) {
        SPDLOG_WARN("监听webapps目录{}失败: {}, 动态库将不会热更新", webapps_root_, strerror(errno));
        close(ifd);
        close(stop_pipe_[0]);
        close(stop_pipe_[1]);
        stop_pipe_[0] = stop_pipe_[1] = -1;
        return;
    }
    // 监听已存在的应用目录
    DIR *dp = opendir(webapps_root_.c_str());
    if (dp != nullptr) {
        while (struct dirent *dir = readdir(dp)) {
            if (dir->d_name[0] == '.') continue;
            std::string path = webapps_root_ + dir->d_name;
            struct stat dst;
            if (::stat(path.c_str(), &dst) == -1 || !S_ISDIR(dst.st_mode)) continue;
            watchDirectoryLocked(ifd, path + "/", kLibMask);
        }
        closedir(dp);
    }
    try {
        watcher_ = std::thread([this, ifd] { watchLoop(ifd); });
    } catch (const std::system_error &e) {
        SPDLOG_WARN("启动动态库监听线程失败: {}, 动态库将不会热更新", e.what());
        watches_.clear();
        close(ifd);
        close(stop_pipe_[0]);
        close(stop_pipe_[1]);
        stop_pipe_[0] = stop_pipe_[1] = -1;
        return;
    }
    SPDLOG_DEBUG("动态库缓存已启用，监听目录: {}", webapps_root_);
}

void DynamicLibCache::stop() {
    {
        std::lock_guard lock(mutex_);
        if (! watcher_.joinable()) return;
    }
    if (stop_pipe_[1] != -1) {
        uint8_t byte = 1;
        ssize_t ret = write(stop_pipe_[1], &byte, sizeof(byte));
        (void) ret;
    }
    watcher_.join();
    std::lock_guard lock(mutex_);
    if (stop_pipe_[0] != -1) {
        close(stop_pipe_[0]);
        stop_pipe_[0] = -1;
    }
    if (stop_pipe_[1] != -1) {
        close(stop_pipe_[1]);
        stop_pipe_[1] = -1;
    }
    cache_.clear();
    watches_.clear();
    if (! copy_dir_.empty()) {
        try{
            std::filesystem::remove_all(copy_dir_);
        } catch(...) { }
    }
}

std::shared_ptr<DynamicLibCache::LibEntry> DynamicLibCache::acquire(const std::string &path) {
    std::lock_guard lock(mutex_);
    auto iter = cache_.find(path);
    if (iter != cache_.end()) return iter->second;
    auto entry = loadLib(path);
    if (entry != nullptr) {
        cache_.emplace(path, entry);
        SPDLOG_INFO("动态链接库{}装载成功", path);
    }
    return entry;
}

bool DynamicLibCache::ensureCopyDir() {
    const std::string prefix = PROJECT_NAME "-libcache-";
    const std::filesystem::path tmp_dir("/tmp");

    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(tmp_dir, ec)) {
        if (ec) break;

        const std::string name = entry.path().filename().string();
        if (name.compare(0, prefix.size(), prefix) != 0) continue;

        const std::string pid_str = name.substr(prefix.size());
        if (pid_str.empty()) continue;

        char* end = nullptr;
        errno = 0;
        long pid = std::strtol(pid_str.c_str(), &end, 10);
        if (errno != 0 || *end != '\0' || pid <= 0) continue;

        if (kill(static_cast<pid_t>(pid), 0) == -1 && errno == ESRCH) {
            if (entry.is_directory(ec) && !ec) {
                std::filesystem::remove_all(entry.path(), ec);
                if (ec) {
                    SPDLOG_WARN("清理残留动态库目录失败: {}, {}", entry.path().string(), ec.message());
                    ec.clear();
                }
            }
        }
    }

    copy_dir_ = "/tmp/" PROJECT_NAME "-libcache-" + std::to_string(getpid());
    if (mkdir(copy_dir_.c_str(), 0700) == -1 && errno != EEXIST) {
        SPDLOG_WARN("创建动态库副本目录失败: {}, 将直接装载源文件", strerror(errno));
        copy_dir_.clear();
        return false;
    }
    return true;
}

bool DynamicLibCache::copyLibFile(const std::string &src, const std::string &dst) {
    struct stat st;
    if (::stat(src.c_str(), &st) == -1 || !S_ISREG(st.st_mode)) return false;
    const std::string tmp = dst + ".tmp";
    int fd_in = ::open(src.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd_in == -1) return false;
    int fd_out = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, st.st_mode & 0777);
    if (fd_out == -1) {
        ::close(fd_in);
        return false;
    }
    bool ok = true;
    char buf[64 * 1024];
    while (ok) {
        ssize_t n = ::read(fd_in, buf, sizeof(buf));
        if (n == 0) break;
        if (n < 0) {
            if (errno == EINTR) continue;
            ok = false;
            break;
        }
        ssize_t offset = 0;
        while (ok && offset < n) {
            ssize_t m = ::write(fd_out, buf + offset, static_cast<size_t>(n - offset));
            if (m < 0) {
                if (errno == EINTR) continue;
                ok = false;
                break;
            }
            offset += m;
        }
    }
    if (ok) ok = ::fchmod(fd_out, static_cast<mode_t>(st.st_mode & 0777)) == 0;
    if (ok) ok = ::fsync(fd_out) == 0;
    ::close(fd_out);
    ::close(fd_in);
    if (ok) ok = ::rename(tmp.c_str(), dst.c_str()) == 0;
    if (! ok) {
        ::unlink(tmp.c_str());
        return false;
    }
    return true;
}

std::shared_ptr<DynamicLibCache::LibEntry> DynamicLibCache::loadLib(const std::string &path) {
    // 源文件的 mtime 用于事件队列溢出时的兜底校验
    struct stat st;
    if (::stat(path.c_str(), &st) == -1 || !S_ISREG(st.st_mode)) {
        SPDLOG_WARN("动态库源文件不存在: {}", path);
        return nullptr;
    }
    std::string dl_path = path;
    // 先将源文件复制为私有副本，再从副本 dlopen：
    // ld/cp/rsync --inplace 会原地直写源文件，直接装载会破坏已映射的库
    if (copy_dir_.empty()) ensureCopyDir();
    if (! copy_dir_.empty()) {
        std::ostringstream oss;
        oss << std::hex << std::setw(16) << std::setfill('0') << std::hash<std::string>{}(path);
        const std::string copy = copy_dir_ + "/" + oss.str() + "-" + kLibName;
        if (copyLibFile(path, copy)) {
            dl_path = copy;
        } else {
            SPDLOG_WARN("复制动态库{}失败, 将直接装载源文件", path);
        }
    }
    void *handle = dlopen(dl_path.c_str(), RTLD_NOW);
    if (handle == nullptr) {
        SPDLOG_WARN("装载动态库出错: {}", dlerror());
        return nullptr;
    }
    void *createFn = dlsym(handle, "createClass");
    void *deleteFn = dlsym(handle, "deleteClass");
    if (createFn == nullptr || deleteFn == nullptr) {
        SPDLOG_WARN("加载动态库导出函数出错: {}", dlerror());
        dlclose(handle);
        return nullptr;
    }
    auto entry = std::make_shared<LibEntry>();
    entry->handle = handle;
    entry->create = reinterpret_cast<BaseClass *(*)()>(createFn);
    entry->destroy = reinterpret_cast<void (*)(BaseClass*)>(deleteFn);
    entry->mtime = st.st_mtim;
    return entry;
}

void DynamicLibCache::watchDirectoryLocked(int ifd, const std::string &dir, uint32_t mask) {
    int wd = inotify_add_watch(ifd, dir.c_str(), mask);
    if (wd == -1) {
        SPDLOG_DEBUG("监听目录{}失败: {}", dir, strerror(errno));
        return;
    }
    watches_[wd] = dir;
}

void DynamicLibCache::invalidateLocked(const std::string &path) {
    auto iter = cache_.find(path);
    if (iter == cache_.end()) return;
    SPDLOG_INFO("动态库{}已变更，等待运行中的请求结束后重新加载", path);
    cache_.erase(iter);
}

void DynamicLibCache::rescan(int ifd) {
    DIR *dp = opendir(webapps_root_.c_str());
    if (dp != nullptr) {
        while (struct dirent *dir = readdir(dp)) {
            if (dir->d_name[0] == '.') continue;
            std::string path = webapps_root_ + dir->d_name;
            struct stat dst;
            if (::stat(path.c_str(), &dst) == -1 || !S_ISDIR(dst.st_mode)) continue;
            watchDirectoryLocked(ifd, path + "/", kLibMask);
        }
        closedir(dp);
    }
    // 校验已缓存动态库文件的 mtime，变更即失效
    auto iter = cache_.begin();
    while (iter != cache_.end()) {
        struct stat st;
        if (::stat(iter->first.c_str(), &st) != 0 || st.st_mtim.tv_sec != iter->second->mtime.tv_sec ||
                st.st_mtim.tv_nsec != iter->second->mtime.tv_nsec) {
            SPDLOG_INFO("动态库{}已变更，等待运行中的请求结束后重新加载", iter->first);
            iter = cache_.erase(iter);
        } else {
            ++iter;
        }
    }
}

void DynamicLibCache::processEvents(int ifd, char *data, ssize_t count) {
    std::lock_guard lock(mutex_);
    ssize_t offset = 0;
    while (offset < count) {
        auto ev = reinterpret_cast<struct inotify_event*>(data + offset);
        offset += sizeof(struct inotify_event) + ev->len;
        std::string name;
        if (ev->len) name.assign(ev->name, strnlen(ev->name, ev->len));
        if (ev->mask & IN_Q_OVERFLOW) {
            SPDLOG_WARN("动态库监听事件队列溢出，重新扫描目录");
            rescan(ifd);
            continue;
        }
        auto iter = watches_.find(ev->wd);
        if (iter == watches_.end()) continue;
        if (ev->mask & IN_IGNORED) {
            watches_.erase(iter);
        } else if (iter->second == webapps_root_) {
            // 应用目录新增或删除
            if (name.empty() || !(ev->mask & IN_ISDIR)) continue;
            if (ev->mask & (IN_CREATE | IN_MOVED_TO)) {
                watchDirectoryLocked(ifd, webapps_root_ + name + "/", kLibMask);
            } else if (ev->mask & (IN_DELETE | IN_MOVED_FROM)) {
                invalidateLocked(webapps_root_ + name + "/" + kLibName);
            }
        } else if (name == kLibName) {
            // libmain.so 发生变更
            if (ev->mask & (IN_CLOSE_WRITE | IN_MODIFY | IN_MOVED_TO | IN_MOVED_FROM | IN_DELETE)) {
                invalidateLocked(iter->second + name);
            }
        }
    }
}

void DynamicLibCache::watchLoop(int ifd) {
    pthread_setname_np(pthread_self(), "libwatch");
    SPDLOG_DEBUG("动态库监听线程已启动");
    std::vector<char> buffer(64 * 1024);
    while (true) {
        struct pollfd fds[2] {};
        fds[0].fd = ifd;
        fds[0].events = POLLIN;
        fds[1].fd = stop_pipe_[0];
        fds[1].events = POLLIN;
        int ret = poll(fds, 2, -1);
        if (ret == -1) {
            if (errno == EINTR) continue;
            SPDLOG_WARN("动态库监听poll出错: {}", strerror(errno));
            break;
        }
        if (fds[1].revents & POLLIN) break; // 收到停止通知
        if (fds[0].revents & POLLIN) {
            ssize_t len = ::read(ifd, buffer.data(), buffer.size());
            if (len <= 0) {
                if (len == 0 || errno != EINTR) {
                    if (len < 0) SPDLOG_WARN("动态库监听读取出错: {}", strerror(errno));
                    break;
                }
                continue;
            }
            processEvents(ifd, buffer.data(), len);
        }
    }
    close(ifd);
    SPDLOG_DEBUG("动态库监听线程已退出");
}
