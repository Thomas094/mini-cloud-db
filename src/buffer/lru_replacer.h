#pragma once

#include "common/types.h"
#include "common/config.h"
#include <cstddef>
#include <list>
#include <unordered_map>

namespace minidb {

// ============================================================
// LRUReplacer - LRU 页面替换策略
// ============================================================
//
// 【面试核心知识点】
// 当 Buffer Pool 满了，需要淘汰一个页面腾出空间。
//
// PostgreSQL 实际使用的是 Clock-Sweep 算法（不是纯LRU）：
//   - 每个 buffer 有一个 usage_count
//   - "时钟指针"扫描时，usage_count > 0 的减1并跳过
//   - usage_count == 0 的被淘汰
//   - 优点：O(1) 近似LRU，适合高并发
//
// 这里先实现标准 LRU（用链表+哈希表），你可以之后挑战 Clock-Sweep。
//
class LRUReplacer {
public:
    explicit LRUReplacer(size_t capacity);
    ~LRUReplacer() = default;

    // ============================================================
    // TODO: 你来实现 - 淘汰一个页面
    // ============================================================
    // 淘汰最近最少使用的帧，将帧ID写入 frame_id
    // 返回 true 表示成功淘汰，false 表示没有可淘汰的帧
    //
    // 实现提示：
    //   使用 std::list 作为LRU链表，最近使用的放头部，淘汰尾部
    //   使用 std::unordered_map<frame_id_t, list::iterator> 做O(1)查找
    //
    //   1. 如果链表为空，返回 false
    //   2. 取链表尾部的 frame_id（最久未使用）
    //   3. 从链表和哈希表中删除
    //   4. 将 frame_id 写入输出参数，返回 true
    //
    bool Evict(frame_id_t* frame_id);

    // ============================================================
    // TODO: 你来实现 - 标记帧为可淘汰
    // ============================================================
    // 当页面的 pin_count 降为0时调用，将帧加入淘汰候选列表
    //
    // 实现提示：
    //   1. 如果帧已在链表中，忽略（避免重复插入）
    //   2. 如果链表已满（size >= capacity），需要先淘汰一个
    //   3. 将帧插入链表头部，同时更新哈希表
    //
    void Unpin(frame_id_t frame_id);

    // ============================================================
    // TODO: 你来实现 - 标记帧为不可淘汰
    // ============================================================
    // 当页面被 pin（有线程使用）时调用，从淘汰候选列表中移除
    //
    // 实现提示：
    //   1. 如果帧不在链表中，直接返回
    //   2. 使用哈希表O(1)定位，从链表中删除
    //   3. 从哈希表中也删除
    //
    void Pin(frame_id_t frame_id);

    // 返回当前可淘汰的帧数量
    size_t Size() const;

private:
    size_t capacity_;

    // LRU 链表：front = 最近使用，back = 最久未使用
    std::list<frame_id_t> lru_list_;

    // 帧ID → 链表迭代器的映射，用于O(1)查找和删除
    std::unordered_map<frame_id_t, std::list<frame_id_t>::iterator> lru_map_;
};

} // namespace minidb
