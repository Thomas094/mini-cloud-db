#pragma once

#include "common/types.h"
#include "common/config.h"
#include <string>
#include <mutex>

namespace minidb {

// ============================================================
// DiskManager - 磁盘I/O管理器
// ============================================================
//
// 【学习要点】
// 这是最底层的I/O组件。生产数据库中，磁盘I/O是性能瓶颈。
// PostgreSQL 使用文件描述符 + pread/pwrite 进行同步I/O。
// 后续你需要将同步I/O 替换为 io_uring 异步I/O（见 IoUringEngine）。
//
// 核心操作只有两个：ReadPage 和 WritePage
// 每个Page在文件中的偏移 = page_id * PAGE_SIZE
//
class DiskManager {
public:
    // 构造函数：打开（或创建）数据库文件
    explicit DiskManager(const std::string& db_file);

    ~DiskManager();

    // 禁止拷贝
    DiskManager(const DiskManager&) = delete;
    DiskManager& operator=(const DiskManager&) = delete;

    // ============================================================
    // TODO: 你来实现 - 读取页面
    // ============================================================
    // 从磁盘读取指定页面到 data 缓冲区
    //
    // 实现提示：
    //   1. 计算文件偏移量：offset = page_id * PAGE_SIZE
    //   2. 使用 pread() 或 lseek() + read() 读取 PAGE_SIZE 字节
    //   3. 读取字节数不足时用0填充（页面可能是新分配的）
    //   4. 注意线程安全（多线程可能同时读不同页面）
    //
    // 参数：
    //   page_id - 要读取的页面ID
    //   data    - 输出缓冲区，大小至少为 PAGE_SIZE
    //
    void ReadPage(page_id_t page_id, char* data);

    // ============================================================
    // TODO: 你来实现 - 写入页面
    // ============================================================
    // 将 data 缓冲区的内容写入磁盘指定页面位置
    //
    // 实现提示：
    //   1. 计算文件偏移量：offset = page_id * PAGE_SIZE
    //   2. 使用 pwrite() 或 lseek() + write() 写入 PAGE_SIZE 字节
    //   3. 必须检查写入是否完整（处理 short write）
    //   4. 考虑是否需要 fsync() 来保证持久性
    //      - WAL 的写入必须 fsync
    //      - 数据页的写入可以延迟（靠WAL保证恢复）
    //
    void WritePage(page_id_t page_id, const char* data);

    // ============================================================
    // TODO: 你来实现 - 分配新页面
    // ============================================================
    // 在文件末尾分配一个新的页面，返回新页面的ID
    //
    // 实现提示：
    //   1. 获取当前文件大小 → 计算下一个 page_id
    //   2. 在文件末尾写入 PAGE_SIZE 个0字节
    //   3. 更新内部的页面计数器
    //   4. 返回新分配的 page_id
    //
    page_id_t AllocatePage();

    // 获取文件描述符（io_uring需要用）
    int GetFileDescriptor() const { return fd_; }

    // 获取已分配的页面总数
    page_id_t GetNumPages() const { return num_pages_; }

private:
    std::string db_file_;          // 数据库文件路径
    int fd_{-1};                   // 文件描述符
    page_id_t num_pages_{0};       // 已分配的页面数
    std::mutex io_mutex_;          // I/O 互斥锁
};

} // namespace minidb
