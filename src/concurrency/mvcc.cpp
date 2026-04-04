#include "concurrency/mvcc.h"
#include "buffer/buffer_pool_manager.h"
#include "common/types.h"
#include "concurrency/commit_log.h"
#include <stdexcept>
#include <algorithm>
#include <set>

namespace minidb {

// ============================================================
// 构造函数（纯内存模式，无 CLOG）
// ============================================================
MVCCManager::MVCCManager() = default;

// ============================================================
// 构造函数（持久化模式，带 CLOG）
// ============================================================
MVCCManager::MVCCManager(const std::string& clog_file, BufferPoolManager* bpm)
    : clog_(std::make_unique<CommitLog>(clog_file)), bpm_(bpm) {
}

// ============================================================
// TODO: 你来实现
// ============================================================
bool Snapshot::IsVisible(txn_id_t xid) const {
    // 优化检查顺序：先快速路径，再查 hash set
    if (xid < xmin) return true;       // 在快照前已提交（快速路径）
    if (xid >= xmax) return false;     // 在快照后开始
    if (active_txns.count(xid) > 0) return false;  // 快照时还活跃
    return true;                        // 在快照前已提交
}

// ============================================================
// TODO: 你来实现
// ============================================================
txn_id_t MVCCManager::BeginTransaction() {
    std::lock_guard<std::mutex> lock(mvcc_mutex_);
    //
    // 1. 分配事务ID
       txn_id_t txn_id = next_txn_id_.fetch_add(1);
    //
    // 2. 记录事务状态
       txn_states_[txn_id] = TxnState::RUNNING;
       active_txns_.insert(txn_id);
    //
    // 3. 创建快照
       Snapshot snapshot;
       snapshot.xmax = next_txn_id_.load();
       snapshot.active_txns.insert(active_txns_.begin(), active_txns_.end()); // 拷贝当前活跃事务
       // active_txns_ 是 std::set（有序），begin() 即为最小值
       if (!active_txns_.empty()) {
         snapshot.xmin = *active_txns_.begin();
       } else {
         snapshot.xmin = txn_id;
       }
       txn_snapshots_[txn_id] = std::move(snapshot);
    // 4. 如果有 CLOG，设置事务状态为 IN_PROGRESS
       if (clog_) {
           clog_->SetTransactionStatus(txn_id, CLogTxnStatus::IN_PROGRESS);
       }
    //
    return txn_id;

}

// ============================================================
// TODO: 你来实现
// ============================================================
void MVCCManager::CommitTransaction(txn_id_t txn_id) {
    {
      std::lock_guard<std::mutex> lock(mvcc_mutex_);
      txn_states_[txn_id] = TxnState::COMMITTED;
      active_txns_.erase(txn_id);
      txn_snapshots_.erase(txn_id);  // 清理快照，防止内存泄漏
    }
    if (clog_) {
      clog_->SetTransactionStatus(txn_id, CLogTxnStatus::COMMITTED);
      clog_->Flush(); // 确保持久化
    }
}

// ============================================================
// TODO: 你来实现
// ============================================================
void MVCCManager::AbortTransaction(txn_id_t txn_id) {
    {
      std::lock_guard<std::mutex> lock(mvcc_mutex_);
      txn_states_[txn_id] = TxnState::ABORTED;
      active_txns_.erase(txn_id);
      txn_snapshots_.erase(txn_id);  // 清理快照，防止内存泄漏
    }
    if (clog_) {
      clog_->SetTransactionStatus(txn_id, CLogTxnStatus::ABORTED);
      clog_->Flush();
    }
}

// ============================================================
// TODO: 你来实现 — 内存版本链的可见性判断
// ============================================================
bool MVCCManager::IsVersionVisible(const TupleVersion& version, txn_id_t reader_txn_id) const {
    std::lock_guard<std::mutex> lock(mvcc_mutex_);

    // 使用无锁版本，避免递归加锁死锁
    const Snapshot* snapshot = GetSnapshotUnlocked(reader_txn_id);
    if (!snapshot) return false;

    // Step 1: 检查创建者 (xmin)
    if (version.xmin == reader_txn_id) {
        // 自己创建的版本
        if (version.xmax == INVALID_TXN_ID) return true;   // 没被删除
        if (version.xmax == reader_txn_id) return false;    // 自己也删了
        // 别人标记删除，检查删除者是否已提交
        TxnState xmax_state = GetTransactionStateUnlocked(version.xmax);
        if (xmax_state == TxnState::COMMITTED) return false; // 删除已提交
        return true;  // 删除未提交，仍可见
    }

    // 检查 xmin 事务状态（使用无锁版本）
    TxnState xmin_state = GetTransactionStateUnlocked(version.xmin);
    if (xmin_state != TxnState::COMMITTED) {
        return false; // 创建者未提交
    }
    if (!snapshot->IsVisible(version.xmin)) {
        return false;  // 创建者对快照不可见
    }

    // Step 2: 检查删除者 (xmax)
    if (version.xmax == INVALID_TXN_ID) {
        return true;   // 没被删除 → 可见
    }
    if (version.xmax == reader_txn_id) {
        return false;  // 自己删除的 → 不可见
    }
    TxnState xmax_state = GetTransactionStateUnlocked(version.xmax);
    if (xmax_state == TxnState::ABORTED) {
        return true;   // 删除被回滚 → 仍可见
    }
    if (xmax_state != TxnState::COMMITTED) {
        return true;   // 删除者未提交 → 仍可见
    }
    if (!snapshot->IsVisible(version.xmax)) {
        return true;   // 删除者对快照不可见 → 仍可见
    }
    return false;      // 已被删除 → 不可见
}

// ============================================================
// TODO: 你来实现 — 页面上元组的可见性判断（磁盘版本）
// ============================================================
//
// 这是持久化版本的可见性判断，结合 TupleHeader + CLOG + Snapshot。
//
// 实现步骤：
//   1. 从页面读取 TupleHeader
//   2. 获取读取者的快照
//   3. 检查 xmin 的 hint bits：
//      a. 如果 XMIN_COMMITTED → xmin 已提交（跳过 CLOG 查询）
//      b. 如果 XMIN_ABORTED → 直接返回 false
//      c. 否则 → 查 CLOG（或内存 txn_states_）
//         - 查到后设置 hint bits（良性脏页优化）
//   4. 如果 xmin 已提交，检查对快照的可见性
//   5. 检查 xmax（逻辑同上）
//   6. 返回最终可见性结果
//
// 【PostgreSQL 对应代码】
//   HeapTupleSatisfiesMVCC() in src/backend/utils/time/tqual.c
//
bool MVCCManager::IsTupleVisible(Page* page, uint16_t slot_id, txn_id_t reader_txn_id) const {
    // 1. 读取 TupleHeader
    auto header_opt = TuplePage::GetTupleHeader(page, slot_id);
    if (!header_opt) return false;
    TupleHeader header = *header_opt;

    // 2. 获取快照（加锁保护）
    std::lock_guard<std::mutex> lock(mvcc_mutex_);
    const Snapshot* snapshot = GetSnapshotUnlocked(reader_txn_id);
    if (!snapshot) return false;

    // 3. 检查 xmin
    bool xmin_visible = false;
    bool xmin_is_self = (header.xmin == reader_txn_id);

    if (xmin_is_self) {
        // 自己创建的元组，对自己可见（跳过快照检查）
        xmin_visible = true;
    } else if (header.IsXminCommitted()) {
        xmin_visible = true;  // hint bit 命中，跳过 CLOG
    } else if (header.IsXminAborted()) {
        return false;           // hint bit 命中，直接不可见
    } else {
        // 查询事务状态（使用无锁版本，因为已持有 mvcc_mutex_）
        TxnState state = GetTransactionStateUnlocked(header.xmin);
        if (state == TxnState::COMMITTED) {
            xmin_visible = true;
            // 设置 hint bit（良性脏页）
            TuplePage::UpdateHintBits(const_cast<Page*>(page), slot_id, TUPLE_XMIN_COMMITTED);
        } else if (state == TxnState::ABORTED) {
            TuplePage::UpdateHintBits(const_cast<Page*>(page), slot_id, TUPLE_XMIN_ABORTED);
            return false;
        } else {
            // IN_PROGRESS 且不是自己的事务 → 不可见
            return false;
        }
    }

    if (!xmin_visible) return false;

    // 非自己事务时，检查快照可见性
    if (!xmin_is_self && !snapshot->IsVisible(header.xmin)) {
        return false;
    }

    // 4. 检查 xmax（删除者）
    if (header.xmax == INVALID_TXN_ID) {
        return true;  // 没被删除
    }

    // 自己删除的 → 不可见
    if (header.xmax == reader_txn_id) {
        return false;
    }

    if (header.IsXmaxCommitted()) {
        // 删除已提交，检查快照可见性
        if (snapshot->IsVisible(header.xmax)) {
            return false;  // 删除对快照可见 → 不可见
        }
        return true;  // 删除对快照不可见 → 仍可见
    } else if (header.IsXmaxAborted()) {
        // 删除被回滚 → 仍可见
        return true;
    } else {
        TxnState xmax_state = GetTransactionStateUnlocked(header.xmax);
        if (xmax_state == TxnState::COMMITTED) {
            TuplePage::UpdateHintBits(const_cast<Page*>(page), slot_id, TUPLE_XMAX_COMMITTED);
            if (snapshot->IsVisible(header.xmax)) {
                return false;
            }
            return true;
        } else if (xmax_state == TxnState::ABORTED) {
            TuplePage::UpdateHintBits(const_cast<Page*>(page), slot_id, TUPLE_XMAX_ABORTED);
            return true;
        } else {
            // 删除进行中 → 仍可见
            return true;
        }
    }
}

// ============================================================
// 获取事务状态（优先查内存，fallback 到 CLOG）
// ============================================================

TxnState MVCCManager::GetTransactionState(txn_id_t txn_id) const {
    std::lock_guard<std::mutex> lock(mvcc_mutex_);
    return GetTransactionStateUnlocked(txn_id);
}

// 内部无锁版本（调用者已持有 mvcc_mutex_）
TxnState MVCCManager::GetTransactionStateUnlocked(txn_id_t txn_id) const {
    // 1. 先查内存缓存（快速路径）
    auto it = txn_states_.find(txn_id);
    if (it != txn_states_.end()) {
        return it->second;
    }

    // 2. 如果有 CLOG，查询持久化状态
    if (clog_) {
        CLogTxnStatus clog_status = clog_->GetTransactionStatus(txn_id);
        switch (clog_status) {
            case CLogTxnStatus::COMMITTED:
                return TxnState::COMMITTED;
            case CLogTxnStatus::ABORTED:
                return TxnState::ABORTED;
            case CLogTxnStatus::IN_PROGRESS:
                return TxnState::RUNNING;
            default:
                return TxnState::INVALID;
        }
    }

    // 3. 都没有
    return TxnState::INVALID;
}

const Snapshot* MVCCManager::GetSnapshot(txn_id_t txn_id) const {
    std::lock_guard<std::mutex> lock(mvcc_mutex_);
    return GetSnapshotUnlocked(txn_id);
}

// 内部无锁版本（调用者已持有 mvcc_mutex_）
const Snapshot* MVCCManager::GetSnapshotUnlocked(txn_id_t txn_id) const {
    auto it = txn_snapshots_.find(txn_id);
    if (it != txn_snapshots_.end()) {
        return &it->second;
    }
    return nullptr;
}

} // namespace minidb
