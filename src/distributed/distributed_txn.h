#pragma once

#include "common/types.h"
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <condition_variable>

namespace minidb {

// ============================================================
// 分布式事务 - Two-Phase Commit (2PC)
// ============================================================
//
// 【面试核心知识点 - 分布式事务】
//
// 分布式事务是分布式数据库最难的部分之一。
// 核心问题：如何保证多个节点上的操作要么全部提交，要么全部回滚？
//
// ╔══════════════════════════════════════════════════════╗
// ║              Two-Phase Commit (2PC)                  ║
// ║                                                      ║
// ║  Phase 1 - Prepare（投票）:                          ║
// ║    Coordinator → Participants: "准备好了吗？"         ║
// ║    Participants: 预写WAL → 回复 YES/NO              ║
// ║                                                      ║
// ║  Phase 2 - Commit/Abort（决策）:                     ║
// ║    if (所有人都 YES):                                ║
// ║        Coordinator → All: COMMIT                     ║
// ║    else:                                             ║
// ║        Coordinator → All: ABORT                      ║
// ╚══════════════════════════════════════════════════════╝
//
// 2PC 的问题：
//   1. 阻塞问题：Coordinator 挂了，Participants 会一直等待
//   2. 性能问题：两轮网络通信 + 多次 fsync
//   3. 可用性问题：任何一个参与者 NO → 全部回滚
//
// 改进方案：
//   - 3PC：加了 PreCommit 阶段，减少阻塞窗口
//   - Percolator（TiDB 使用）：
//     · 用 BigTable/TiKV 存储锁信息
//     · 分为 Prewrite + Commit 两阶段
//     · 不需要专门的 Coordinator
//   - Spanner（CockroachDB 参考）：
//     · 使用 TrueTime API 实现外部一致性
//     · Paxos 组作为参与者（每个组内3-5副本）
//

// 参与者状态
enum class ParticipantState : uint8_t {
    INIT = 0,
    PREPARED,     // 已准备（投YES票）
    COMMITTED,    // 已提交
    ABORTED,      // 已回滚
};

// 参与者ID
using participant_id_t = uint32_t;

// 分布式事务状态
enum class DistributedTxnState : uint8_t {
    INIT = 0,
    PREPARING,    // 正在发送 Prepare
    PREPARED,     // 所有参与者都 Prepare 成功
    COMMITTING,   // 正在发送 Commit
    COMMITTED,    // 全部 Commit 成功
    ABORTING,     // 正在发送 Abort
    ABORTED,      // 全部 Abort 完成
};

// ============================================================
// Participant - 分布式事务参与者
// ============================================================
class Participant {
public:
    explicit Participant(participant_id_t id);
    ~Participant() = default;

    // ============================================================
    // TODO: 你来实现 - 处理 Prepare 请求
    // ============================================================
    // 当收到 Coordinator 的 Prepare 请求时：
    //
    // 实现步骤：
    //   1. 检查本地事务是否可以提交
    //      - 有无锁冲突？
    //      - 数据是否完整？
    //      - 约束是否满足？
    //   2. 如果可以：
    //      - 将事务修改写入 WAL（确保 Prepare 记录持久化）
    //      - fsync WAL
    //      - 返回 YES
    //   3. 如果不行：
    //      - 回滚本地修改
    //      - 返回 NO
    //
    // 【关键】Prepare 后写入的 WAL 必须 fsync！
    //   因为 Participant 投了 YES，就"承诺"可以提交。
    //   即使崩溃重启，也要能根据 WAL 恢复这个承诺。
    //
    bool Prepare(txn_id_t txn_id);

    // ============================================================
    // TODO: 你来实现 - 处理 Commit 请求
    // ============================================================
    // Coordinator 通知提交
    //
    // 实现步骤：
    //   1. 写入 COMMIT 记录到 WAL
    //   2. 释放事务持有的所有锁
    //   3. 使修改对其他事务可见
    //   4. 清理事务上下文
    //
    void Commit(txn_id_t txn_id);

    // ============================================================
    // TODO: 你来实现 - 处理 Abort 请求
    // ============================================================
    void Abort(txn_id_t txn_id);

    participant_id_t GetId() const { return id_; }
    ParticipantState GetState(txn_id_t txn_id) const;

private:
    participant_id_t id_;
    std::unordered_map<txn_id_t, ParticipantState> txn_states_;
    std::mutex participant_mutex_;
};

// ============================================================
// Coordinator - 分布式事务协调者
// ============================================================
class Coordinator {
public:
    Coordinator() = default;
    ~Coordinator() = default;

    // 注册参与者
    void AddParticipant(std::shared_ptr<Participant> participant);

    // ============================================================
    // TODO: 你来实现 - 执行分布式事务（2PC）
    // ============================================================
    // 这是 2PC 协议的完整实现
    //
    // 实现步骤：
    //
    // Phase 1 - Prepare:
    //   1. 向所有参与者发送 Prepare(txn_id)
    //   2. 收集所有响应
    //   3. 如果所有人都返回 YES → 进入 Phase 2 (Commit)
    //   4. 如果有人返回 NO → 进入 Phase 2 (Abort)
    //
    // Phase 2a - Commit:
    //   1. 写入 COMMIT 决定到 Coordinator WAL（fsync！）
    //   2. 向所有参与者发送 Commit(txn_id)
    //   3. 等待所有确认
    //   4. 写入 END 记录
    //
    // Phase 2b - Abort:
    //   1. 写入 ABORT 决定到 Coordinator WAL
    //   2. 向所有参与者发送 Abort(txn_id)
    //
    // 返回 true 表示事务提交成功
    //
    // 【故障处理 - 面试高频题】
    //   Q: Coordinator 在 Phase 1 后、Phase 2 前崩溃怎么办？
    //   A: 如果 COMMIT 记录已写入 WAL → 重启后继续 Commit
    //      如果没有 COMMIT 记录 → 视为 Abort
    //
    //   Q: Participant 在 Prepare 后崩溃怎么办？
    //   A: 重启后检查 WAL 中的 Prepare 记录
    //      向 Coordinator 查询该事务的最终状态
    //
    bool ExecuteTransaction(txn_id_t txn_id);

    // 获取事务状态
    DistributedTxnState GetTxnState(txn_id_t txn_id) const;

private:
    std::vector<std::shared_ptr<Participant>> participants_;
    std::unordered_map<txn_id_t, DistributedTxnState> txn_states_;
    std::mutex coord_mutex_;
};

} // namespace minidb
