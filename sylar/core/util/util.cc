#include <unistd.h>
#include <string.h>
#include <time.h>
#include <dirent.h>
#include <signal.h> // for kill()
#include <sys/syscall.h>
#include <sys/stat.h>
#include <execinfo.h> // for backtrace()
#include <cxxabi.h>   // for abi::__cxa_demangle()
#include <algorithm>  // for std::transform()
#include <stdarg.h>

#include "sylar/core/util/util.h"
#include "sylar/core/log/log.h"
#include "sylar/core/fiber.h"
#include "sylar/core/memory/memorypool.h"

namespace sylar {

static sylar::Logger::ptr g_logger = SYLAR_LOG_NAME("system");

pid_t GetThreadId() {
    return syscall(SYS_gettid);
}

uint64_t GetFiberId() {
    return Fiber::GetFiberId();
}

uint64_t GetElapsedMS() {
    struct timespec ts = {0};
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

std::string GetThreadName() {
    char thread_name[16] = {0};
    pthread_getname_np(pthread_self(), thread_name, 16);
    return std::string(thread_name);
}

void SetThreadName(const std::string &name) {
    pthread_setname_np(pthread_self(), name.substr(0, 15).c_str());
}

/**
 * @brief 解析C++符号的混淆名称（demangle）
 * 
 * 该函数尝试解析两种格式的混淆符号：
 * 1. 包含在括号中的复杂符号（如类型信息）
 * 2. 简单的符号字符串
 * 
 * @param str 需要解析的混淆符号字符串指针
 * @return std::string 解析后的可读字符串，若解析失败返回原始字符串或简单符号
 */
static std::string demangle(const char *str) {
    size_t size = 0;
    int status  = 0;
    std::string rt;
    rt.resize(256);

    // 尝试解析带括号的复杂符号格式（如类型信息）
    // 格式说明：跳过'('前内容，跳过'_'前内容，捕获到')'或'+'前的内容
    if (1 == sscanf(str, "%*[^(]%*[^_]%255[^)+]", &rt[0])) {
        // 使用ABI函数进行demangle处理
        char *v = abi::__cxa_demangle(&rt[0], nullptr, &size, &status);
        if (v) {
            std::string result(v);
            free(v);
            return result;
        }
    }

    // 若复杂符号解析失败，尝试解析简单符号格式
    if (1 == sscanf(str, "%255s", &rt[0])) {
        return rt;
    }

    // 完全解析失败时返回原始字符串
    return str;
}

void Backtrace(std::vector<std::string> &bt, int size, int skip) {
    void **array = (void **)malloc((sizeof(void *) * size));
    size_t s     = ::backtrace(array, size);

    char **strings = backtrace_symbols(array, s);
    if (strings == NULL) {
        SYLAR_LOG_ERROR(g_logger) << "backtrace_synbols error";
        return;
    }

    for (size_t i = skip; i < s; ++i) {
        bt.push_back(demangle(strings[i]));
    }

    free(strings);
    free(array);
}

std::string BacktraceToString(int size, int skip, const std::string &prefix) {
    std::vector<std::string> bt;
    Backtrace(bt, size, skip);
    std::stringstream ss;
    for (size_t i = 0; i < bt.size(); ++i) {
        ss << prefix << bt[i] << std::endl;
    }
    return ss.str();
}

uint64_t GetCurrentMS() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000ul + tv.tv_usec / 1000;
}

uint64_t GetCurrentUS() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000 * 1000ul + tv.tv_usec;
}

std::string ToUpper(const std::string &name) {
    std::string rt = name;
    std::transform(rt.begin(), rt.end(), rt.begin(), ::toupper);
    return rt;
}

std::string ToLower(const std::string &name) {
    std::string rt = name;
    std::transform(rt.begin(), rt.end(), rt.begin(), ::tolower);
    return rt;
}

