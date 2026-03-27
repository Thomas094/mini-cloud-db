#pragma once

#include "common/types.h"
#include <atomic>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace minidb {

// ============================================================
// Tuple 版本头（MVCC 的核心数据结构）
// ============================================================
//
// 【面试核心知识点 - MVCC 多版本并发控制】
//
// MVCC 是现代数据库并发控制的基石（PG、MySQL InnoDB 都用）。
// 核心思想：每次修改不覆盖旧数据，而是创建新版本。
// 读操作看到的是"快照"，不需要加锁，读写互不阻塞。
//
// PostgreSQL 的实现方式（Tuple Header 中的关键字段）：
//   - xmin: 创建（INSERT）这个元组的事务ID
//   - xmax: 删除（DELETE）或更新这个元组的事务ID（0表示有效）
//   - t_ctid: 指向新版本的 (page, offset)，形成版本链
//
// 可见性判断规则（简化版）：
//   一个元组对事务T可见，当且仅当：
//     1. xmin 已提交 且 xmin < T的快照时间戳
//     2. xmax 无效（0）或 xmax 未提交 或 xmax > T的快照时间戳
//
// 与 MySQL InnoDB 的对比：
//   - PG: 旧版本存在原表中（需要 VACUUM 清理）
//   - InnoDB: 旧版本存在 Undo Log 中（不需要 VACUUM，但 Undo 可能膨胀）
//

struct TupleVersion {
    txn_id_t xmin{INVALID_TXN_ID};    // 创建此版本的事务ID
    txn_id_t xmax{INVALID_TXN_ID};    // 删除此版本的事务ID（0=有效）
    timestamp_t create_ts{0};           // 创建时间戳（快照读用）
    timestamp_t delete_ts{0};           // 删除时间戳

    // 版本链指针（指向旧版本）
    // PG 的版本链是"新→旧"（HOT链是"旧→新"）
    // 我们这里用"新→旧"，即当前版本指向前一个版本
    TupleVersion* prev_version{nullptr};

    // 元组数据（简化版，用固定大小）
    static constexpr size_t MAX_TUPLE_SIZE = 256;
    char data[MAX_TUPLE_SIZE]{};
    uint16_t data_len{0};

    // 记录所在页面（用于定位）
    page_id_t page_id{INVALID_PAGE_ID};
    uint16_t slot_offset{0};
};

// ============================================================
// Snapshot - 事务快照
// ============================================================
//
// 快照定义了一个事务"能看到什么"。
//
// PostgreSQL 快照包含：
//   - xmin: 最小活跃事务ID（所有小于此值的事务都已提交）
//   - xmax: 下一个将分配的事务ID
//   - xip[]: 当前活跃的事务ID列表
//
// 判断一个事务 xid 是否对快照可见：
//   - xid < snapshot.xmin → 可见（已提交）
//   - xid >= snapshot.xmax → 不可见（还没开始）
//   - xid in snapshot.xip → 不可见（还在执行中）
//   - 其他情况 → 可见
//
struct Snapshot {
    txn_id_t xmin{INVALID_TXN_ID};  // 最小活跃事务
    txn_id_t xmax{INVALID_TXN_ID};  // 下一个事务ID
    std::unordered_set<txn_id_t> active_txns;  // 快照时的活跃事务

    // ============================================================
    // TODO: 你来实现 - 判断事务是否对此快照可见
    // ============================================================
    //
    // 实现逻辑：
    //   if (xid < xmin) return true;      // 在快照前已提交
    //   if (xid >= xmax) return false;    // 在快照后开始
    //   if (active_txns.count(xid)) return false;  // 快照时还活跃
    //   return true;                       // 在快照前已提交
    //
    bool IsVisible(txn_id_t xid) const;
};

// ============================================================
// MVCCManager - MVCC 管理器
// ============================================================
class MVCCManager {
public:
    MVCCManager();
    ~MVCCManager() = default;

    // ============================================================
    // TODO: 你来实现 - 开始新事务
    // ============================================================
    // 分配新的事务ID，创建快照
    //
    // 实现步骤：
    //   1. 分配新事务ID: next_txn_id_++
    //   2. 将新事务加入 active_txns_ 集合
    //   3. 创建快照：
    //      - snapshot.xmin = active_txns_ 中最小的ID
    //      - snapshot.xmax = next_txn_id_（下一个将分配的ID）
    //      - snapshot.active_txns = active_txns_ 的拷贝
    //   4. 存储快照（用于后续可见性判断）
    //   5. 返回事务ID
    //
    txn_id_t BeginTransaction();

    // ============================================================
    // TODO: 你来实现 - 提交事务
    // ============================================================
    // 标记事务为已提交，从活跃列表移除
    //
    void CommitTransaction(txn_id_t txn_id);

    // ============================================================
    // TODO: 你来实现 - 回滚事务
    // ============================================================
    // 标记事务为已回滚，清理该事务创建的所有版本
    //
    void AbortTransaction(txn_id_t txn_id);

    // ============================================================
    // TODO: 你来实现 - 判断元组版本对事务是否可见
    // ============================================================
    // 这是 MVCC 的核心！面试必问！
    //
    // 实现逻辑（PG风格）：
    //   1. 检查 version.xmin:
    //      - 如果 xmin 的事务已回滚 → 不可见
    //      - 如果 xmin 对当前快照不可见 → 不可见
    //   2. 检查 version.xmax:
    //      - 如果 xmax == INVALID_TXN_ID → 可见（没被删除）
    //      - 如果 xmax 的事务已回滚 → 可见（删除被回滚了）
    //      - 如果 xmax 对当前快照不可见 → 可见（删除还没"生效"）
    //      - 否则 → 不可见（已被删除）
    //
    bool IsVersionVisible(const TupleVersion& version, txn_id_t reader_txn_id) const;

    // 获取事务的快照
    const Snapshot* GetSnapshot(txn_id_t txn_id) const;

private:
    std::atomic<txn_id_t> next_txn_id_{1};

    // 活跃事务集合
    std::unordered_set<txn_id_t> active_txns_;

    // 事务ID → 快照
    std::unordered_map<txn_id_t, Snapshot> txn_snapshots_;

    // 事务ID → 状态
    std::unordered_map<txn_id_t, TxnState> txn_states_;

    mutable std::mutex mvcc_mutex_;
};

} // namespace minidb
