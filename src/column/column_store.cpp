#include "column/column_store.h"
#include <stdexcept>

namespace minidb {

void ColumnStore::DefineSchema(const std::string& table_name,
                                const std::vector<ColumnDef>& columns) {
    std::lock_guard<std::mutex> lock(store_mutex_);
    schemas_[table_name] = columns;
    // 为每列初始化空的 chunk 列表
    for (auto& col : columns) {
        column_chunks_[table_name][col.name] = {};
    }
}

// ============================================================
// TODO: 你来实现
// ============================================================
void ColumnStore::BatchInsert(const std::string& table_name,
                               const std::vector<std::vector<char>>& column_data,
                               uint32_t num_rows) {
    // 参考 column_store.h 中的注释
    throw std::runtime_error("BatchInsert: 尚未实现");
}

// ============================================================
// TODO: 你来实现
// ============================================================
std::vector<ColumnChunk> ColumnStore::ProjectScan(
    const std::string& table_name,
    const std::vector<std::string>& column_names) const {
    // 参考 column_store.h 中的注释
    throw std::runtime_error("ProjectScan: 尚未实现");
}

// ============================================================
// TODO: 你来实现
// ============================================================
std::vector<char> ColumnStore::CompressRLE(const char* data, size_t data_len, ColumnType type) {
    // 以 INT32 为例：
    //
    // const int32_t* values = reinterpret_cast<const int32_t*>(data);
    // size_t count = data_len / sizeof(int32_t);
    // std::vector<char> result;
    //
    // size_t i = 0;
    // while (i < count) {
    //     int32_t current_val = values[i];
    //     uint32_t run_length = 1;
    //     while (i + run_length < count && values[i + run_length] == current_val) {
    //         run_length++;
    //     }
    //     // 写入 (value, count) 对
    //     result.insert(result.end(),
    //                   reinterpret_cast<const char*>(&current_val),
    //                   reinterpret_cast<const char*>(&current_val) + sizeof(int32_t));
    //     result.insert(result.end(),
    //                   reinterpret_cast<const char*>(&run_length),
    //                   reinterpret_cast<const char*>(&run_length) + sizeof(uint32_t));
    //     i += run_length;
    // }
    // return result;

    throw std::runtime_error("CompressRLE: 尚未实现");
}

// ============================================================
// TODO: 你来实现
// ============================================================
std::vector<char> ColumnStore::DecompressRLE(const char* data, size_t data_len, ColumnType type) {
    // CompressRLE 的逆过程
    throw std::runtime_error("DecompressRLE: 尚未实现");
}

// ============================================================
// TODO: 你来实现
// ============================================================
std::vector<char> ColumnStore::CompressDictionary(
    const char* data, size_t data_len, std::vector<std::string>& dictionary) {
    // 参考 column_store.h 中的注释
    throw std::runtime_error("CompressDictionary: 尚未实现");
}

// ============================================================
// TODO: 你来实现
// ============================================================
CompressionType ColumnStore::AutoSelectCompression(const ColumnChunk& chunk) {
    // 参考 column_store.h 中的注释
    throw std::runtime_error("AutoSelectCompression: 尚未实现");
}

} // namespace minidb
