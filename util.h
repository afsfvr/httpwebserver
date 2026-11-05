#ifndef UTIL_H_
#define UTIL_H_

#include <ctime>
#include <string>
#include <sys/stat.h>

namespace util {

// ---------------- 日期 ----------------

// 当前时间的HTTP日期字符串，每线程缓存整秒精度结果，避免每请求重复 gmtime/strftime 开销。
inline const char *httpDateNow() {
    thread_local struct Cache {
        time_t sec{ 0 };
        char buf[32]{};
    } cache;
    time_t now = time(nullptr);
    if (now != cache.sec) {
        cache.sec = now;
        struct tm tm_val;
        gmtime_r(&now, &tm_val);
        std::strftime(cache.buf, sizeof(cache.buf), "%a, %d %b %Y %H:%M:%S GMT", &tm_val);
    }
    return cache.buf;
}

// 指定时间点的HTTP日期字符串
inline std::string httpDateOf(time_t timestamp) {
    struct tm tm_val;
    gmtime_r(&timestamp, &tm_val);
    char buf[32]{};
    std::strftime(buf, sizeof(buf), "%a, %d %b %Y %H:%M:%S GMT", &tm_val);
    return std::string(buf);
}

// 去除首尾空白符（空格、Tab、回车、换行）
inline std::string trim(const std::string &str) {
    size_t start = str.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = str.find_last_not_of(" \t\r\n");
    return str.substr(start, end - start + 1);
}

// 十六进制字符转0~15数值，非合法字符返回0（urlDecode内部辅助）
inline unsigned char toHex(unsigned char x) {
    if (x >= 'A' && x <= 'Z') return x - 'A' + 10;
    else if (x >= 'a' && x <= 'z') return x - 'a' + 10;
    else if (x >= '0' && x <= '9') return x - '0';
    else return 0;
}

// URL百分号解码，无'%'时零拷贝直接返回
inline std::string urlDecode(const std::string &str) {
    if (str.find('%') == std::string::npos) return str;
    std::string ret;
    size_t length = str.length();
    for (size_t i = 0; i < length; i++) {
        if (i + 2 < length && str[i] == '%') {
            ret += (toHex(str[i + 1]) << 4) | toHex(str[i + 2]);
            i += 2;
        } else {
            ret += str[i];
        }
    }
    return ret;
}

// URL百分号编码，仅保留安全字符：A-Z a-z 0-9 - _ . ~
inline std::string urlEncode(const std::string &value) {
    static constexpr char hex_digits[] = "0123456789ABCDEF";
    std::string escaped;
    escaped.reserve(value.length() * 3);
    for (const unsigned char c: value) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')
            || c == '-' || c == '_' || c == '.' || c == '~') {
            escaped += static_cast<char>(c);
        } else {
            escaped += '%';
            escaped += hex_digits[c >> 4];
            escaped += hex_digits[c & 0x0F];
        }
    }
    return escaped;
}

// 判断路径是否存在且为普通文件
inline bool isFile(const std::string &filename) {
    struct stat st;
    return stat(filename.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

}  // namespace util

#endif

