#pragma once

#include "common/types.h"
#include "common/config.h"
#include "storage/page.h"
#include "recovery/log_record.h"

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include <queue>
#include <thread>
#include <condition_variable>

namespace minidb {

// ============================================================
// PageServer - 存储计算分离架构中的存储节点
// ============================================================
//
// 【面试核心知识点 - 存储计算分离】
//
// 传统架构：计算和存储耦合在同一台机器上
//   优点：I/O 延迟低
//   缺点：扩展不灵活（要扩计算就得扩存储，反之亦然）
//
// 存储计算分离架构：
//   计算节点（Compute）：执行 SQL，管理 Buffer Pool，无状态
//   存储节点（Storage）：管理数据页和 WAL，有状态
//
// ╔══════════════════════════════════════════════════════════╗
// ║                     Neon 架构                           ║
// ║                                                         ║
// ║  Compute (PG)  ──WAL──>  Safekeeper (共识)             ║
// ║      │                        │                         ║
// ║      │                        ▼                         ║
// ║      │<──Page──  Pageserver (存储)                      ║
// ║                        │                                ║
// ║                        ▼                                ║
// ║                  Object Storage (S3)                    ║
// ╚══════════════════════════════════════════════════════════╝
//
// ╔══════════════════════════════════════════════════════════╗
// ║                     Aurora 架构                          ║
// ║                                                         ║
// ║  Compute (Writer) ──WAL──> Storage (6副本, Quorum 4/6)  ║
// ║      │                          │                       ║
// ║      │                          │ 异步回放WAL生成页面    ║
// ║      │                          ▼                       ║
// ║  Compute (Reader) <──Page── Storage                     ║
// ╚══════════════════════════════════════════════════════════╝
//
// Aurora 的 "Log is the Database" 革命性设计：
//   - 写入路径：计算节点只发送 WAL 给存储层（网络传输量减少 ~46x）
//   - 读取路径：存储层回放 WAL 重建页面，按需返回给计算节点
//   - 优势：跨AZ 6副本，但写入延迟很低（不需要传输完整页面）
//

// 存储请求类型
enum class StorageRequestType : uint8_t {
    GET_PAGE = 0,     // 获取指定 LSN 的页面
    PUT_WAL,          // 存储 WAL 日志
    GET_WAL,          // 获取 WAL 日志（用于备份/恢复）
};

// 存储请求
struct StorageRequest {
    StorageRequestType type;
    page_id_t page_id{INVALID_PAGE_ID};
    lsn_t lsn{INVALID_LSN};           // 请求该LSN版本的页面
    tenant_id_t tenant_id{0};

    // WAL 数据（PUT_WAL 时使用）
    std::vector<char> wal_data;

    // 响应数据（服务端填充）
    char page_data[PAGE_SIZE]{};
    bool success{false};
};

class PageServer {
public:
    explicit PageServer(const std::string& data_dir);
    ~PageServer();

    // ============================================================
    // TODO: 你来实现 - 处理获取页面请求
    // ============================================================
    // 这是 Neon Pageserver 的核心功能：根据 LSN 返回页面
    //
    // 实现步骤（模拟 Neon 的 get_page_at_lsn）：
    //   1. 查找该 page_id 的基础页面（base image）
    //   2. 收集从 base_lsn 到 request_lsn 之间的所有 WAL 记录
    //   3. 在基础页面上回放这些 WAL 记录（redo）
    //   4. 返回重建后的页面
    //
    // 【Neon 的关键优化】
    //   - 定期创建页面的 "image layer"（基础快照）
    //   - 只需回放快照之后的 WAL（减少 redo 量）
    //   - 历史版本可以保留（支持 Point-in-Time Recovery）
    //
    // 【Aurora 的不同之处】
    //   Aurora 不保留历史版本，存储节点在后台持续回放 WAL。
    //   读取时直接返回最新页面，不需要实时 redo。
    //   这使得 Aurora 的读延迟更低，但不支持任意时间点恢复。
    //
    void HandleGetPage(StorageRequest& request);

    // ============================================================
    // TODO: 你来实现 - 处理存储WAL请求
    // ============================================================
    // 接收计算节点发来的 WAL 日志并持久化
    //
    // 实现步骤：
    //   1. 将 WAL 数据追加到对应 tenant 的日志文件
    //   2. fsync 保证持久性
    //   3. 解析 WAL 记录，更新内存中的 page→WAL 索引
    //   4. 返回确认
    //
    // 【Neon 的 Safekeeper】
    //   Neon 有专门的 Safekeeper 组件来接收和持久化 WAL：
    //   - 3个 Safekeeper 形成 Paxos 共识
    //   - 2/3 写入成功即可返回
    //   - Pageserver 从 Safekeeper 异步消费 WAL
    //
    void HandlePutWAL(StorageRequest& request);

    // ============================================================
    // TODO: 你来实现 - 后台WAL回放线程
    // ============================================================
    // 持续消费 WAL 日志，更新页面状态
    //
    // 类似 Aurora 存储节点的后台 redo 过程。
    //
    void BackgroundRedoLoop();

    // 启动/停止后台线程
    void Start();
    void Stop();

private:
    std::string data_dir_;

    // page_id → 基础页面数据
    std::unordered_map<page_id_t, std::vector<char>> base_images_;

    // page_id → WAL 记录列表（按 LSN 排序）
    std::unordered_map<page_id_t, std::vector<LogRecord>> page_wal_index_;

    // 待回放的 WAL 队列
    std::queue<LogRecord> wal_replay_queue_;

    std::mutex page_mutex_;
    std::mutex wal_mutex_;
    std::condition_variable wal_cv_;

    std::thread redo_thread_;
    bool running_{false};
};

// ============================================================
// RPCClient - 计算节点侧的 RPC 客户端（简化版）
// ============================================================
//
// 在真实系统中，这里会使用 gRPC 或自定义 TCP 协议。
// 我们简化为本地函数调用，重点理解交互模式。
//
class RPCClient {
public:
    explicit RPCClient(PageServer* page_server);
    ~RPCClient() = default;

    // ============================================================
    // TODO: 你来实现 - 远程获取页面
    // ============================================================
    // 计算节点通过 RPC 向存储节点请求页面
    //
    // 在 Neon 中：
    //   Compute (PG) 发送 GetPage@LSN 请求
    //   → Pageserver 回放 WAL 到目标 LSN
    //   → 返回重建的页面
    //
    // 在 Aurora 中：
    //   Compute 发送页面请求
    //   → Storage 返回最新页面（已经在后台完成了redo）
    //
    bool FetchPageRemote(page_id_t page_id, lsn_t lsn, char* page_data);

    // ============================================================
    // TODO: 你来实现 - 远程发送WAL
    // ============================================================
    // 计算节点将 WAL 发送到存储节点
    //
    bool SendWAL(const std::vector<char>& wal_data, tenant_id_t tenant_id);

private:
    PageServer* page_server_;  // 简化：直接持有指针，实际应该是网络连接
};

} // namespace minidb
