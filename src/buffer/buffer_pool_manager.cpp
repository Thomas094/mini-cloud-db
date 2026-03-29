#include "buffer/buffer_pool_manager.h"
#include "common/types.h"
#include "storage/page.h"
#include <mutex>
#include <stdexcept>
#include <vector>

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
    frame_id_t free_frame_id = 0;
    Page* page = nullptr;
    {
        std::unique_lock<std::mutex> lock(latch_);
        if (page_table_.find(page_id) != page_table_.end()) {
            frame_id_t frame_id = page_table_[page_id];
            page = &pages_[frame_id];
            // 如果页面正在被其他线程加载，等待加载完成
            // wait 会自动释放 latch_，被唤醒后重新获取
            page->WaitUntilReady(lock);
            page->IncrementPinCount();
            replacer_->Pin(frame_id);
            return page;
        }
        if (!FindFreeFrame(&free_frame_id)) {
            return nullptr;
        }
        page = &pages_[free_frame_id];
        page_id_t old_page_id = page->GetPageId();
        if (old_page_id != INVALID_PAGE_ID) {
            page_table_.erase(old_page_id);
        }
        // 提前建映射，但标记为 loading 状态
        // 其他线程命中后会等待 loading 完成，不会读到不完整的数据
        page_table_[page_id] = free_frame_id;
        replacer_->Pin(free_frame_id);
        page->SetLoading(true);
    }
    {
        std::lock_guard<std::shared_mutex> lock(page->GetPageLatch());
        // 如果是脏页，写回磁盘
        if (page->IsDirty()) {
            disk_manager_->WritePage(page->GetPageId(), page->GetData());
        }
        page->Reset();
        disk_manager_->ReadPage(page_id, page->GetData());
        page->SetPageId(page_id);
        page->IncrementPinCount();
        page->SetDirty(false);
        // LSN 已嵌入 data_ 头部的 PageHeader 中，
        // ReadPage 读入 data_ 后 LSN 自动恢复，无需手动设置。
    }
    {
        // 加载完成，清除 loading 标记并唤醒等待线程
        std::lock_guard<std::mutex> lock(latch_);
        page->SetLoading(false);
        page->NotifyReady();
    }
    return page;
}

// ============================================================
// TODO: 你来实现
// ============================================================
bool BufferPoolManager::UnpinPage(page_id_t page_id, bool is_dirty) {
    Page *page = nullptr;
    frame_id_t frame_id = 0;
    {
        std::unique_lock<std::mutex> lock(latch_);
        auto iter = page_table_.find(page_id);
        if (iter == page_table_.end()) {
            return false;
        }
        frame_id = iter->second;
        page = &pages_[frame_id];
        // 等待页面加载完成后再 Unpin
        page->WaitUntilReady(lock);
    }
    bool need_unpin = false;
    {
        std::lock_guard<std::shared_mutex> lock(page->GetPageLatch());
        if (page->GetPageId() != page_id) {
            return false;
        }
        if (is_dirty) {
            page->SetDirty(true);
        }
        if (page->GetPinCount() > 0) {
            page->DecrementPinCount();
        } else {
            return false;
        }
        need_unpin = page->GetPinCount() == 0;
    }
    if (need_unpin) {
        std::lock_guard<std::mutex> lock(latch_);
        replacer_->Unpin(frame_id);
    }
    return true;
}

// ============================================================
// TODO: 你来实现
// ============================================================
Page* BufferPoolManager::NewPage(page_id_t* page_id) {
    frame_id_t free_frame_id = 0;
    Page *page = nullptr;
    page_id_t new_page_id = INVALID_PAGE_ID;
    {
        std::lock_guard<std::mutex> lock(latch_);
        if (!FindFreeFrame(&free_frame_id)) {
            return nullptr;
        }
        page = &pages_[free_frame_id];
        page_id_t old_page_id = page->GetPageId();
        if (old_page_id != INVALID_PAGE_ID) {
            page_table_.erase(old_page_id);
        }
        // NewPage 不需要 loading 机制：
        // 新页面不需要从磁盘加载，只需在数据准备好后再写入 page_table_，
        // 在此之前其他线程通过 page_table_ 查不到该页面，天然不可见。
        new_page_id = disk_manager_->AllocatePage();
        *page_id = new_page_id;
        replacer_->Pin(free_frame_id);
    }
    {
        std::lock_guard<std::shared_mutex> lock(page->GetPageLatch());
        // 如果是脏页，写回磁盘
        if (page->IsDirty()) {
            disk_manager_->WritePage(page->GetPageId(), page->GetData());
        }
        page->Reset();
        page->SetPageId(new_page_id);
        page->SetDirty(false);
        // Reset() 已将 data_ 全部清零，PageHeader 中的 LSN 也被重置为 0。
        page->IncrementPinCount();
    }
    {
        // 数据就绪后才建立映射，其他线程此时才能看到该页面
        std::lock_guard<std::mutex> lock(latch_);
        page_table_[new_page_id] = free_frame_id;
    }
    return page;
}

// ============================================================
// TODO: 你来实现
// ============================================================
bool BufferPoolManager::FlushPage(page_id_t page_id) {
    if (page_id == INVALID_PAGE_ID) {
        return false;
    }
    frame_id_t frame_id = 0;
    {
        std::unique_lock<std::mutex> lock(latch_);
        auto iter = page_table_.find(page_id);
        if (iter == page_table_.end()) {
            return false;
        }
        frame_id = iter->second;
        Page *page = &pages_[frame_id];
        // 等待页面加载完成后再 Flush
        page->WaitUntilReady(lock);
    }
    Page *page = &pages_[frame_id];
    std::lock_guard<std::shared_mutex> lock(page->GetPageLatch());
    {
        if (page->GetPageId() != page_id) {
            return false;
        }
        disk_manager_->WritePage(page->GetPageId(), page->GetData());
        page->SetDirty(false);
        // LSN 由 WAL 模块在修改页面时设置，FlushPage 只负责刷盘，
        // 不应修改 LSN（LSN 已嵌入 data_ 中，会随 WritePage 一起持久化）。
    }
    return true;
}

void BufferPoolManager::FlushAllPages() {
    std::unordered_map<page_id_t, frame_id_t> page_table;
    std::vector<Page*> page_list;
    {
        std::lock_guard<std::mutex> lock(latch_);
        for (auto [page_id, frame_id] : page_table_) {
            page_list.push_back(&pages_[frame_id]);
        }
    }
    for (auto* page : page_list) {
        if (page->IsDirty()) {
            std::lock_guard<std::shared_mutex> lock(page->GetPageLatch());
            if (page->IsDirty()) {
                disk_manager_->WritePage(page->GetPageId(), page->GetData());
                page->SetDirty(false);
            }
        }
    }
}

bool BufferPoolManager::FindFreeFrame(frame_id_t* frame_id) {
    if (!free_list_.empty()) {
        *frame_id = free_list_.front();
        free_list_.pop_front();
        return true;
    }
    if (replacer_->Evict(frame_id)) {
        return true;
    }
    return false;
}

} // namespace minidb
