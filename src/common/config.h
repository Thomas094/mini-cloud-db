#pragma once

#include <cstddef>

namespace minidb {

// ============================================================
// 全局配置常量
// 面试知识点：PostgreSQL默认页大小8KB，MySQL InnoDB默认16KB
// ============================================================

// 页面大小：8KB，与PostgreSQL保持一致
constexpr size_t PAGE_SIZE = 8192;

// Buffer Pool 默认帧数（可容纳的页面数）
// 生产环境中 PostgreSQL 的 shared_buffers 通常设为内存的 25%
constexpr size_t BUFFER_POOL_SIZE = 1024;

// WAL 日志缓冲区大小
constexpr size_t WAL_BUFFER_SIZE = 16 * PAGE_SIZE;  // 128KB

// B+Tree 的阶数（内部节点最大子节点数）
constexpr int BPLUS_TREE_ORDER = 128;

// B+Tree 叶子节点最大记录数
constexpr int BPLUS_TREE_LEAF_MAX_SIZE = 127;

// io_uring 提交队列深度
constexpr unsigned IO_URING_QUEUE_DEPTH = 256;

// RPC 服务默认端口
constexpr int DEFAULT_RPC_PORT = 9527;

// 多租户默认最大内存配额（字节）
constexpr size_t DEFAULT_TENANT_MEMORY_QUOTA = 256 * 1024 * 1024;  // 256MB

// 冷热数据分层：热数据访问间隔阈值（秒）
constexpr int HOT_DATA_ACCESS_INTERVAL_SEC = 300;     // 5分钟内访问过 = 热数据
constexpr int WARM_DATA_ACCESS_INTERVAL_SEC = 3600;   // 1小时内 = 温数据

} // namespace minidb
