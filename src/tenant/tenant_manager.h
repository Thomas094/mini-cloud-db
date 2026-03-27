#pragma once

#include "common/types.h"
#include "common/config.h"
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace minidb {

// ============================================================
// TenantManager - 多租户资源管理
// ============================================================
//
// 【面试核心知识点 - 多租户隔离】
//
// 多租户（Multi-Tenancy）是 Cloud-Native 数据库的关键能力。
// 多个用户（租户）共享同一套数据库基础设施，但：
//   1. 数据隔离：租户A看不到租户B的数据
//   2. 性能隔离：租户A的大查询不能影响租户B
//   3. 资源隔离：CPU/内存/IO 按配额分配
//
// Noisy Neighbor（吵闹的邻居）问题：
//   某个租户突然跑一个大查询，吃掉所有资源，
//   导致其他租户的延迟飙升。
//
// 解决方案层次：
//   Level 1: 共享一切（最省成本，隔离最差）
//   Level 2: 共享进程，独立连接池 + 资源配额
//   Level 3: 独立进程（如 Neon 的做法：每个租户一个PG进程）
//   Level 4: 独立虚拟机/容器（如 Aurora Serverless）
//
// Neon 的多租户架构：
//   - 每个租户有独立的 Compute 节点（PG进程）
//   - Pageserver 在存储层做租户隔离（不同的 timeline）
//   - Compute 可以按需启停（Serverless，0→1 冷启动）
//

// 资源配额定义
struct ResourceQuota {
    size_t max_memory_bytes{DEFAULT_TENANT_MEMORY_QUOTA};  // 最大内存
    size_t max_buffer_pool_pages{256};  // 最大缓冲池页数
    double max_cpu_percent{25.0};       // 最大CPU占比
    uint64_t max_iops{1000};            // 最大IOPS
    uint64_t max_connections{100};      // 最大连接数
};

// 租户资源使用情况
struct ResourceUsage {
    std::atomic<size_t> memory_used{0};
    std::atomic<size_t> buffer_pages_used{0};
    std::atomic<uint64_t> io_ops_count{0};      // 当前周期I/O操作数
    std::atomic<uint64_t> active_connections{0};
    std::atomic<uint64_t> total_queries{0};
};

// 租户信息
struct TenantInfo {
    tenant_id_t id;
    std::string name;
    ResourceQuota quota;
    std::unique_ptr<ResourceUsage> usage;
    bool active{true};
};

class TenantManager {
public:
    TenantManager() = default;
    ~TenantManager() = default;

    // ============================================================
    // TODO: 你来实现 - 创建租户
    // ============================================================
    // 创建新租户并分配默认资源配额
    //
    // 实现步骤：
    //   1. 分配唯一的 tenant_id
    //   2. 创建 TenantInfo 和 ResourceUsage
    //   3. 存储到 tenants_ 映射
    //   4. 返回 tenant_id
    //
    tenant_id_t CreateTenant(const std::string& name,
                              const ResourceQuota& quota = ResourceQuota());

    // ============================================================
    // TODO: 你来实现 - 资源申请（准入控制）
    // ============================================================
    // 在执行操作前检查资源配额
    //
    // 实现逻辑：
    //   1. 查找租户
    //   2. 检查 usage + requested <= quota
    //      - 内存：usage.memory_used + size <= quota.max_memory
    //      - Buffer Pool：usage.buffer_pages_used + pages <= quota.max_buffer_pool_pages
    //      - IOPS：当前周期 io_ops_count < quota.max_iops
    //   3. 如果超额 → 返回 false（拒绝请求）
    //   4. 如果允许 → 更新 usage，返回 true
    //
    // 【Noisy Neighbor 防御】
    //   配额用尽时的策略：
    //   - 硬限制：直接拒绝请求（返回错误）
    //   - 软限制：允许但降低优先级（排队等待）
    //   - 弹性：允许短暂突破配额（burst），但很快限流
    //
    bool AcquireMemory(tenant_id_t tenant_id, size_t bytes);
    bool AcquireBufferPages(tenant_id_t tenant_id, size_t pages);
    bool AcquireIOPS(tenant_id_t tenant_id);

    // ============================================================
    // TODO: 你来实现 - 资源释放
    // ============================================================
    void ReleaseMemory(tenant_id_t tenant_id, size_t bytes);
    void ReleaseBufferPages(tenant_id_t tenant_id, size_t pages);

    // ============================================================
    // TODO: 你来实现 - 重置IOPS计数器
    // ============================================================
    // 每个周期（如1秒）调用一次，重置所有租户的IOPS计数
    //
    void ResetIOPSCounters();

    // 获取租户信息
    const TenantInfo* GetTenantInfo(tenant_id_t tenant_id) const;

    // 获取所有租户列表
    std::vector<tenant_id_t> ListTenants() const;

private:
    std::unordered_map<tenant_id_t, TenantInfo> tenants_;
    std::atomic<tenant_id_t> next_tenant_id_{1};
    mutable std::mutex tenant_mutex_;
};

} // namespace minidb
