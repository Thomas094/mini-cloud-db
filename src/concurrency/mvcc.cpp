#include "concurrency/mvcc.h"
#include <stdexcept>
#include <algorithm>

namespace minidb {

MVCCManager::MVCCManager() = default;

// ============================================================
// TODO: 你来实现
// ============================================================
bool Snapshot::IsVisible(txn_id_t xid) const {
    // 实现提示：
    //   if (xid < xmin) return true;
    //   if (xid >= xmax) return false;
    //   if (active_txns.count(xid) > 0) return false;
    //   return true;
    //
    // 【深入理解】
    //   这就是 PostgreSQL 的 TransactionIdIsInProgress() +
    //   TransactionIdDidCommit() 的简化版。
    //   PG 实际上还要检查 CLOG (Commit Log) 来确认事务状态。

    throw std::runtime_error("Snapshot::IsVisible: 尚未实现");
}

// ============================================================
// TODO: 你来实现
// ============================================================
txn_id_t MVCCManager::BeginTransaction() {
    // std::lock_guard<std::mutex> lock(mvcc_mutex_);
    //
    // 1. 分配事务ID
    //    txn_id_t txn_id = next_txn_id_.fetch_add(1);
    //
    // 2. 记录事务状态
    //    txn_states_[txn_id] = TxnState::RUNNING;
    //    active_txns_.insert(txn_id);
    //
    // 3. 创建快照
    //    Snapshot snapshot;
    //    snapshot.xmax = next_txn_id_.load();
    //    snapshot.active_txns = active_txns_;  // 拷贝当前活跃事务
    //    if (!active_txns_.empty()) {
    //        snapshot.xmin = *std::min_element(active_txns_.begin(), active_txns_.end());
    //    } else {
    //        snapshot.xmin = txn_id;
    //    }
    //    txn_snapshots_[txn_id] = std::move(snapshot);
    //
    // 4. return txn_id;

    throw std::runtime_error("BeginTransaction: 尚未实现");
}

// ============================================================
// TODO: 你来实现
// ============================================================
void MVCCManager::CommitTransaction(txn_id_t txn_id) {
    // std::lock_guard<std::mutex> lock(mvcc_mutex_);
    //
    // 1. txn_states_[txn_id] = TxnState::COMMITTED;
    // 2. active_txns_.erase(txn_id);
    //
    // 【思考】为什么不立即删除快照？
    //   因为其他活跃事务的快照中可能引用了此事务。
    //   快照的清理应该在所有引用它的事务都结束后进行。
    //   PostgreSQL 通过 OldestActiveTransactionId 来判断。

    throw std::runtime_error("CommitTransaction: 尚未实现");
}

// ============================================================
// TODO: 你来实现
// ============================================================
void MVCCManager::AbortTransaction(txn_id_t txn_id) {
    // std::lock_guard<std::mutex> lock(mvcc_mutex_);
    //
    // 1. txn_states_[txn_id] = TxnState::ABORTED;
    // 2. active_txns_.erase(txn_id);
    //
    // 【进阶】在完整实现中，还需要：
    //   - 沿着版本链，将此事务创建的所有版本标记为无效
    //   - 或者依赖 MVCC 可见性判断自动跳过回滚事务的版本

    throw std::runtime_error("AbortTransaction: 尚未实现");
}

// ============================================================
// TODO: 你来实现
// ============================================================
bool MVCCManager::IsVersionVisible(const TupleVersion& version, txn_id_t reader_txn_id) const {
    // std::lock_guard<std::mutex> lock(mvcc_mutex_);
    //
    // const Snapshot* snapshot = GetSnapshot(reader_txn_id);
    // if (!snapshot) return false;
    //
    // // Step 1: 检查创建者 (xmin)
    // if (version.xmin == reader_txn_id) {
    //     // 自己创建的版本
    //     if (version.xmax == INVALID_TXN_ID) return true;   // 没被删除
    //     if (version.xmax == reader_txn_id) return false;    // 自己也删了
    //     return true;  // 别人标记删除但还没提交
    // }
    //
    // // 检查 xmin 事务状态
    // auto it = txn_states_.find(version.xmin);
    // if (it == txn_states_.end() || it->second == TxnState::ABORTED) {
    //     return false;  // 创建者已回滚
    // }
    // if (!snapshot->IsVisible(version.xmin)) {
    //     return false;  // 创建者对快照不可见
    // }
    //
    // // Step 2: 检查删除者 (xmax)
    // if (version.xmax == INVALID_TXN_ID) {
    //     return true;   // 没被删除 → 可见
    // }
    // auto xit = txn_states_.find(version.xmax);
    // if (xit != txn_states_.end() && xit->second == TxnState::ABORTED) {
    //     return true;   // 删除被回滚 → 仍可见
    // }
    // if (!snapshot->IsVisible(version.xmax)) {
    //     return true;   // 删除者对快照不可见 → 仍可见
    // }
    // return false;      // 已被删除 → 不可见

    throw std::runtime_error("IsVersionVisible: 尚未实现");
}

const Snapshot* MVCCManager::GetSnapshot(txn_id_t txn_id) const {
    auto it = txn_snapshots_.find(txn_id);
    if (it != txn_snapshots_.end()) {
        return &it->second;
    }
    return nullptr;
}

} // namespace minidb
