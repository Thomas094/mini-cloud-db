#include "tenant/tenant_manager.h"
#include <stdexcept>

namespace minidb {

// ============================================================
// TODO: 你来实现
// ============================================================
tenant_id_t TenantManager::CreateTenant(const std::string& name,
                                          const ResourceQuota& quota) {
    // std::lock_guard<std::mutex> lock(tenant_mutex_);
    //
    // tenant_id_t id = next_tenant_id_.fetch_add(1);
    //
    // TenantInfo info;
    // info.id = id;
    // info.name = name;
    // info.quota = quota;
    // info.usage = std::make_unique<ResourceUsage>();
    // info.active = true;
    //
    // tenants_.emplace(id, std::move(info));
    // return id;

    throw std::runtime_error("CreateTenant: 尚未实现");
}

// ============================================================
// TODO: 你来实现
// ============================================================
bool TenantManager::AcquireMemory(tenant_id_t tenant_id, size_t bytes) {
    // std::lock_guard<std::mutex> lock(tenant_mutex_);
    //
    // auto it = tenants_.find(tenant_id);
    // if (it == tenants_.end()) return false;
    //
    // auto& info = it->second;
    // size_t current = info.usage->memory_used.load();
    // if (current + bytes > info.quota.max_memory_bytes) {
    //     return false;  // 超出配额
    // }
    // info.usage->memory_used.fetch_add(bytes);
    // return true;
    //
    // 【思考】这里的 check-then-act 在高并发下有 TOCTOU 问题
    //   更好的做法是用 compare_exchange_weak 循环：
    //   do {
    //       current = memory_used.load();
    //       if (current + bytes > max) return false;
    //   } while (!memory_used.compare_exchange_weak(current, current + bytes));

    throw std::runtime_error("AcquireMemory: 尚未实现");
}

// ============================================================
// TODO: 你来实现
// ============================================================
bool TenantManager::AcquireBufferPages(tenant_id_t tenant_id, size_t pages) {
    // 同 AcquireMemory 逻辑，检查 buffer_pages_used
    throw std::runtime_error("AcquireBufferPages: 尚未实现");
}

// ============================================================
// TODO: 你来实现
// ============================================================
bool TenantManager::AcquireIOPS(tenant_id_t tenant_id) {
    // 检查当前周期 io_ops_count 是否已达上限
    throw std::runtime_error("AcquireIOPS: 尚未实现");
}

// ============================================================
// TODO: 你来实现
// ============================================================
void TenantManager::ReleaseMemory(tenant_id_t tenant_id, size_t bytes) {
    // info.usage->memory_used.fetch_sub(bytes);
    throw std::runtime_error("ReleaseMemory: 尚未实现");
}

// ============================================================
// TODO: 你来实现
// ============================================================
void TenantManager::ReleaseBufferPages(tenant_id_t tenant_id, size_t pages) {
    throw std::runtime_error("ReleaseBufferPages: 尚未实现");
}

// ============================================================
// TODO: 你来实现
// ============================================================
void TenantManager::ResetIOPSCounters() {
    // std::lock_guard<std::mutex> lock(tenant_mutex_);
    // for (auto& [id, info] : tenants_) {
    //     info.usage->io_ops_count.store(0);
    // }
    throw std::runtime_error("ResetIOPSCounters: 尚未实现");
}

const TenantInfo* TenantManager::GetTenantInfo(tenant_id_t tenant_id) const {
    std::lock_guard<std::mutex> lock(tenant_mutex_);
    auto it = tenants_.find(tenant_id);
    return it != tenants_.end() ? &it->second : nullptr;
}

std::vector<tenant_id_t> TenantManager::ListTenants() const {
    std::lock_guard<std::mutex> lock(tenant_mutex_);
    std::vector<tenant_id_t> result;
    result.reserve(tenants_.size());
    for (auto& [id, _] : tenants_) {
        result.push_back(id);
    }
    return result;
}

} // namespace minidb
