#pragma once

#include "common/types.h"
#include "common/config.h"
#include <chrono>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace minidb {

// ============================================================
// TieredStorage - 冷热数据分层存储
// ============================================================
//
// 【面试核心知识点 - 冷热分层】
//
// 核心思想：不同"温度"的数据放在不同成本的存储介质上
//
// +-------------------+------------------+-------------------+
// | 热数据 (Hot)       | 温数据 (Warm)     | 冷数据 (Cold)     |
// +-------------------+------------------+-------------------+
// | NVMe SSD          | SATA SSD         | S3 / HDD          |
// | 延迟: <100μs      | 延迟: <1ms       | 延迟: >10ms        |
// | 成本: $$$          | 成本: $$         | 成本: $            |
// | 高频访问           | 中频访问          | 低频访问           |
// +-------------------+------------------+-------------------+
//
// Neon 的冷热分层：
//   - 热数据在 Pageserver 本地 SSD
//   - 冷数据自动迁移到 S3 对象存储
//   - 访问冷数据时从 S3 按需加载
//
// Aurora 的存储分层：
//   - 所有数据在分布式存储层（EBS + S3）
//   - 最近的数据在存储节点缓存中
//   - 备份数据在 S3
//

// 页面访问统计
struct PageAccessStats {
    page_id_t page_id;
    DataTemperature temperature{DataTemperature::HOT};
    uint64_t access_count{0};
    std::chrono::steady_clock::time_point last_access_time;
    std::chrono::steady_clock::time_point create_time;
};

// 分层策略
struct TieringPolicy {
    // 热→温的阈值（秒未访问）
    int hot_to_warm_seconds{HOT_DATA_ACCESS_INTERVAL_SEC};
    // 温→冷的阈值（秒未访问）
    int warm_to_cold_seconds{WARM_DATA_ACCESS_INTERVAL_SEC};
    // 冷数据目标路径（模拟 S3）
    std::string cold_storage_path{"/tmp/minidb_cold/"};
    // 后台扫描间隔（秒）
    int scan_interval_seconds{60};
};

class TieredStorage {
public:
    explicit TieredStorage(const TieringPolicy& policy = TieringPolicy());
    ~TieredStorage() = default;

    // ============================================================
    // TODO: 你来实现 - 记录页面访问
    // ============================================================
    // 每次页面被访问时调用，更新统计信息
    //
    // 实现步骤：
    //   1. 更新 access_count++
    //   2. 更新 last_access_time = now
    //   3. 如果页面当前是温/冷数据但频繁访问 → 提升为热数据
    //
    void RecordAccess(page_id_t page_id);

    // ============================================================
    // TODO: 你来实现 - 评估页面温度
    // ============================================================
    // 根据访问统计判断页面的温度
    //
    // 实现逻辑：
    //   time_since_access = now - last_access_time
    //   if (time_since_access < hot_threshold) → HOT
    //   elif (time_since_access < warm_threshold) → WARM
    //   else → COLD
    //
    DataTemperature EvaluateTemperature(page_id_t page_id) const;

    // ============================================================
    // TODO: 你来实现 - 执行数据迁移
    // ============================================================
    // 扫描所有页面，将变冷的数据迁移到冷存储
    //
    // 实现步骤：
    //   1. 遍历所有页面的访问统计
    //   2. 评估每个页面的温度
    //   3. 对于新变冷的页面：
    //      a. 将数据写入冷存储路径（模拟上传到S3）
    //      b. 从热存储中释放空间
    //      c. 更新页面的温度状态
    //   4. 对于变热的冷数据（被频繁访问）：
    //      a. 从冷存储加载回热存储
    //
    // 【关键要求】迁移过程不能阻塞在线读写！
    //   - 使用 copy-on-write 或 双写策略
    //   - 迁移完成后原子切换
    //
    void RunMigration();

    // 获取页面当前温度
    DataTemperature GetTemperature(page_id_t page_id) const;

    // 获取各温度层的页面数量统计
    struct TierStats {
        size_t hot_pages{0};
        size_t warm_pages{0};
        size_t cold_pages{0};
    };
    TierStats GetStats() const;

private:
    TieringPolicy policy_;
    std::unordered_map<page_id_t, PageAccessStats> access_stats_;
    mutable std::mutex tier_mutex_;
};

} // namespace minidb
