#include "storage/io_uring_engine.h"
#include <stdexcept>
#include <vector>

// 如果系统安装了 liburing，取消下面的注释
// #include <liburing.h>

namespace minidb {

IoUringEngine::IoUringEngine(unsigned queue_depth) : queue_depth_(queue_depth) {
    // ============================================================
    // TODO: 你来实现 - 初始化 io_uring
    // ============================================================
    //
    // #ifdef HAS_IO_URING
    //   ring_ptr_ = new struct io_uring;
    //   auto* ring = static_cast<struct io_uring*>(ring_ptr_);
    //
    //   // 初始化 io_uring 实例
    //   int ret = io_uring_queue_init(queue_depth_, ring, 0);
    //   if (ret < 0) {
    //       delete ring;
    //       ring_ptr_ = nullptr;
    //       throw std::runtime_error("io_uring_queue_init 失败: " + std::to_string(-ret));
    //   }
    //   initialized_ = true;
    // #else
    //   // 没有 liburing，回退到同步模式（仅用于开发测试）
    //   initialized_ = false;
    // #endif
}

IoUringEngine::~IoUringEngine() {
    // #ifdef HAS_IO_URING
    //   if (initialized_ && ring_ptr_) {
    //       auto* ring = static_cast<struct io_uring*>(ring_ptr_);
    //       io_uring_queue_exit(ring);
    //       delete ring;
    //   }
    // #endif
}

// ============================================================
// TODO: 你来实现
// ============================================================
void IoUringEngine::Submit(const IoRequest& request) {
    // 参考 io_uring_engine.h 中的注释
    //
    // 【fallback 方案】如果没有 liburing，可以回退到同步 I/O：
    //   switch (request.op) {
    //       case IoOpType::READ:
    //           pread(request.fd, request.buf, request.len, request.offset);
    //           break;
    //       case IoOpType::WRITE:
    //           pwrite(request.fd, request.buf, request.len, request.offset);
    //           break;
    //       case IoOpType::FSYNC:
    //           fsync(request.fd);
    //           break;
    //   }
    //   if (request.callback) request.callback(0);

    throw std::runtime_error("IoUringEngine::Submit: 尚未实现");
}

// ============================================================
// TODO: 你来实现
// ============================================================
void IoUringEngine::SubmitBatch(const std::vector<IoRequest>& requests) {
    // 批量提交的关键：先把所有 SQE 填好，最后一次 io_uring_submit
    //
    // for (auto& req : requests) {
    //     io_uring_sqe* sqe = io_uring_get_sqe(ring);
    //     // 设置 sqe...（同 Submit）
    // }
    // io_uring_submit(ring);  // 一次系统调用提交所有请求
    //
    // 【性能关键】
    //   假设 Buffer Pool 需要预取10个页面：
    //   - 同步 I/O: 10次 pread 系统调用，串行等待
    //   - io_uring: 1次 submit 系统调用，内核并行执行

    throw std::runtime_error("IoUringEngine::SubmitBatch: 尚未实现");
}

// ============================================================
// TODO: 你来实现
// ============================================================
void IoUringEngine::WaitCompletion() {
    // 参考 io_uring_engine.h 中的注释

    throw std::runtime_error("IoUringEngine::WaitCompletion: 尚未实现");
}

// ============================================================
// TODO: 你来实现
// ============================================================
int IoUringEngine::PollCompletions() {
    // 非阻塞版本：
    //
    // int count = 0;
    // io_uring_cqe* cqe;
    // while (io_uring_peek_cqe(ring, &cqe) == 0) {
    //     auto* req = (IoRequest*)io_uring_cqe_get_data(cqe);
    //     if (req && req->callback) req->callback(cqe->res);
    //     io_uring_cqe_seen(ring, cqe);
    //     delete req;
    //     count++;
    // }
    // return count;

    throw std::runtime_error("IoUringEngine::PollCompletions: 尚未实现");
}

} // namespace minidb
