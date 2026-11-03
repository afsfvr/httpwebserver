#ifndef DYNLIB_CACHE_H_
#define DYNLIB_CACHE_H_

#include <cstdint>
#include <ctime>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

// 注意：base_class.h 在头文件内定义了 BaseClass 的方法实现，
// 只能被单个 .cpp 引用，这里仅使用前置声明。
class BaseClass;

/**
 * 动态库缓存管理
 * 以进程为单位缓存已装载的动态库，避免每次请求重复 dlopen/dlclose；
 * 同时使用 inotify 监听 webapps 目录，libmain.so 变更后自动失效重新加载，
 * 运行中的请求通过 shared_ptr 持有旧库，结束后才会真正卸载。
 */
class DynamicLibCache {
public:
    struct LibEntry {
        void *handle{nullptr};
        struct timespec mtime{0, 0};      // 装载时的 libmain.so 修改时间（兜底校验用）
        BaseClass *(*create)(){nullptr};  // createClass
        void (*destroy)(BaseClass*){nullptr}; // deleteClass
        ~LibEntry();
    };

    static DynamicLibCache& instance();

    // 启动 inotify 监听线程（须在 daemonize fork 之后调用）
    void start(const std::string &webappsPath);
    // 停止监听线程并清空缓存
    void stop();
    // 获取指定路径的动态库，未缓存则装载，失败返回nullptr
    std::shared_ptr<LibEntry> acquire(const std::string &path);

private:
    DynamicLibCache() = default;
    // exit handler 是 LIFO：静态单例的析构可能先于 hookFunction 中的 stop()，
    // 必须在此先停止监听线程，否则 joinable 的 std::thread 析构会 terminate
    ~DynamicLibCache() { stop(); }
    DynamicLibCache(const DynamicLibCache&) = delete;
    DynamicLibCache& operator=(const DynamicLibCache&) = delete;

    // 装载新的动态库（冷路径，须持有 mutex_）；先复制源文件为私有副本再 dlopen，
    // 避免 ld/cp 原地直写破坏已映射的代码页导致崩溃
    std::shared_ptr<LibEntry> loadLib(const std::string &path);
    // 建立私有副本目录
    bool ensureCopyDir();
    // 文件复制（写临时文件+原子rename覆盖）
    static bool copyLibFile(const std::string &src, const std::string &dst);
    // 监听一个目录，须持有 mutex_
    void watchDirectoryLocked(int ifd, const std::string &dir, uint32_t mask);
    // 处理一批 inotify 事件，内部自行加锁
    void processEvents(int ifd, char *data, ssize_t count);
    // 事件队列溢出后重新扫描目录与已缓存动态库，须持有 mutex_
    void rescan(int ifd);
    void watchLoop(int ifd);
    // 使指定路径的动态库失效，须持有 mutex_
    void invalidateLocked(const std::string &path);

    std::mutex mutex_;
    int stop_pipe_[2]{-1, -1};
    std::string webapps_root_;
    std::string copy_dir_;
    std::thread watcher_;
    std::unordered_map<std::string, std::shared_ptr<LibEntry>> cache_;
    std::unordered_map<int, std::string> watches_; // wd -> 监听的目录
};

#endif // DYNLIB_CACHE_H_
