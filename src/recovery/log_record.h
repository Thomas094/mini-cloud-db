#pragma once

#include "common/types.h"
#include <cstdint>
#include <cstring>
#include <string>

namespace minidb {

// ============================================================
// WAL Log Record 格式定义
// ============================================================
//
// 【面试核心知识点 - WAL (Write-Ahead Logging)】
//
// WAL 是数据库ACID中 D（Durability）的核心保证：
//   "先写日志，再写数据"
//
// 核心规则：
//   1. 修改数据页之前，必须先将对应的日志写入WAL
//   2. 事务提交时，必须将WAL刷盘（fsync）
//   3. 数据页可以延迟写回（checkpoint时再刷）
//   4. 崩溃恢复时：重放WAL日志即可恢复数据
//
// PostgreSQL WAL 格式 (XLogRecord):
//   +------------------+
//   | xl_tot_len       |  总长度
//   | xl_xid           |  事务ID
//   | xl_prev          |  上一条记录的LSN
//   | xl_info          |  操作类型
//   | xl_rmid          |  资源管理器ID
//   | xl_crc           |  CRC校验
//   +------------------+
//   | XLogRecordData   |  数据负载
//   +------------------+
//
// Aurora 的颠覆性设计 "Log is the Database":
//   - 计算节点只发送 WAL log 给存储节点
//   - 存储节点通过回放 WAL 重建页面
//   - 消除了传统架构中的"写两次"问题
//

// 日志记录类型
enum class LogRecordType : uint8_t {
    INVALID = 0,
    INSERT,       // 插入元组
    DELETE,       // 删除元组
    UPDATE,       // 更新元组
    TXN_BEGIN,    // 事务开始
    TXN_COMMIT,   // 事务提交
    TXN_ABORT,    // 事务回滚
    CHECKPOINT,   // 检查点
};

// ============================================================
// LogRecord - 日志记录结构
// ============================================================
struct LogRecord {
    // --- Header ---
    uint32_t size_{0};              // 整条记录的大小（含header）
    lsn_t lsn_{INVALID_LSN};       // 本条记录的LSN
    lsn_t prev_lsn_{INVALID_LSN};  // 同一事务的上一条LSN（用于undo链）
    txn_id_t txn_id_{INVALID_TXN_ID};  // 所属事务ID
    LogRecordType type_{LogRecordType::INVALID};

    // --- Payload（根据类型不同，有不同含义）---
    page_id_t page_id_{INVALID_PAGE_ID};  // 被修改的页面
    uint16_t offset_{0};                   // 页面内偏移
    uint16_t old_data_len_{0};             // 旧数据长度（用于undo）
    uint16_t new_data_len_{0};             // 新数据长度（用于redo）

    // 可变长度数据存储（简化设计，实际会用更紧凑的格式）
    static constexpr size_t MAX_PAYLOAD_SIZE = 4096;
    char old_data_[MAX_PAYLOAD_SIZE]{};    // undo 数据
    char new_data_[MAX_PAYLOAD_SIZE]{};    // redo 数据

    LogRecord() = default;

    // 构造事务控制日志（BEGIN/COMMIT/ABORT）
    static LogRecord MakeTxnRecord(LogRecordType type, txn_id_t txn_id, lsn_t prev_lsn) {
        LogRecord rec;
        rec.type_ = type;
        rec.txn_id_ = txn_id;
        rec.prev_lsn_ = prev_lsn;
        rec.size_ = sizeof(LogRecord);
        return rec;
    }

    // 构造数据修改日志（INSERT/UPDATE/DELETE）
    static LogRecord MakeDataRecord(LogRecordType type, txn_id_t txn_id, lsn_t prev_lsn,
                                     page_id_t page_id, uint16_t offset,
                                     const char* old_data, uint16_t old_len,
                                     const char* new_data, uint16_t new_len) {
        LogRecord rec;
        rec.type_ = type;
        rec.txn_id_ = txn_id;
        rec.prev_lsn_ = prev_lsn;
        rec.page_id_ = page_id;
        rec.offset_ = offset;
        rec.old_data_len_ = old_len;
        rec.new_data_len_ = new_len;
        if (old_data && old_len > 0) {
            std::memcpy(rec.old_data_, old_data, old_len);
        }
        if (new_data && new_len > 0) {
            std::memcpy(rec.new_data_, new_data, new_len);
        }
        rec.size_ = sizeof(LogRecord);
        return rec;
    }
};

} // namespace minidb