std::string Time2Str(time_t ts, const std::string &format) {
    struct tm tm;
    localtime_r(&ts, &tm);
    char buf[64];
    strftime(buf, sizeof(buf), format.c_str(), &tm);
    return buf;
}

time_t Str2Time(const char *str, const char *format) {
    struct tm t;
    memset(&t, 0, sizeof(t));
    if (!strptime(str, format, &t)) {
        return 0;
    }
    return mktime(&t);
}

/// ******************************** FSUtil ********************************
void FSUtil::ListAllFile(std::vector<std::string> &files, const std::string &path, const std::string &subfix) {
    if (access(path.c_str(), 0) != 0) {
        return;
    }
    DIR *dir = opendir(path.c_str());
    if (dir == nullptr) {
        return;
    }
    struct dirent *dp = nullptr;
    while ((dp = readdir(dir)) != nullptr) {
        if (dp->d_type == DT_DIR) {
            if (!strcmp(dp->d_name, ".") || !strcmp(dp->d_name, "..")) {
                continue;
            }
            ListAllFile(files, path + "/" + dp->d_name, subfix);
        } else if (dp->d_type == DT_REG) {
            std::string filename(dp->d_name);
            if (subfix.empty()) {
                files.push_back(path + "/" + filename);
            } else {
                if (filename.size() < subfix.size()) {
                    continue;
                }
                if (filename.substr(filename.length() - subfix.size()) == subfix) {
                    files.push_back(path + "/" + filename);
                }
            }
        }
    }
    closedir(dir);
}

static int __lstat(const char *file, struct stat *st = nullptr) {
    struct stat lst;
    int ret = lstat(file, &lst);
    if (st) {
        *st = lst;
    }
    return ret;
}

