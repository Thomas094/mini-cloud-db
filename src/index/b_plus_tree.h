#pragma once

#include "common/types.h"
#include "common/config.h"
#include "storage/page.h"
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace minidb {

// ============================================================
// B+Tree 索引
// ============================================================
//
// 【面试核心知识点】
//
// B+Tree 是关系数据库最常用的索引结构。
// PostgreSQL 的 btree 实现在 src/backend/access/nbtree/ 中。
//
// 与 B-Tree 的区别：
//   - B+Tree 所有数据都在叶子节点
//   - 叶子节点之间有链表指针（方便范围查询）
//   - 内部节点只存 key 和子节点指针
//
// 关键操作的复杂度：
//   查找: O(log_B N)  —— B 是阶数
//   插入: O(log_B N)  —— 可能触发分裂
//   删除: O(log_B N)  —— 可能触发合并
//
// 并发 B+Tree（高级话题）：
//   - Latch Crabbing: 从上到下加锁，确认不分裂后释放父节点锁
//   - Optimistic Latch Coupling: 先不加写锁，到叶子才加
//   - PostgreSQL 使用 Lehman-Yao 算法（带 right-link 的 B+Tree）
//

// 简化的 Key-Value 对
using KeyType = int64_t;
struct ValueType {
    page_id_t page_id{INVALID_PAGE_ID};
    uint16_t slot_num{0};
};

// ============================================================
// B+Tree 节点基类
// ============================================================
// 每个节点存储在一个 Page 中
// 节点类型通过页面头部标识
//
enum class BPlusTreeNodeType : uint8_t {
    INTERNAL = 0,
    LEAF = 1,
};

struct BPlusTreeNodeHeader {
    BPlusTreeNodeType node_type;
    int size{0};              // 当前key数量
    int max_size{0};          // 最大key数量
    page_id_t parent_page_id{INVALID_PAGE_ID};
};

// ============================================================
// B+Tree 叶子节点
// ============================================================
// 布局：| Header | key1 | val1 | key2 | val2 | ... | next_leaf_page_id |
//
struct LeafNode {
    BPlusTreeNodeHeader header;
    page_id_t next_leaf_page_id{INVALID_PAGE_ID};  // 叶子链表

    static constexpr int MAX_PAIRS =
        (PAGE_SIZE - sizeof(BPlusTreeNodeHeader) - sizeof(page_id_t))
        / (sizeof(KeyType) + sizeof(ValueType));

    KeyType keys[MAX_PAIRS]{};
    ValueType values[MAX_PAIRS]{};
};

// ============================================================
// B+Tree 内部节点
// ============================================================
// 布局：| Header | ptr0 | key1 | ptr1 | key2 | ptr2 | ... |
// 注意：n 个 key 对应 n+1 个子节点指针
//
struct InternalNode {
    BPlusTreeNodeHeader header;

    static constexpr int MAX_KEYS =
        (PAGE_SIZE - sizeof(BPlusTreeNodeHeader) - sizeof(page_id_t))
        / (sizeof(KeyType) + sizeof(page_id_t));

    KeyType keys[MAX_KEYS]{};
    page_id_t children[MAX_KEYS + 1]{};  // children[i] 指向 key[i] 左边的子树
};

// ============================================================
// BPlusTree - B+Tree 索引实现
// ============================================================
class BPlusTree {
public:
    BPlusTree() = default;
    ~BPlusTree() = default;

    // ============================================================
    // TODO: 你来实现 - 查找
    // ============================================================
    // 根据 key 查找对应的 value
    //
    // 实现步骤：
    //   1. 从根节点开始
    //   2. 如果是内部节点：
    //      - 二分查找找到合适的子节点指针
    //      - 递归进入子节点
    //   3. 如果是叶子节点：
    //      - 二分查找 key
    //      - 找到返回 value，没找到返回 nullopt
    //
    // 【并发优化 - Latch Crabbing 协议】
    //   读操作：从上到下获取 Read Latch
    //     - 获取子节点的 Read Latch 后，释放父节点的 Read Latch
    //   写操作：从上到下获取 Write Latch
    //     - 如果子节点是"安全的"（不会分裂/合并），释放所有祖先的 Write Latch
    //     - "安全"定义：插入时 size < max_size-1；删除时 size > min_size
    //
    std::optional<ValueType> Search(KeyType key) const;

    // ============================================================
    // TODO: 你来实现 - 插入
    // ============================================================
    // 插入 key-value 对
    //
    // 实现步骤：
    //   1. 如果树为空，创建根叶子节点
    //   2. 找到目标叶子节点
    //   3. 如果叶子有空间 → 直接插入（保持有序）
    //   4. 如果叶子满了 → 分裂（Split）：
    //      a. 创建新叶子节点
    //      b. 将原叶子的后半部分移到新叶子
    //      c. 更新叶子链表指针
    //      d. 将新叶子的最小 key 上推到父节点
    //      e. 如果父节点也满了 → 递归分裂（可能一直到根）
    //   5. 如果根节点分裂 → 创建新的根节点
    //
    // 【面试常见追问】
    //   Q: B+Tree 分裂时如何保证并发安全？
    //   A: 使用 Latch Crabbing。写操作持有从根到叶的写锁链，
    //      确认不会分裂后逐步释放上层锁。
    //
    bool Insert(KeyType key, const ValueType& value);

    // ============================================================
    // TODO: 你来实现 - 删除
    // ============================================================
    // 删除指定 key
    //
    // 实现步骤：
    //   1. 找到目标叶子节点
    //   2. 删除 key-value 对
    //   3. 如果叶子仍然至少半满 → 完成
    //   4. 如果叶子不足半满 → 尝试借（Redistribute）或合并（Merge）：
    //      a. 先尝试从兄弟节点借一个 key
    //      b. 如果兄弟也不够 → 与兄弟合并
    //      c. 合并后父节点也要删除一个 key，可能递归向上
    //
    bool Delete(KeyType key);

    // ============================================================
    // TODO: 你来实现 - 范围查询
    // ============================================================
    // 返回 [begin_key, end_key] 范围内的所有 value
    //
    // 利用叶子节点的链表指针进行顺序扫描，
    // 这就是 B+Tree 比 B-Tree 优越的地方之一。
    //
    std::vector<ValueType> RangeScan(KeyType begin_key, KeyType end_key) const;

    // 打印树结构（调试用）
    void Print() const;

private:
    // 查找 key 应该在的叶子节点
    LeafNode* FindLeafNode(KeyType key) const;

    // 在有序数组中二分查找
    int BinarySearch(const KeyType* keys, int size, KeyType target) const;

    page_id_t root_page_id_{INVALID_PAGE_ID};

    // 简化实现：用内存中的节点，不走 Buffer Pool
    // 完整实现应通过 BufferPoolManager 管理节点页面
    std::vector<std::unique_ptr<char[]>> node_pages_;
};

} // namespace minidb
