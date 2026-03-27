#include "network/page_server.h"
#include <stdexcept>
#include <cstring>

namespace minidb {

PageServer::PageServer(const std::string& data_dir) : data_dir_(data_dir) {}

PageServer::~PageServer() {
    Stop();
}

// ============================================================
// TODO: 你来实现
// ============================================================
void PageServer::HandleGetPage(StorageRequest& request) {
    // std::lock_guard<std::mutex> lock(page_mutex_);
    //
    // 模拟 Neon Pageserver 的 get_page_at_lsn:
    //
    // 1. 查找基础页面
    //    auto it = base_images_.find(request.page_id);
    //    char page_buf[PAGE_SIZE] = {0};
    //    if (it != base_images_.end()) {
    //        memcpy(page_buf, it->second.data(), PAGE_SIZE);
    //    }
    //
    // 2. 获取需要回放的 WAL 记录
    //    auto wal_it = page_wal_index_.find(request.page_id);
    //    if (wal_it != page_wal_index_.end()) {
    //        for (auto& record : wal_it->second) {
    //            if (record.lsn_ <= request.lsn) {
    //                // 回放: 将 new_data 应用到 page_buf 的 offset 位置
    //                memcpy(page_buf + record.offset_,
    //                       record.new_data_, record.new_data_len_);
    //            }
    //        }
    //    }
    //
    // 3. 返回结果
    //    memcpy(request.page_data, page_buf, PAGE_SIZE);
    //    request.success = true;

    throw std::runtime_error("HandleGetPage: 尚未实现");
}

// ============================================================
// TODO: 你来实现
// ============================================================
void PageServer::HandlePutWAL(StorageRequest& request) {
    // 1. 解析 WAL 数据
    //    const LogRecord* record = reinterpret_cast<const LogRecord*>(request.wal_data.data());
    //
    // 2. 存储到 WAL 索引
    //    {
    //        std::lock_guard<std::mutex> lock(page_mutex_);
    //        page_wal_index_[record->page_id_].push_back(*record);
    //    }
    //
    // 3. 加入回放队列
    //    {
    //        std::lock_guard<std::mutex> lock(wal_mutex_);
    //        wal_replay_queue_.push(*record);
    //        wal_cv_.notify_one();
    //    }
    //
    // 4. request.success = true;

    throw std::runtime_error("HandlePutWAL: 尚未实现");
}

// ============================================================
// TODO: 你来实现
// ============================================================
void PageServer::BackgroundRedoLoop() {
    // while (running_) {
    //     std::unique_lock<std::mutex> lock(wal_mutex_);
    //     wal_cv_.wait(lock, [this] {
    //         return !wal_replay_queue_.empty() || !running_;
    //     });
    //
    //     while (!wal_replay_queue_.empty()) {
    //         LogRecord record = wal_replay_queue_.front();
    //         wal_replay_queue_.pop();
    //         lock.unlock();
    //
    //         // 回放 WAL 到基础页面
    //         std::lock_guard<std::mutex> page_lock(page_mutex_);
    //         auto& base = base_images_[record.page_id_];
    //         if (base.empty()) base.resize(PAGE_SIZE, 0);
    //         memcpy(base.data() + record.offset_,
    //                record.new_data_, record.new_data_len_);
    //
    //         lock.lock();
    //     }
    // }
    //
    // 【思考】后台 redo 的频率如何控制？
    //   - 太频繁：CPU开销大
    //   - 太慢：GetPage 时需要回放更多 WAL
    //   - Aurora 的做法：后台持续 redo，GetPage 时只需返回缓存的最新页面

    throw std::runtime_error("BackgroundRedoLoop: 尚未实现");
}

void PageServer::Start() {
    running_ = true;
    redo_thread_ = std::thread([this] { BackgroundRedoLoop(); });
}

void PageServer::Stop() {
    running_ = false;
    wal_cv_.notify_all();
    if (redo_thread_.joinable()) {
        redo_thread_.join();
    }
}

// ============================================================
// RPCClient 实现
// ============================================================

RPCClient::RPCClient(PageServer* page_server) : page_server_(page_server) {}

// ============================================================
// TODO: 你来实现
// ============================================================
bool RPCClient::FetchPageRemote(page_id_t page_id, lsn_t lsn, char* page_data) {
    // 简化实现（本地调用模拟RPC）：
    //
    // StorageRequest request;
    // request.type = StorageRequestType::GET_PAGE;
    // request.page_id = page_id;
    // request.lsn = lsn;
    //
    // page_server_->HandleGetPage(request);
    //
    // if (request.success) {
    //     memcpy(page_data, request.page_data, PAGE_SIZE);
    //     return true;
    // }
    // return false;
    //
    // 【真实系统中】这里会序列化请求 → 通过 TCP/gRPC 发送 → 反序列化响应

    throw std::runtime_error("FetchPageRemote: 尚未实现");
}

// ============================================================
// TODO: 你来实现
// ============================================================
bool RPCClient::SendWAL(const std::vector<char>& wal_data, tenant_id_t tenant_id) {
    // StorageRequest request;
    // request.type = StorageRequestType::PUT_WAL;
    // request.wal_data = wal_data;
    // request.tenant_id = tenant_id;
    //
    // page_server_->HandlePutWAL(request);
    // return request.success;

    throw std::runtime_error("SendWAL: 尚未实现");
}

} // namespace minidb
