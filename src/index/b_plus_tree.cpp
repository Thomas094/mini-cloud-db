#include "index/b_plus_tree.h"
#include <stdexcept>
#include <algorithm>

namespace minidb {

// ============================================================
// TODO: 你来实现 - 查找
// ============================================================
std::optional<ValueType> BPlusTree::Search(KeyType key) const {
    // 实现提示：
    //
    // 1. 如果树为空 (root_page_id_ == INVALID_PAGE_ID) → 返回 nullopt
    //
    // 2. 从根节点开始，沿着内部节点找到叶子节点
    //    LeafNode* leaf = FindLeafNode(key);
    //
    // 3. 在叶子节点中二分查找 key
    //    int idx = BinarySearch(leaf->keys, leaf->header.size, key);
    //    if (idx >= 0 && idx < leaf->header.size && leaf->keys[idx] == key) {
    //        return leaf->values[idx];
    //    }
    //    return std::nullopt;

    throw std::runtime_error("BPlusTree::Search: 尚未实现");
}

// ============================================================
// TODO: 你来实现 - 插入
// ============================================================
bool BPlusTree::Insert(KeyType key, const ValueType& value) {
    // 这是 B+Tree 最复杂的操作，建议分步骤实现：
    //
    // Step 1: 处理空树
    //   if (root_page_id_ == INVALID_PAGE_ID) {
    //       // 创建一个叶子节点作为根
    //       // 插入 key-value
    //       // 设置 root_page_id_
    //       return true;
    //   }
    //
    // Step 2: 找到目标叶子节点
    //   LeafNode* leaf = FindLeafNode(key);
    //
    // Step 3: 检查是否有重复 key
    //   // 如果已存在，返回 false（不允许重复）
    //
    // Step 4: 叶子未满 → 直接插入
    //   if (leaf->header.size < LeafNode::MAX_PAIRS) {
    //       // 在有序位置插入 key-value
    //       // 后面的元素往后移动（memmove）
    //       return true;
    //   }
    //
    // Step 5: 叶子已满 → 分裂
    //   // 1. 创建新叶子
    //   // 2. 将后半部分移到新叶子
    //   // 3. 将新元素插入合适的叶子
    //   // 4. 更新 next_leaf 链表
    //   // 5. 将分裂 key 上推到父节点 → InsertIntoParent()
    //
    // 【分裂的关键】
    //   分裂点 = (MAX_PAIRS + 1) / 2
    //   上推的 key = 新叶子的第一个 key（叶子分裂）
    //
    // 【内部节点分裂与叶子不同】
    //   叶子分裂：上推 key 的副本（key同时保留在叶子中）
    //   内部节点分裂：上推 key 被"移走"（不保留在原节点中）

    throw std::runtime_error("BPlusTree::Insert: 尚未实现");
}

// ============================================================
// TODO: 你来实现 - 删除
// ============================================================
bool BPlusTree::Delete(KeyType key) {
    // 实现提示：
    //
    // 1. 找到包含 key 的叶子节点
    // 2. 删除 key-value（后面的元素前移）
    // 3. 如果叶子仍然至少半满 → 完成
    // 4. 如果不足半满 → Redistribute 或 Merge
    //
    // Redistribute (借):
    //   - 从相邻兄弟借一个 key
    //   - 更新父节点中的分隔 key
    //
    // Merge (合并):
    //   - 将当前节点合并到兄弟节点
    //   - 从父节点删除分隔 key
    //   - 父节点可能也不足半满 → 递归处理

    throw std::runtime_error("BPlusTree::Delete: 尚未实现");
}

// ============================================================
// TODO: 你来实现 - 范围查询
// ============================================================
std::vector<ValueType> BPlusTree::RangeScan(KeyType begin_key, KeyType end_key) const {
    // 实现提示：
    //
    // 1. 找到 begin_key 所在的叶子节点
    // 2. 在叶子中找到 >= begin_key 的第一个位置
    // 3. 从该位置开始顺序扫描：
    //    while (current_key <= end_key) {
    //        result.push_back(current_value);
    //        advance to next;
    //        if (到达叶子末尾) {
    //            跳到 next_leaf_page_id 指向的下一个叶子;
    //        }
    //    }
    //
    // 【这就是 B+Tree 叶子链表的价值！】
    //   B-Tree 做范围查询需要中序遍历（涉及回溯），效率低。
    //   B+Tree 只需沿着叶子链表顺序扫描，缓存友好、效率高。

    throw std::runtime_error("BPlusTree::RangeScan: 尚未实现");
}

// ============================================================
// TODO: 你来实现 - 查找叶子节点（辅助函数）
// ============================================================
LeafNode* BPlusTree::FindLeafNode(KeyType key) const {
    // 从根节点开始，沿内部节点向下搜索：
    //
    // page_id_t current = root_page_id_;
    // while (true) {
    //     获取 current 对应的节点页面;
    //     if (节点是叶子) return reinterpret_cast<LeafNode*>(page_data);
    //
    //     auto* internal = reinterpret_cast<InternalNode*>(page_data);
    //     // 在内部节点的 keys 中找到 key 应该在的子树
    //     int idx = 上界二分查找(internal->keys, internal->header.size, key);
    //     current = internal->children[idx];
    // }

    throw std::runtime_error("FindLeafNode: 尚未实现");
}

int BPlusTree::BinarySearch(const KeyType* keys, int size, KeyType target) const {
    int lo = 0, hi = size - 1;
    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2;
        if (keys[mid] == target) return mid;
        if (keys[mid] < target) lo = mid + 1;
        else hi = mid - 1;
    }
    return lo;  // 返回插入位置
}

void BPlusTree::Print() const {
    // 调试用，可以后续实现层次遍历打印
}

} // namespace minidb
