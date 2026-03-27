#pragma once

#include "common/types.h"
#include "common/config.h"
#include "storage/page.h"
#include "storage/disk_manager.h"
#include "buffer/lru_replacer.h"

#include <mutex>
#include <unordered_map>
#include <list>
#include <memory>

namespace minidb {

// ============================================================
// BufferPoolManager - 缓冲池管理器
// ============================================================
//
// 【面试核心知识点 - 最重要的组件之一】
//
// Buffer Pool 是数据库内存管理的核心，它的职责：
//   1. 管理一块固定大小的内存区域（由多个 frame 组成）
//   2. 作为磁盘和上层模块之间的缓存层
//   3. 使用替换策略（LRU/Clock）管理页面的换入换出
//
// 核心流程（FetchPage 为例）：
//   1. 先查 page_table_（哈希表），看页面是否已在内存中
//   2. 如果在 → 增加 pin_count，直接返回
//   3. 如果不在 → 找一个空闲帧（或通过Replacer淘汰一个）
//   4. 如果被淘汰的帧是脏页 → 先写回磁盘
//   5. 从磁盘读取目标页面到这个帧
//   6. 更新 page_table_ 映射关系
//
// PostgreSQL 对应：src/backend/storage/buffer/bufmgr.c
//   - ReadBuffer() → 对应我们的 FetchPage()
//   - StrategyGetBuffer() → 对应我们的 Replacer
//
class BufferPoolManager {
public:
    BufferPoolManager(size_t pool_size, DiskManager* disk_manager);
    ~BufferPoolManager();

    // 禁止拷贝
    BufferPoolManager(const BufferPoolManager&) = delete;
    BufferPoolManager& operator=(const BufferPoolManager&) = delete;

    // ============================================================
    // TODO: 你来实现 - 获取页面（最核心的函数）
    // ============================================================
    // 从 Buffer Pool 获取指定页面。如果不在内存中，从磁盘加载。
    //
    // 实现步骤：
    //   1. 在 page_table_ 中查找 page_id
    //      - 找到 → 该页面已在内存中
    //        a. pin_count++
    //        b. 通知 replacer_ 该帧被 Pin 了（不可淘汰）
    //        c. 返回 Page*
    //
    //   2. 没找到 → 需要从磁盘加载
    //      a. 先获取一个空闲帧（FindFreeFrame，见下方辅助函数）
    //      b. 如果空闲帧之前有旧页面且是脏页 → 写回磁盘
    //      c. 清除旧页面在 page_table_ 中的映射
    //      d. 从磁盘读取新页面数据到帧中
    //      e. 设置页面元数据（page_id, pin_count=1, dirty=false）
    //      f. 在 page_table_ 中建立新映射
    //      g. 返回 Page*
    //
    // 返回值：成功返回 Page*，失败返回 nullptr（所有帧都被pin住了）
    //
    Page* FetchPage(page_id_t page_id);

    // ============================================================
    // TODO: 你来实现 - 释放页面（Unpin）
    // ============================================================
    // 使用完页面后调用，减少 pin_count
    //
    // 实现步骤：
    //   1. 在 page_table_ 中查找，没找到返回 false
    //   2. pin_count <= 0 返回 false（不能减到负数）
    //   3. pin_count--
    //   4. 如果 is_dirty 为 true，标记页面为脏
    //   5. 如果 pin_count 降为 0，通知 replacer_ 该帧可以被淘汰了
    //   6. 返回 true
    //
    bool UnpinPage(page_id_t page_id, bool is_dirty);

    // ============================================================
    // TODO: 你来实现 - 创建新页面
    // ============================================================
    // 在 Buffer Pool 中创建一个新的页面
    //
    // 实现步骤：
    //   1. 获取空闲帧
    //   2. 通过 DiskManager 分配新的 page_id
    //   3. 设置页面元数据
    //   4. 更新 page_table_
    //   5. 将 page_id 写入输出参数
    //   6. 返回 Page*
    //
    Page* NewPage(page_id_t* page_id);

    // ============================================================
    // TODO: 你来实现 - 刷新页面到磁盘
    // ============================================================
    // 无论页面是否为脏，强制写回磁盘
    //
    bool FlushPage(page_id_t page_id);

    // 刷新所有页面到磁盘（checkpoint 时使用）
    void FlushAllPages();

private:
    // ============================================================
    // TODO: 你来实现 - 查找空闲帧（辅助函数）
    // ============================================================
    // 优先从 free_list_ 获取，如果没有则通过 replacer_ 淘汰
    //
    // 实现步骤：
    //   1. 检查 free_list_ 是否非空
    //      - 是 → 取出一个空闲帧ID，返回 true
    //   2. free_list_ 为空 → 调用 replacer_->Evict()
    //      - 成功淘汰 → 返回 true
    //      - 无法淘汰（所有帧都被pin住） → 返回 false
    //
    bool FindFreeFrame(frame_id_t* frame_id);

    size_t pool_size_;                  // Buffer Pool 帧数
    Page* pages_;                       // 帧数组（内存中的页面）
    DiskManager* disk_manager_;         // 磁盘管理器
    std::unique_ptr<LRUReplacer> replacer_;  // 页面替换器

    // page_id → frame_id 的映射表
    // 这是 Buffer Pool 的核心数据结构，类似于 PG 的 buf_table
    std::unordered_map<page_id_t, frame_id_t> page_table_;

    // 空闲帧列表（初始时所有帧都在这里）
    std::list<frame_id_t> free_list_;

    std::mutex latch_;  // 保护所有内部状态
};

} // namespace minidb
