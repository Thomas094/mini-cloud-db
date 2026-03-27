#include "distributed/distributed_txn.h"
#include <stdexcept>

namespace minidb {

// ============================================================
// Participant 实现
// ============================================================

Participant::Participant(participant_id_t id) : id_(id) {}

// ============================================================
// TODO: 你来实现
// ============================================================
bool Participant::Prepare(txn_id_t txn_id) {
    // std::lock_guard<std::mutex> lock(participant_mutex_);
    //
    // 简化实现（总是同意）：
    //   txn_states_[txn_id] = ParticipantState::PREPARED;
    //   return true;
    //
    // 真实实现需要：
    //   1. 检查数据完整性约束
    //   2. 检查锁冲突
    //   3. 预写 WAL 并 fsync
    //   4. 如果任何检查失败 → return false

    throw std::runtime_error("Participant::Prepare: 尚未实现");
}

// ============================================================
// TODO: 你来实现
// ============================================================
void Participant::Commit(txn_id_t txn_id) {
    // std::lock_guard<std::mutex> lock(participant_mutex_);
    //
    // txn_states_[txn_id] = ParticipantState::COMMITTED;
    // // 释放锁、使修改可见

    throw std::runtime_error("Participant::Commit: 尚未实现");
}

// ============================================================
// TODO: 你来实现
// ============================================================
void Participant::Abort(txn_id_t txn_id) {
    // std::lock_guard<std::mutex> lock(participant_mutex_);
    //
    // txn_states_[txn_id] = ParticipantState::ABORTED;
    // // 回滚本地修改

    throw std::runtime_error("Participant::Abort: 尚未实现");
}

ParticipantState Participant::GetState(txn_id_t txn_id) const {
    auto it = txn_states_.find(txn_id);
    return it != txn_states_.end() ? it->second : ParticipantState::INIT;
}

// ============================================================
// Coordinator 实现
// ============================================================

void Coordinator::AddParticipant(std::shared_ptr<Participant> participant) {
    std::lock_guard<std::mutex> lock(coord_mutex_);
    participants_.push_back(std::move(participant));
}

// ============================================================
// TODO: 你来实现
// ============================================================
bool Coordinator::ExecuteTransaction(txn_id_t txn_id) {
    // std::lock_guard<std::mutex> lock(coord_mutex_);
    //
    // // ===== Phase 1: Prepare =====
    // txn_states_[txn_id] = DistributedTxnState::PREPARING;
    //
    // bool all_prepared = true;
    // for (auto& participant : participants_) {
    //     if (!participant->Prepare(txn_id)) {
    //         all_prepared = false;
    //         break;
    //     }
    // }
    //
    // // ===== Phase 2: Commit or Abort =====
    // if (all_prepared) {
    //     // 写入 COMMIT 决定到 WAL（这是 Point of No Return）
    //     txn_states_[txn_id] = DistributedTxnState::COMMITTING;
    //
    //     for (auto& participant : participants_) {
    //         participant->Commit(txn_id);
    //     }
    //     txn_states_[txn_id] = DistributedTxnState::COMMITTED;
    //     return true;
    // } else {
    //     txn_states_[txn_id] = DistributedTxnState::ABORTING;
    //
    //     for (auto& participant : participants_) {
    //         participant->Abort(txn_id);
    //     }
    //     txn_states_[txn_id] = DistributedTxnState::ABORTED;
    //     return false;
    // }
    //
    // 【进阶挑战】
    //   1. 加入超时机制：Participant 超时未响应 → 视为 NO
    //   2. 加入重试机制：Commit 消息发送失败 → 重试
    //   3. 实现 Presumed Abort 优化：
    //      - 如果不确定事务状态 → 默认 Abort
    //      - 减少了 Abort 时写 WAL 的开销

    throw std::runtime_error("Coordinator::ExecuteTransaction: 尚未实现");
}

DistributedTxnState Coordinator::GetTxnState(txn_id_t txn_id) const {
    auto it = txn_states_.find(txn_id);
    return it != txn_states_.end() ? it->second : DistributedTxnState::INIT;
}

} // namespace minidb
