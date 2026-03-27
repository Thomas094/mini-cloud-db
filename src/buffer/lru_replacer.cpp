#include "buffer/lru_replacer.h"
#include "common/types.h"
#include <stdexcept>

namespace minidb {

LRUReplacer::LRUReplacer(size_t capacity) : capacity_(capacity) {}

bool LRUReplacer::Evict(frame_id_t* frame_id) {
    if (lru_list_.empty()) {
        return false;
    }

    *frame_id = lru_list_.back();
    lru_map_.erase(*frame_id);
    lru_list_.pop_back();
    return true;
}

void LRUReplacer::Unpin(frame_id_t frame_id) {
    // 你的实现...
    if (lru_map_.find(frame_id) != lru_map_.end()) {
        return;
    }
    if (lru_list_.size() >= capacity_) {
        frame_id_t evicted_frame_id;
        Evict(&evicted_frame_id);
    }
    lru_list_.push_front(frame_id);
    lru_map_[frame_id] = lru_list_.begin();
}

void LRUReplacer::Pin(frame_id_t frame_id) {
    if (lru_map_.find(frame_id) == lru_map_.end()) {
        return;
    }
    lru_list_.erase(lru_map_[frame_id]);
    lru_map_.erase(frame_id);
}

size_t LRUReplacer::Size() const {
    return lru_list_.size();
}


} // namespace minidb
