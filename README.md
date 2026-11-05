# http服务器

使用 C++17 实现的 Web 服务器，采用「线程池 + 非阻塞 socket + epoll + Reactor」并发模型，基于 spdlog 实现日志系统，通过动态链接库实现应用插件化部署，支持 HTTPS、IPv4/IPv6、会话管理（Redis 存储）与 IP 黑名单。

## 特性

- **Reactor 并发模型**：主线程 epoll 处理连接的注册与分发，工作线程池处理 I/O 与业务逻辑
- **HTTP/HTTPS**：基于 OpenSSL 实现 TLS（ALPN 协商），支持 PROXY Protocol V1 头解析
- **完整方法支持**：GET / HEAD / POST / PUT / DELETE / OPTIONS / TRACE
- **插件化应用**：每个应用（webapp）编译为动态库，通过 `dlopen` 装载，基于 `inotify` 实现热更新
- **会话管理**：`Session` + `Cookie` 支持，会话数据可存入 Redis（需内置连接池）
- **日志系统**：控制台彩色输出 + 滚动文件（默认 10MB x 10 份），支持 7 个日志级别与自定义线程名格式
- **安全性**：IP 黑名单（持久化到 `black.txt`）、路径合法性校验、Redis 连接池限制并发
- **守护进程**：支持 `-d` 以 daemon 方式后台运行，优雅退出（SIGINT/SIGQUIT/SIGTERM）

## 目录结构

```text
├── main.cpp            程序入口：配置解析、daemon 化、日志初始化
├── webserver.h/.cpp    epoll 事件循环，监听连接、分发请求
├── threadpool.h/.cpp   线程池，执行业务逻辑
├── http_connect.h/.cpp 单个连接封装，解析/发送 HTTP 报文
├── request.h/.cpp      请求封装：URL、请求头、参数、body 读取
├── response.h/.cpp     响应封装：状态码、响应头、Cookie、chunked 写入
├── base_class.h        webapp 抽象基类 BaseClass，导出 createClass/deleteClass
├── dynlib_cache.h/.cpp 动态库缓存与 inotify 热更新
├── config.h/.cpp       单例配置：命令行解析
├── cookie.h/.cpp       Cookie 封装
├── session.h/.cpp      会话封装（USE_REDIS 时启用）
├── redis_pool.h/.cpp   Redis 连接池（USE_REDIS 时启用）
├── blacklist.h/.cpp    IP 黑名单
├── block_queue.h       线程安全阻塞队列
└── webapps/            应用插件目录，每个子目录编译为一个 libmain.so
    ├── root/           根路径应用（静态资源路由）
    └── upload/         文件上传/下载应用
```

## 编译

依赖：Linux、g++ 或 clang++（C++17）、CMake >= 3.5、pthread、dl；启用 HTTPS 需 OpenSSL。

```bash
cmake -S . -B build -DBUILD_WEBAPPS=ON   # 可选：-DLOG=ON -DREDIS=ON -DHTTPS=ON -DNGINX=ON
cmake --build build -j
```

CMake 选项：

| 选项 | 默认 | 说明 |
|------|------|------|
| `LOG` | `ON` | 日志。关闭后以 `NO_LOG` 编译，日志全部失效 |
| `REDIS` | `OFF` | 会话存储与 Redis 连接池，链接 hiredis |
| `HTTPS` | `OFF` | TLS 支持，链接 OpenSSL |
| `NGINX` | `OFF` | nginx 部署模式 |
| `BUILD_WEBAPPS` | `OFF` | 同时编译 `webapps/` 下的应用插件 |

> 提示：绑定 1024 以下端口需要 root 权限，可通过 `make authorize` 授予可执行文件 `cap_net_bind_service` 能力。

## 运行

```bash
./main -p 8080 -W /home/user/site -l info -f logs/server.log
```

| 短参数 | 长参数 | 说明 |
|--------|--------|------|
| `-4` / `-6` | `--ipv4` / `--ipv6` | 允许 IPv4 / IPv6 访问，不指定则二者均允许 |
| `-p` | `--port` | 监听端口，默认 `8888` |
| `-t` | `--threads` | 工作线程数，默认 `8` |
| `-W` | `--workdir` | 工作路径（绝对路径），默认为当前目录 |
| `-d` | `--daemon` | 以守护进程方式运行 |
| `-l` | `--log-level` | 日志级别：0-trace ~ 6-off，默认 `2`（info） |
| `-f` | `--log-file` | 日志文件路径，不指定则仅输出到控制台 |
| `-w` | `--webapps` | 动态库所在路径，默认为 `工作路径/webapps/` |
| `-r` | `--root-url` | 根路径（访问 `ip:port` 时的应用），默认 `/root/` |
| | `--proxy-protocol` | 启用 PROXY Protocol V1 头解析（非 HTTPS 构建） |
| `-i` `-P` `-u` `-m` `-M` `-c` 等 | `--redis-*` | Redis 地址/端口/认证与连接池参数（REDIS 构建） |
| | `--ssl-cert` / `--ssl-key` | TLS 证书与私钥路径（HTTPS 构建） |
| `-h` | `--help` | 查看帮助 |

## Webapp 开发

一个 webapp 即一个或多个 `.cpp` 编译成的动态库 `libmain.so`，运行时被主程序 `dlopen` 装载。其 `Request`/`Response` 符号由携带 `-rdynamic` 的主程序导出提供，因此插件无需（也不应）编译服务器本体代码，只依赖头文件。

最小示例 `webapps/root/main.cpp`：

```cpp
#include "../../base_class.h"

class Root : public BaseClass {
public:
    // 返回空串表示已直接写出响应，否则按返回的文件名发送静态资源
    std::string doGet(Request *request, Response *response, const std::string &cur_path) override {
        std::string url = request->getUrl().substr(5); // 去掉 "/root"
        return url.empty() ? "resources/index.html" : "resources" + url;
    }
};

extern "C" BaseClass* createClass() { return new Root(); }    // 装载入口
extern "C" void deleteClass(BaseClass *base) { delete base; } // 卸载入口
```

`CMakeLists.txt`（以 `webapps/root/` 为例）：

```cmake
file(GLOB SRC *.cpp)
add_library(root SHARED ${SRC})
set_target_properties(root PROPERTIES OUTPUT_NAME "main" POSITION_INDEPENDENT_CODE ON)
set_target_properties(root PROPERTIES LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
```

热更新机制：服务器以进程为单位缓存已装载的库，`inotify` 监听 webapps 目录；`libmain.so` 变更后缓存自动失效并重新装载，正在处理的请求通过 `shared_ptr` 持有旧库，结束后才真正卸载。因此运行中直接重新编译插件即可生效，无需重启服务器。

`BaseClass` 中各 `doXxx()` 默认返回 405，只需覆盖关心的方法（见 `base_class.h`）；`service()` 统一分发 HTTP 方法。