static int __mkdir(const char *dirname) {
    if (access(dirname, F_OK) == 0) {
        return 0;
    }
    return mkdir(dirname, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
}

bool FSUtil::Mkdir(const std::string &dirname) {
    if (__lstat(dirname.c_str()) == 0) {
        return true;
    }
    char *path = strdup(dirname.c_str());
    char *ptr  = strchr(path + 1, '/');
    do {
        for (; ptr; *ptr = '/', ptr = strchr(ptr + 1, '/')) {
            *ptr = '\0';
            if (__mkdir(path) != 0) {
                break;
            }
        }
        if (ptr != nullptr) {
            break;
        } else if (__mkdir(path) != 0) {
            break;
        }
        free(path);
        return true;
    } while (0);
    free(path);
    return false;
}

bool FSUtil::IsRunningPidfile(const std::string &pidfile) {
    if (__lstat(pidfile.c_str()) != 0) {
        return false;
    }
    std::ifstream ifs(pidfile);
    std::string line;
    if (!ifs || !std::getline(ifs, line)) {
        return false;
    }
    if (line.empty()) {
        return false;
    }
    pid_t pid = atoi(line.c_str());
    if (pid <= 1) {
        return false;
    }
    if (kill(pid, 0) != 0) {
        return false;
    }
    return true;
}

bool FSUtil::Unlink(const std::string &filename, bool exist) {
    if (!exist && __lstat(filename.c_str())) {
        return true;
    }
    return ::unlink(filename.c_str()) == 0;
}

bool FSUtil::Rm(const std::string &path) {
    struct stat st;
    if (lstat(path.c_str(), &st)) {
        return true;
    }
    if (!(st.st_mode & S_IFDIR)) {
        return Unlink(path);
    }

    DIR *dir = opendir(path.c_str());
    if (!dir) {
        return false;
    }

    bool ret          = true;
    struct dirent *dp = nullptr;
    while ((dp = readdir(dir))) {
        if (!strcmp(dp->d_name, ".") || !strcmp(dp->d_name, "..")) {
            continue;
        }
        std::string dirname = path + "/" + dp->d_name;
        ret                 = Rm(dirname);
    }
    closedir(dir);
    if (::rmdir(path.c_str())) {
        ret = false;
    }
    return ret;
}

bool FSUtil::Mv(const std::string &from, const std::string &to) {
    if (!Rm(to)) {
        return false;
    }
    return rename(from.c_str(), to.c_str()) == 0;
}

bool FSUtil::Realpath(const std::string &path, std::string &rpath) {
    if (__lstat(path.c_str())) {
        return false;
    }
    char *ptr = ::realpath(path.c_str(), nullptr);
    if (nullptr == ptr) {
        return false;
    }
    std::string(ptr).swap(rpath);
    free(ptr);
    return true;
}

bool FSUtil::Symlink(const std::string &from, const std::string &to) {
    if (!Rm(to)) {
        return false;
    }
    return ::symlink(from.c_str(), to.c_str()) == 0;
}

std::string FSUtil::Dirname(const std::string &filename) {
    if (filename.empty()) {
        return ".";
    }
    auto pos = filename.rfind('/');
    if (pos == 0) {
        return "/";
    } else if (pos == std::string::npos) {
        return ".";
    } else {
        return filename.substr(0, pos);
    }
}

std::string FSUtil::Basename(const std::string &filename) {
    if (filename.empty()) {
        return filename;
    }
    auto pos = filename.rfind('/');
    if (pos == std::string::npos) {
        return filename;
    } else {
        return filename.substr(pos + 1);
    }
}

bool FSUtil::OpenForRead(std::ifstream &ifs, const std::string &filename, std::ios_base::openmode mode) {
    ifs.open(filename.c_str(), mode);
    return ifs.is_open();
}

bool FSUtil::OpenForWrite(std::ofstream &ofs, const std::string &filename, std::ios_base::openmode mode) {
    ofs.open(filename.c_str(), mode);
    if (!ofs.is_open()) {
        std::string dir = Dirname(filename);
        Mkdir(dir);
        ofs.open(filename.c_str(), mode);
    }
    return ofs.is_open();
}


/// ******************************** TypeUtil ********************************

int8_t TypeUtil::ToChar(const std::string &str) {
    if (str.empty()) {
        return 0;
    }
    return *str.begin();
}

int64_t TypeUtil::Atoi(const std::string &str) {
    if (str.empty()) {
        return 0;
    }
    return strtoull(str.c_str(), nullptr, 10);
}

double TypeUtil::Atof(const std::string &str) {
    if (str.empty()) {
        return 0;
    }
    return atof(str.c_str());
}

int8_t TypeUtil::ToChar(const char *str) {
    if (str == nullptr) {
        return 0;
    }
    return str[0];
}

int64_t TypeUtil::Atoi(const char *str) {
    if (str == nullptr) {
        return 0;
    }
    return strtoull(str, nullptr, 10);
}

double TypeUtil::Atof(const char *str) {
    if (str == nullptr) {
        return 0;
    }
    return atof(str);
}

std::string StringUtil::Format(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    auto v = Formatv(fmt, ap);
    va_end(ap);
    return v;
}

std::string StringUtil::Formatv(const char* fmt, va_list ap) {
    char* buf = nullptr;
    auto len = vasprintf(&buf, fmt, ap);
    if(len == -1) {
        return "";
    }
    std::string ret(buf, len);
    free(buf);
    return ret;
}

static const char uri_chars[256] = {
    /* 0 */
    0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 1, 1, 0,
    1, 1, 1, 1, 1, 1, 1, 1,   1, 1, 0, 0, 0, 1, 0, 0,
    /* 64 */
    0, 1, 1, 1, 1, 1, 1, 1,   1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1,   1, 1, 1, 0, 0, 0, 0, 1,
    0, 1, 1, 1, 1, 1, 1, 1,   1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1,   1, 1, 1, 0, 0, 0, 1, 0,
    /* 128 */
    0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
    /* 192 */
    0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
};

static const char xdigit_chars[256] = {
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,1,2,3,4,5,6,7,8,9,0,0,0,0,0,0,
    0,10,11,12,13,14,15,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,10,11,12,13,14,15,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
};

#define CHAR_IS_UNRESERVED(c)           \
    (uri_chars[(unsigned char)(c)])

//-.0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ_abcdefghijklmnopqrstuvwxyz~
/**
 * UrlEncode函数用于将字符串进行URL编码
 * 
 * @param str 需要进行URL编码的字符串
 * @param space_as_plus 指定空格是否编码为加号，为false时空格编码为%20
 * @return 返回URL编码后的字符串
 * 
 * 本函数通过遍历输入字符串，对非保留字符进行编码非保留字符被编码为%加上其ASCII码的十六进制表示
 * 空格的编码方式由space_as_plus参数决定，为true时编码为加号，否则编码为%20
 * 为了提高效率，只有当遇到需要编码的字符时，才会创建新的字符串对象进行编码
 */
std::string StringUtil::UrlEncode(const std::string& str, bool space_as_plus) {
    // 定义十六进制数字字符，用于字符编码
    static const char *hexdigits = "0123456789ABCDEF";
    // 初始化字符串流指针，用于构建编码后的字符串
    std::string* ss = nullptr;
    // 获取字符串末尾位置，用于遍历
    const char* end = str.c_str() + str.length();
    // 遍历字符串中的每个字符
    for(const char* c = str.c_str() ; c < end; ++c) {
        // 检查字符是否需要编码
        if(!CHAR_IS_UNRESERVED(*c)) {
            // 如果尚未创建字符串流，则进行初始化，并预留足够空间
            if(!ss) {
                ss = new std::string;
                ss->reserve(str.size() * 1.2);
                // 将之前未编码的字符添加到字符串流中
                ss->append(str.c_str(), c - str.c_str());
            }
            // 空格的编码方式由space_as_plus参数决定
            if(*c == ' ' && space_as_plus) {
                ss->append(1, '+');
            } else {
                // 对非保留字符进行编码
                ss->append(1, '%');
                ss->append(1, hexdigits[(uint8_t)*c >> 4]);
                ss->append(1, hexdigits[*c & 0xf]);
            }
        } else if(ss) {
            // 如果当前字符是保留字符且已经创建了字符串流，则直接添加到字符串流中
            ss->append(1, *c);
        }
    }
    // 如果没有进行任何编码，直接返回原始字符串
    if(!ss) {
        return str;
    } else {
        // 如果进行了编码，创建最终的字符串结果，并释放内存
        std::string rt = *ss;
        delete ss;
        return rt;
    }
}

/**
 * UrlDecode函数用于解码URL中的字符串。
 * 它会将URL中的加号替换为空格（如果space_as_plus为true），并将百分号编码的字符转换回原始字符。
 * 
 * @param str 待解码的URL字符串。
 * @param space_as_plus 指示是否将加号解码为单个空格。如果为false，加号将保持不变。
 * @return 解码后的字符串。
 */
std::string StringUtil::UrlDecode(const std::string& str, bool space_as_plus) {
    // 指向正在构建的解码字符串的指针，初始为nullptr，因为在开始时我们不知道是否需要解码。
    std::string* ss = nullptr;
    
    // 获取字符串的末尾位置，用于边界检查。
    const char* end = str.c_str() + str.length();
    
    // 遍历输入字符串的每个字符。
    for(const char* c = str.c_str(); c < end; ++c) {
        // 如果当前字符是加号且space_as_plus为true，将其解码为单个空格。
        if(*c == '+' && space_as_plus) {
            // 如果ss为空，说明这是第一次需要解码，我们创建一个新的字符串并添加之前遍历过的字符。
            if(!ss) {
                ss = new std::string;
                ss->append(str.c_str(), c - str.c_str());
            }
            // 添加单个空格到解码字符串中。
            ss->append(1, ' ');
        } 
        // 如果当前字符是百分号且后面有两个十六进制数字，将其解码为对应的字符。
        else if(*c == '%' && (c + 2) < end
                    && isxdigit(*(c + 1)) && isxdigit(*(c + 2))){
            // 同样，如果ss为空，创建一个新的字符串并添加之前遍历过的字符。
            if(!ss) {
                ss = new std::string;
                ss->append(str.c_str(), c - str.c_str());
            }
            // 将百分号编码的字符转换回原始字符。
            ss->append(1, (char)(xdigit_chars[(int)*(c + 1)] << 4 | xdigit_chars[(int)*(c + 2)]));
            // 跳过已经处理过的两个十六进制数字。
            c += 2;
        } 
        // 如果已经有解码操作发生（即ss不为空），将当前字符添加到解码字符串中。
        else if(ss) {
            ss->append(1, *c);
        }
    }
    // 如果没有进行任何解码操作，直接返回原始字符串。
    if(!ss) {
        return str;
    } 
    // 否则，返回解码后的字符串，并释放分配的内存。
    else {
        std::string rt = *ss;
        delete ss;
        return rt;
    }
}

/**
 * @brief 字符串修剪函数
 * 
 * 该函数用于移除字符串str两端包含的delimit指定的字符。如果str的两端存在delimit中指定的字符，
 * 则从字符串两端删除这些字符，直到遇到第一个不属于delimit的字符为止。
 * 
 * @param str 待处理的字符串
 * @param delimit 需要移除的字符集合
 * @return std::string 返回修剪后的字符串
 */
std::string StringUtil::Trim(const std::string& str, const std::string& delimit) {
    // 查找字符串中第一个不属于delimit的字符位置
    auto begin = str.find_first_not_of(delimit);
    // 如果整个字符串都由delimit中的字符组成，则返回空字符串
    if(begin == std::string::npos) {
        return "";
    }
    // 查找字符串中最后一个不属于delimit的字符位置
    auto end = str.find_last_not_of(delimit);
    // 提取并返回从第一个非delimit字符到最后一个非delimit字符之间的子字符串
    return str.substr(begin, end - begin + 1);
}

std::string StringUtil::TrimLeft(const std::string& str, const std::string& delimit) {
    auto begin = str.find_first_not_of(delimit);
    if(begin == std::string::npos) {
        return "";
    }
    return str.substr(begin);
}

std::string StringUtil::TrimRight(const std::string& str, const std::string& delimit) {
    auto end = str.find_last_not_of(delimit);
    if(end == std::string::npos) {
        return "";
    }
    return str.substr(0, end);
}

std::string StringUtil::WStringToString(const std::wstring& ws) {
    std::string str_locale = setlocale(LC_ALL, "");
    const wchar_t* wch_src = ws.c_str();
    size_t n_dest_size = wcstombs(NULL, wch_src, 0) + 1;
    char *ch_dest = (char*)SYLAR_THREAD_MALLOC(n_dest_size);
    memset(ch_dest,0,n_dest_size);
    wcstombs(ch_dest,wch_src,n_dest_size);
    std::string str_result = ch_dest;
    SYLAR_THREAD_FREE(ch_dest, n_dest_size);
    setlocale(LC_ALL, str_locale.c_str());
    return str_result;
}

std::wstring StringUtil::StringToWString(const std::string& s) {
    std::string str_locale = setlocale(LC_ALL, "");
    const char* chSrc = s.c_str();
    size_t n_dest_size = mbstowcs(NULL, chSrc, 0) + 1;
    wchar_t* wch_dest = new wchar_t[n_dest_size];
    wmemset(wch_dest, 0, n_dest_size);
    mbstowcs(wch_dest,chSrc,n_dest_size);
    std::wstring wstr_result = wch_dest;
    delete []wch_dest;
    setlocale(LC_ALL, str_locale.c_str());
    return wstr_result;
}





} // namespace sylar