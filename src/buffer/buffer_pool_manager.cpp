#include "buffer/buffer_pool_manager.h"
#include "common/types.h"
#include "storage/page.h"
#include <stdexcept>

namespace minidb {

BufferPoolManager::BufferPoolManager(size_t pool_size, DiskManager* disk_manager)
    : pool_size_(pool_size), disk_manager_(disk_manager) {
    // 分配帧数组
    pages_ = new Page[pool_size];
    // 创建 LRU 替换器
    replacer_ = std::make_unique<LRUReplacer>(pool_size);
    // 初始化空闲列表：所有帧一开始都是空闲的
    for (size_t i = 0; i < pool_size; i++) {
        free_list_.push_back(static_cast<frame_id_t>(i));
    }
}

BufferPoolManager::~BufferPoolManager() {
    // 析构时刷新所有脏页
    FlushAllPages();
    delete[] pages_;
}

// ============================================================
// TODO: 你来实现
// ============================================================
Page* BufferPoolManager::FetchPage(page_id_t page_id) {
    {
        std::lock_guard<std::mutex> lock(latch_);
        if (page_table_.find(page_id) != page_table_.end()) {
            frame_id_t frame_id = page_table_[page_id];
            Page *page = &pages_[frame_id];
            page->IncrementPinCount();
            replacer_->Pin(frame_id);
            return page;
        }
    }
    frame_id_t free_frame_id = 0;
    if (FindFreeFrame(&free_frame_id)) {
        Page* page = nullptr;
        {
            std::lock_guard<std::mutex> lock(latch_);
            page = &pages_[free_frame_id];
        }
        if (page->IsDirty()) {
            disk_manager_->WritePage(page_id, page->GetData());
        }
        {
            std::lock_guard<std::mutex> lock(latch_);
            page_table_.erase(page->GetPageId());
        }
        page->Reset();

        disk_manager_->ReadPage(page_id, page->GetData());
        page->SetPageId(page_id);
        page->IncrementPinCount();
        page->SetDirty(false);

        {
            std::lock_guard<std::mutex> lock(latch_);
            page_table_[page_id] = free_frame_id;
        }
        return page;
    }
    return nullptr;

}

// ============================================================
// TODO: 你来实现
// ============================================================
bool BufferPoolManager::UnpinPage(page_id_t page_id, bool is_dirty) {
    // std::lock_guard<std::mutex> lock(latch_);
    Page *page = nullptr;
    frame_id_t frame_id = 0;
    {
        std::lock_guard<std::mutex> lock(latch_);
        auto iter = page_table_.find(page_id);
        if (iter == page_table_.end()) {
            return false;
        }
        frame_id = iter->second;
        page = &pages_[frame_id];
    }
    if (is_dirty) {
        page->SetDirty(true);
    }
    page->DecrementPinCount();
    if (page->GetPinCount() == 0) {
        replacer_->Unpin(frame_id);
        disk_manager_->WritePage(page_id, page->GetData());
    }
    return true;
}

// ============================================================
// TODO: 你来实现
// ============================================================
Page* BufferPoolManager::NewPage(page_id_t* page_id) {
    // std::lock_guard<std::mutex> lock(latch_);
    //
    // 参考提示见 buffer_pool_manager.h 中的注释
    frame_id_t free_frame_id = 0;
    if (!FindFreeFrame(&free_frame_id)) {
        return nullptr;
    }
    page_id_t &new_page_id = *page_id;
    new_page_id = disk_manager_->AllocatePage();
    Page *page = new Page();
    page->SetPageId(new_page_id);
    page->SetDirty(false);
    {
        std::lock_guard<std::mutex> lock(latch_);
        page_table_[new_page_id] = free_frame_id;
    }
    return page;
}

// ============================================================
// TODO: 你来实现
// ============================================================
bool BufferPoolManager::FlushPage(page_id_t page_id) {
    // std::lock_guard<std::mutex> lock(latch_);
    //
    // 实现提示：
    //   1. page_id == INVALID_PAGE_ID → 返回 false
    //   2. 在 page_table_ 中查找帧
    //   3. 调用 disk_manager_->WritePage() 写回
    //   4. 清除脏标记
    //   5. 更新页面 LSN（可选）
    if (page_id == INVALID_PAGE_ID) {
        return false;
    }
    frame_id_t frame_id = 0;
    {
        std::lock_guard<std::mutex> lock(latch_);
        auto iter = page_table_.find(page_id);
        if (iter == page_table_.end()) {
            return false;
        }
        frame_id = iter->second;
    }
    Page *page = &pages_[frame_id];
    disk_manager_->WritePage(page_id, page->GetData());
    pages_->SetDirty(false);
    return true;
}

void BufferPoolManager::FlushAllPages() {
    std::lock_guard<std::mutex> lock(latch_);
    for (auto& [page_id, frame_id] : page_table_) {
        if (pages_[frame_id].IsDirty()) {
            disk_manager_->WritePage(page_id, pages_[frame_id].GetData());
            pages_[frame_id].SetDirty(false);
        }
    }
}

// ============================================================
// TODO: 你来实现
// ============================================================
bool BufferPoolManager::FindFreeFrame(frame_id_t* frame_id) {
    // 参考提示见 buffer_pool_manager.h 中的注释

    throw std::runtime_error("FindFreeFrame: 尚未实现");
}

} // namespace minidb
