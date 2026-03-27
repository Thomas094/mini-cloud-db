#include "tiered/tiered_storage.h"
#include <stdexcept>

namespace minidb {

TieredStorage::TieredStorage(const TieringPolicy& policy) : policy_(policy) {}

// ============================================================
// TODO: 你来实现
// ============================================================
void TieredStorage::RecordAccess(page_id_t page_id) {
    // std::lock_guard<std::mutex> lock(tier_mutex_);
    //
    // auto& stats = access_stats_[page_id];
    // stats.page_id = page_id;
    // stats.access_count++;
    // stats.last_access_time = std::chrono::steady_clock::now();
    //
    // // 如果是冷数据被频繁访问，提升温度
    // if (stats.temperature == DataTemperature::COLD) {
    //     stats.temperature = DataTemperature::WARM;
    //     // 触发从冷存储加载回热存储
    // }

    throw std::runtime_error("RecordAccess: 尚未实现");
}

// ============================================================
// TODO: 你来实现
// ============================================================
DataTemperature TieredStorage::EvaluateTemperature(page_id_t page_id) const {
    // 参考 tiered_storage.h 中的注释
    throw std::runtime_error("EvaluateTemperature: 尚未实现");
}

// ============================================================
// TODO: 你来实现
// ============================================================
void TieredStorage::RunMigration() {
    // 参考 tiered_storage.h 中的注释
    //
    // 【实际系统的复杂性】
    //   1. 迁移过程中页面可能被修改 → 需要处理并发
    //   2. 冷存储可能不可用 → 需要重试和降级策略
    //   3. 迁移可能影响在线查询性能 → 需要限流
    //   4. Neon 使用 "layer" 概念管理数据：
    //      - Image Layer: 完整的页面快照
    //      - Delta Layer: WAL 增量
    //      - 旧的 Layer 可以异步迁移到 S3

    throw std::runtime_error("RunMigration: 尚未实现");
}

DataTemperature TieredStorage::GetTemperature(page_id_t page_id) const {
    std::lock_guard<std::mutex> lock(tier_mutex_);
    auto it = access_stats_.find(page_id);
    if (it != access_stats_.end()) {
        return it->second.temperature;
    }
    return DataTemperature::HOT;  // 默认热数据
}

TieredStorage::TierStats TieredStorage::GetStats() const {
    std::lock_guard<std::mutex> lock(tier_mutex_);
    TierStats stats;
    for (auto& [_, access] : access_stats_) {
        switch (access.temperature) {
            case DataTemperature::HOT:  stats.hot_pages++;  break;
            case DataTemperature::WARM: stats.warm_pages++; break;
            case DataTemperature::COLD: stats.cold_pages++; break;
        }
    }
    return stats;
}

} // namespace minidb
