#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <limits>

namespace minidb {

// ============================================================
// 基础类型定义
// 这些类型贯穿整个项目，是数据库内核的"语言"
// ============================================================

// 页面ID：数据库中每个数据页的唯一标识
using page_id_t = int32_t;

// 帧ID：Buffer Pool中槽位的编号
using frame_id_t = int32_t;

// 事务ID：每个事务的唯一标识，单调递增
// PostgreSQL 中叫 TransactionId (xid)，32位，会有回卷问题
using txn_id_t = uint64_t;

// LSN (Log Sequence Number)：WAL日志的位置标识
// 这是数据库恢复的核心概念，表示日志流中的字节偏移
using lsn_t = uint64_t;

// 时间戳类型，用于MVCC快照
using timestamp_t = uint64_t;

// 租户ID
using tenant_id_t = uint32_t;

// 无效值常量
constexpr page_id_t INVALID_PAGE_ID = -1;
constexpr frame_id_t INVALID_FRAME_ID = -1;
constexpr txn_id_t INVALID_TXN_ID = 0;
constexpr lsn_t INVALID_LSN = 0;

// 事务状态
enum class TxnState : uint8_t {
    INVALID = 0,
    RUNNING,
    COMMITTED,
    ABORTED,
};

// 隔离级别 —— 面试高频题：各级别能防止什么异常？
enum class IsolationLevel : uint8_t {
    READ_UNCOMMITTED = 0,  // 啥也不防
    READ_COMMITTED,        // 防脏读（PostgreSQL默认）
    REPEATABLE_READ,       // 防脏读+不可重复读（MySQL默认）
    SERIALIZABLE,          // 防所有异常
};

// 锁模式
enum class LockMode : uint8_t {
    SHARED = 0,    // 读锁（S锁）
    EXCLUSIVE,     // 写锁（X锁）
};

// 数据存储温度（冷热分层用）
enum class DataTemperature : uint8_t {
    HOT = 0,   // 热数据：高频访问，存SSD
    WARM,      // 温数据：中频访问
    COLD,      // 冷数据：低频访问，可迁移到廉价存储
};

} // namespace minidb
