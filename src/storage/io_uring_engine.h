#pragma once

#include "common/config.h"
#include <functional>
#include <string>

namespace minidb {

// ============================================================
// IoUringEngine - 基于 io_uring 的异步I/O引擎
// ============================================================
//
// 【面试核心知识点 - io_uring】
//
// io_uring 是 Linux 5.1 引入的高性能异步 I/O 接口。
// 它通过两个环形队列实现用户态和内核态的零拷贝通信：
//
// +--------------------+        +--------------------+
// | Submission Queue   |  --->  | Completion Queue   |
// | (SQ - 提交队列)     |        | (CQ - 完成队列)    |
// +--------------------+        +--------------------+
//    用户态填入请求              内核态返回结果
//
// 工作流程：
//   1. 用户将 I/O 请求（sqe）放入 SQ
//   2. 调用 io_uring_submit() 通知内核
//   3. 内核异步执行 I/O
//   4. 完成后将结果（cqe）放入 CQ
//   5. 用户从 CQ 获取结果
//
// 与传统 I/O 对比：
//   +------------------+--------+-----------+------------+
//   |                  | 同步   | epoll+NIO | io_uring   |
//   +------------------+--------+-----------+------------+
//   | 系统调用次数      | 每次1个 | 2个       | 批量提交   |
//   | 内核-用户态切换   | 多次   | 多次      | 极少       |
//   | 零拷贝           | 否     | 否        | 支持       |
//   | 适用场景          | 简单   | 网络      | 通用       |
//   +------------------+--------+-----------+------------+
//
// 为什么数据库需要 io_uring？
//   - Buffer Pool 需要频繁进行磁盘 I/O（读取/刷新页面）
//   - io_uring 允许批量提交多个 I/O 请求，减少系统调用开销
//   - 支持 Direct I/O，绕过 OS Page Cache（数据库有自己的 Buffer Pool）
//

// I/O 完成回调类型
using IoCallback = std::function<void(int result)>;

// I/O 请求类型
enum class IoOpType : uint8_t {
    READ = 0,
    WRITE,
    FSYNC,
};

// I/O 请求描述
struct IoRequest {
    IoOpType op;
    int fd;            // 文件描述符
    void* buf;         // 数据缓冲区
    size_t len;        // 数据长度
    off_t offset;      // 文件偏移
    IoCallback callback;  // 完成回调
};

class IoUringEngine {
public:
    explicit IoUringEngine(unsigned queue_depth = IO_URING_QUEUE_DEPTH);
    ~IoUringEngine();

    // 禁止拷贝
    IoUringEngine(const IoUringEngine&) = delete;
    IoUringEngine& operator=(const IoUringEngine&) = delete;

    // ============================================================
    // TODO: 你来实现 - 提交I/O请求
    // ============================================================
    // 将一个I/O请求放入 Submission Queue
    //
    // 实现步骤（需要 #include <liburing.h>）：
    //   1. 获取一个 SQE: io_uring_sqe* sqe = io_uring_get_sqe(&ring_);
    //   2. 根据请求类型设置 SQE:
    //      - READ:  io_uring_prep_read(sqe, req.fd, req.buf, req.len, req.offset);
    //      - WRITE: io_uring_prep_write(sqe, req.fd, req.buf, req.len, req.offset);
    //      - FSYNC: io_uring_prep_fsync(sqe, req.fd, 0);
    //   3. 设置用户数据（用于关联回调）:
    //      io_uring_sqe_set_data(sqe, new IoRequest(req));
    //   4. 提交: io_uring_submit(&ring_);
    //
    void Submit(const IoRequest& request);

    // ============================================================
    // TODO: 你来实现 - 批量提交
    // ============================================================
    // 将多个I/O请求一次性放入SQ，然后统一提交
    // 这是 io_uring 性能优势的关键：减少 submit 系统调用次数
    //
    void SubmitBatch(const std::vector<IoRequest>& requests);

    // ============================================================
    // TODO: 你来实现 - 等待完成
    // ============================================================
    // 等待并处理已完成的I/O请求
    //
    // 实现步骤：
    //   1. io_uring_cqe* cqe;
    //   2. io_uring_wait_cqe(&ring_, &cqe);  // 阻塞等待
    //   3. 获取关联的请求: auto* req = (IoRequest*)io_uring_cqe_get_data(cqe);
    //   4. 调用回调: req->callback(cqe->res);
    //   5. 标记 CQE 为已消费: io_uring_cqe_seen(&ring_, cqe);
    //   6. delete req;
    //
    // 变体: PollCompletions() 非阻塞地处理所有已完成的请求
    //
    void WaitCompletion();

    // 非阻塞轮询已完成的请求
    int PollCompletions();

private:
    // struct io_uring ring_;  // io_uring 实例
    // 需要 #include <liburing.h> 才能使用
    // 这里先用 void* 占位，你实现时替换为 struct io_uring
    void* ring_ptr_{nullptr};
    unsigned queue_depth_;
    bool initialized_{false};
};

} // namespace minidb
