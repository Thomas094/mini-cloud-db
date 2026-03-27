#include "storage/disk_manager.h"

#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/stat.h>
#include <stdexcept>
#include <cstring>

namespace minidb {

DiskManager::DiskManager(const std::string& db_file) : db_file_(db_file) {
    // 打开文件，不存在则创建，读写模式
    fd_ = open(db_file.c_str(), O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
    if (fd_ < 0) {
        throw std::runtime_error("DiskManager: 无法打开文件 " + db_file);
    }

    // 获取文件大小，计算已有的页面数
    struct stat file_stat;
    if (fstat(fd_, &file_stat) < 0) {
        close(fd_);
        throw std::runtime_error("DiskManager: 无法获取文件状态");
    }
    num_pages_ = static_cast<page_id_t>(file_stat.st_size / PAGE_SIZE);
}

DiskManager::~DiskManager() {
    if (fd_ >= 0) {
        close(fd_);
        fd_ = -1;
    }
}

// ============================================================
// TODO: 你来实现 ReadPage
// ============================================================
void DiskManager::ReadPage(page_id_t page_id, char* data) {
    // 实现提示（完成后删除这些注释）：
    //
    {
      std::lock_guard<std::mutex> lock(io_mutex_);
      if (page_id >= num_pages_) {
        throw std::runtime_error("ReadPage: 页面ID超出文件范围");
      }
    }
    //
    // 1. 计算偏移: 
    off_t offset = static_cast<off_t>(page_id) * PAGE_SIZE;
    //
    // 2. 使用 pread 读取:
    ssize_t bytes_total_read = 0;
    while (bytes_total_read < PAGE_SIZE) {
      ssize_t ret = pread(fd_, data + bytes_total_read, PAGE_SIZE - bytes_total_read,
                          offset + bytes_total_read);
      if (ret < 0)
        throw std::runtime_error("ReadPage: 读取失败 ret = " + std::to_string(ret));
      bytes_total_read += ret;
      if (ret == 0 && bytes_total_read < PAGE_SIZE) {
        memset(data + bytes_total_read, 0, PAGE_SIZE - bytes_total_read);
        break;
      }
    }

    return;
    //
    // 3. 处理读取结果:
    //    - bytes_read < 0: 抛出异常
    //    - bytes_read < PAGE_SIZE: 用0填充剩余部分
    //      memset(data + bytes_read, 0, PAGE_SIZE - bytes_read);
    //
    // 【思考题】为什么 pread 比 lseek + read 更适合多线程场景？
    // 答：pread 是原子操作，不会受其他线程 lseek 的影响

}

// ============================================================
// TODO: 你来实现 WritePage
// ============================================================
void DiskManager::WritePage(page_id_t page_id, const char* data) {
    // 实现提示（完成后删除这些注释）：
    //
    //
    // 1. 计算偏移: 
    off_t offset = static_cast<off_t>(page_id) * PAGE_SIZE;
    //
    // 2. 使用 pwrite 写入（需要处理 short write）:
    ssize_t bytes_written = 0;
    while (bytes_written < PAGE_SIZE) {
      ssize_t ret = pwrite(fd_, data + bytes_written, PAGE_SIZE - bytes_written,
                           offset + bytes_written);
      if (ret < 0)
        throw std::runtime_error("WritePage: 写入失败 ret = " + std::to_string(ret));
      bytes_written += ret;
    }
    return ;
    //
    // 【深入思考】什么时候需要 fsync？
    //   - checkpoint 时需要 fsync 数据页
    //   - WAL commit 时需要 fsync 日志
    //   - 正常写入可以不 fsync（靠 WAL 保证持久性）

}

// ============================================================
// TODO: 你来实现 AllocatePage
// ============================================================
page_id_t DiskManager::AllocatePage() {
    // 实现提示（完成后删除这些注释）：
    //
    std::lock_guard<std::mutex> lock(io_mutex_);
    //
    // 1. 新页面ID就是当前的 num_pages_
    page_id_t new_page_id = num_pages_;
    //
    // 2. 在文件末尾写入空页面:
    char empty_page[PAGE_SIZE] = {0};
    off_t offset = static_cast<off_t>(new_page_id) * PAGE_SIZE;
    ssize_t bytes_written = 0;
    while (bytes_written < PAGE_SIZE) {
      ssize_t ret = pwrite(fd_, empty_page + bytes_written, PAGE_SIZE - bytes_written,
                           offset + bytes_written);
      if (ret < 0)
        throw std::runtime_error("AllocatePage: 写入失败 ret = " + std::to_string(ret));
      bytes_written += ret;
    }
    num_pages_++;
    return new_page_id;

}

} // namespace minidb
