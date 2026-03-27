#pragma once

#include "common/types.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>

namespace minidb {

// ============================================================
// ColumnStore - 列式存储引擎
// ============================================================
//
// 【面试核心知识点 - 列存 vs 行存】
//
// 行存（Row Store）：
//   存储方式：| col1_row1 | col2_row1 | col3_row1 | col1_row2 | ...
//   优势：整行读写快（OLTP场景，如 SELECT * WHERE id=1）
//   代表：PostgreSQL（Heap Table）、MySQL InnoDB
//
// 列存（Column Store）：
//   存储方式：| col1_row1 | col1_row2 | col1_row3 | ... | col2_row1 | col2_row2 | ...
//   优势：
//     1. 分析查询只读需要的列（减少I/O）
//     2. 同类型数据聚集 → 压缩效果极好（10x-100x）
//     3. SIMD 向量化处理加速
//   代表：ClickHouse、DuckDB、Parquet格式
//
// HTAP（混合负载）趋势：
//   同一个数据库同时支持 OLTP + OLAP
//   - TiDB: TiKV(行存) + TiFlash(列存)
//   - PostgreSQL: Hydra / cstore_fdw 列存扩展
//   - 我们的设计: RowStore(默认) + ColumnStore(分析查询)
//

// 列的数据类型（简化版）
enum class ColumnType : uint8_t {
    INT32 = 0,
    INT64,
    FLOAT64,
    VARCHAR,  // 变长字符串
};

// 列定义
struct ColumnDef {
    std::string name;
    ColumnType type;
    bool nullable{true};
};

// ============================================================
// 压缩编码类型
// ============================================================
//
// 【面试加分项 - 列存压缩技术】
//
// 1. RLE (Run-Length Encoding)：
//    连续相同值只存一次 + 计数
//    例：[A,A,A,B,B] → [(A,3),(B,2)]
//    适合：低基数列（如性别、状态）
//
// 2. Dictionary Encoding：
//    建立字典映射，用短编码替代原值
//    例：["北京","上海","北京"] → 字典{0:"北京",1:"上海"}, 数据[0,1,0]
//    适合：字符串列、中低基数列
//
// 3. Delta Encoding：
//    存储相邻值的差值
//    例：[100,101,103,104] → [100,1,2,1]
//    适合：时间戳、自增ID
//
// 4. Bit-Packing：
//    用尽量少的 bit 表示值
//    例：值范围[0,7]只需3个bit，而不是32bit的int
//
enum class CompressionType : uint8_t {
    NONE = 0,       // 不压缩
    RLE,            // Run-Length Encoding
    DICTIONARY,     // 字典编码
    DELTA,          // 差值编码
    // BIT_PACKING, // 位压缩（可选实现）
};

// ============================================================
// ColumnChunk - 列数据块
// ============================================================
// 一个 ColumnChunk 存储某一列的一批行数据
//
struct ColumnChunk {
    ColumnType type;
    CompressionType compression{CompressionType::NONE};
    uint32_t num_rows{0};

    // 原始数据（压缩前或解压后）
    std::vector<char> raw_data;

    // 压缩后的数据
    std::vector<char> compressed_data;

    // NULL 位图（第 i 个 bit 为 1 表示第 i 行为 NULL）
    std::vector<uint8_t> null_bitmap;
};

class ColumnStore {
public:
    ColumnStore() = default;
    ~ColumnStore() = default;

    // 定义表结构
    void DefineSchema(const std::string& table_name, const std::vector<ColumnDef>& columns);

    // ============================================================
    // TODO: 你来实现 - 批量插入（列式）
    // ============================================================
    // 以列式格式批量插入数据
    //
    // 参数：
    //   table_name: 表名
    //   column_data: 每列的数据（外层vector对应列，内层vector对应行）
    //   num_rows: 行数
    //
    // 实现步骤：
    //   1. 根据 table_name 获取 schema
    //   2. 对每一列：
    //      a. 创建 ColumnChunk
    //      b. 选择合适的压缩编码（见 AutoSelectCompression）
    //      c. 压缩数据
    //      d. 存储到 column_chunks_ 中
    //
    void BatchInsert(const std::string& table_name,
                     const std::vector<std::vector<char>>& column_data,
                     uint32_t num_rows);

    // ============================================================
    // TODO: 你来实现 - 列投影扫描
    // ============================================================
    // 只读取指定的列（列裁剪/Column Pruning）
    //
    // 这是列存的核心优势：
    //   SELECT col1, col3 FROM table;
    //   行存需要读取所有列再丢弃不需要的 → 大量无效I/O
    //   列存只读取 col1 和 col3 的数据 → 最小化I/O
    //
    // 返回：每列的解压后数据
    //
    std::vector<ColumnChunk> ProjectScan(const std::string& table_name,
                                          const std::vector<std::string>& column_names) const;

    // ============================================================
    // TODO: 你来实现 - RLE 压缩
    // ============================================================
    // 对 int32 列数据进行 Run-Length Encoding 压缩
    //
    // 输入：[1,1,1,2,2,3,3,3,3]
    // 输出格式：| value(4B) | count(4B) | value(4B) | count(4B) | ...
    // 输出：[(1,3), (2,2), (3,4)]
    //
    static std::vector<char> CompressRLE(const char* data, size_t data_len, ColumnType type);

    // ============================================================
    // TODO: 你来实现 - RLE 解压
    // ============================================================
    static std::vector<char> DecompressRLE(const char* data, size_t data_len, ColumnType type);

    // ============================================================
    // TODO: 你来实现 - 字典编码压缩
    // ============================================================
    // 对 VARCHAR 列进行字典编码
    //
    // 输入：["apple","banana","apple","cherry","banana"]
    // 输出：
    //   字典: {0:"apple", 1:"banana", 2:"cherry"}
    //   编码: [0, 1, 0, 2, 1]
    //
    static std::vector<char> CompressDictionary(const char* data, size_t data_len,
                                                  std::vector<std::string>& dictionary);

    // ============================================================
    // TODO: 你来实现 - 自动选择压缩算法
    // ============================================================
    // 根据数据特征自动选择最佳压缩算法
    //
    // 启发式规则：
    //   - 如果连续重复值多（重复率>50%）→ RLE
    //   - 如果是字符串且基数低（unique值/总行数 < 0.3）→ Dictionary
    //   - 如果是数值且单调递增/递减 → Delta
    //   - 其他情况 → NONE
    //
    static CompressionType AutoSelectCompression(const ColumnChunk& chunk);

private:
    // 表名 → 列定义
    std::unordered_map<std::string, std::vector<ColumnDef>> schemas_;

    // 表名 → 列名 → ColumnChunk列表（每个chunk存一批行）
    std::unordered_map<std::string,
        std::unordered_map<std::string, std::vector<ColumnChunk>>> column_chunks_;

    mutable std::mutex store_mutex_;
};

} // namespace minidb
