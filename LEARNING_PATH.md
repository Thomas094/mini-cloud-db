# Mini Cloud-Native Database — 14 天学习路径

> **目标岗位**：数据库内核 / 分布式系统研发  
> **前置技能**：6 年 C++ 业务开发经验  
> **项目总量**：10 个模块，3,800+ 行骨架代码，**47 个 TODO 函数**需要你亲手实现  
> **编译方式**：
> ```bash
> source /opt/rh/devtoolset-11/enable   # 启用 GCC 11
> cd mini-cloud-db/build
> cmake .. && make -j$(nproc)
> ./integration_test                     # 运行集成测试
> ```

---

## 整体架构总览

```
┌─────────────────────────────────────────────────────────┐
│                    Client / SQL Layer                     │
├──────────┬──────────┬───────────┬────────────────────────┤
│  B+Tree  │  Column  │  MVCC     │  Distributed Txn (2PC) │
│  Index   │  Store   │  Manager  │  Coordinator           │
├──────────┴──────────┴───────────┴────────────────────────┤
│              Buffer Pool Manager                          │
│         ┌─────────────┬──────────────┐                   │
│         │ LRU Replacer │  Page Table  │                   │
│         └─────────────┴──────────────┘                   │
├───────────────────┬──────────────────────────────────────┤
│   WAL Manager     │   Disk Manager / io_uring Engine      │
├───────────────────┴──────────────────────────────────────┤
│           PageServer (存储计算分离 RPC)                    │
├──────────────────────────────────────────────────────────┤
│   Tiered Storage (冷热分层)  │  Tenant Manager (多租户)   │
└──────────────────────────────────────────────────────────┘
```

---

## 阶段 1：磁盘 I/O 层（第 1 天）

### 🎯 目标
掌握数据库最底层的页面读写机制，理解 `pread`/`pwrite` 的原子性优势。

### 📁 涉及文件
| 文件 | 说明 |
|------|------|
| `src/storage/disk_manager.h` | 接口定义 + 知识点注释 |
| `src/storage/disk_manager.cpp` | **你的实现文件** |

### ✏️ TODO 清单（3 个函数）

| # | 函数 | 难度 | 说明 |
|---|------|------|------|
| 1 | `DiskManager::ReadPage()` | ⭐ | 使用 `pread()` 读取指定页面 |
| 2 | `DiskManager::WritePage()` | ⭐ | 使用 `pwrite()` 写入，处理 short write |
| 3 | `DiskManager::AllocatePage()` | ⭐ | 在文件末尾分配新页面 |

### 🔑 核心知识点
- `pread`/`pwrite` 是线程安全的（不共享文件偏移），而 `lseek + read` 不是
- 页面偏移公式：`offset = page_id * PAGE_SIZE`（8KB）
- PostgreSQL 对应代码：`src/backend/storage/smgr/md.c`

### ✅ 验证方法
运行集成测试中的 `TestDiskManager`：
```bash
./integration_test 2>&1 | grep -A 2 "DiskManager"
```
**预期结果**：
```
=== TEST: DiskManager 基本读写 ===
✅ PASS: DiskManager 基本读写
```

### 📝 自测清单
- [ ] 写入一个页面后读回，数据完全一致
- [ ] 连续分配 10 个页面，page_id 依次为 0-9
- [ ] 多线程并发 ReadPage 不会互相干扰
- [ ] 写入不足 `PAGE_SIZE` 字节时正确处理 short write

---

## 阶段 2：LRU 页面替换（第 2 天）

### 🎯 目标
实现经典的 LRU 缓存淘汰策略，掌握 **链表 + 哈希表** 的 O(1) 设计模式。

### 📁 涉及文件
| 文件 | 说明 |
|------|------|
| `src/buffer/lru_replacer.h` | 接口定义 |
| `src/buffer/lru_replacer.cpp` | **你的实现文件** |

### ✏️ TODO 清单（4 个函数）

| # | 函数 | 难度 | 说明 |
|---|------|------|------|
| 1 | `LRUReplacer::Evict()` | ⭐⭐ | 淘汰链表尾部（最久未使用） |
| 2 | `LRUReplacer::Unpin()` | ⭐⭐ | 帧可淘汰时加入链表头部 |
| 3 | `LRUReplacer::Pin()` | ⭐ | 帧被使用时从链表移除 |
| 4 | `LRUReplacer::Size()` | ⭐ | 返回可淘汰帧数 |

### 🔑 核心知识点
- `std::list` + `std::unordered_map<frame_id, list::iterator>` 实现 O(1) 查找+删除
- PostgreSQL 实际使用 **Clock-Sweep** 算法（近似 LRU，更适合高并发）
- 面试高频题：为什么不用纯链表？为什么 Clock-Sweep 比 LRU 好？

### ✅ 验证方法
```bash
./integration_test 2>&1 | grep -A 2 "LRU"
```
**预期结果**：
```
=== TEST: LRU Replacer ===
✅ PASS: LRU Replacer
```

### 📝 自测清单
- [ ] Unpin(0), Unpin(1), Unpin(2) 后 Size() == 3
- [ ] Pin(1) 后 Size() == 2
- [ ] Evict 返回 0（最久未使用）
- [ ] 空的 Replacer 调用 Evict 返回 false
- [ ] 重复 Unpin 同一个帧不会导致重复插入

---

## 阶段 3：Buffer Pool Manager（第 3-4 天）

### 🎯 目标
实现数据库内存管理的**核心枢纽**，这是面试中问的最多的组件之一。

### 📁 涉及文件
| 文件 | 说明 |
|------|------|
| `src/buffer/buffer_pool_manager.h` | 接口定义 + 详细流程注释 |
| `src/buffer/buffer_pool_manager.cpp` | **你的实现文件** |

### ✏️ TODO 清单（5 个函数）

| # | 函数 | 难度 | 说明 |
|---|------|------|------|
| 1 | `FindFreeFrame()` | ⭐⭐ | 优先从 free_list 取，否则调 Evict |
| 2 | `FetchPage()` | ⭐⭐⭐ | **最核心**：查表→命中返回 / 未命中→找帧→淘汰脏页→磁盘读取 |
| 3 | `NewPage()` | ⭐⭐ | 分配新页 + 注册到 page_table |
| 4 | `UnpinPage()` | ⭐⭐ | pin_count-- + 脏页标记（注意不能清除已有脏标记！） |
| 5 | `FlushPage()` | ⭐ | 强制刷盘 |

### 🔑 核心知识点
- **page_table_**（hash map）是 Buffer Pool 的核心索引，类似 PG 的 `buf_table`
- `UnpinPage` 的易错点：`if (is_dirty) page->SetDirty(true)` 而不是 `page->SetDirty(is_dirty)`
- PostgreSQL 通过分区 hash table + LWLock 减少锁竞争
- 对应 PG 代码：`src/backend/storage/buffer/bufmgr.c` → `ReadBuffer()`

### ✅ 验证方法
```bash
./integration_test 2>&1 | grep -A 2 "Buffer Pool"
```
**预期结果**：
```
=== TEST: Buffer Pool Manager ===
✅ PASS: Buffer Pool Manager
```

### 📝 自测清单
- [ ] NewPage 创建页面后写入数据，UnpinPage(dirty=true)，再 FetchPage 数据一致
- [ ] 创建超过 pool_size 个页面时，旧页面被正确淘汰
- [ ] 所有帧都被 pin 住时，FetchPage 返回 nullptr
- [ ] 脏页淘汰时数据正确写回磁盘
- [ ] FlushAllPages 后所有页面 dirty 标记清除

### 💡 扩展练习
完成基础实现后，尝试：
1. 将 LRUReplacer 替换为 **Clock-Sweep** 算法
2. 实现 **分区 page_table**（将 hash 表分成 N 个分区，各自加锁）

---

## 阶段 4：WAL 日志管理（第 5 天）

### 🎯 目标
掌握数据库持久性（Durability）的根基——Write-Ahead Logging。

### 📁 涉及文件
| 文件 | 说明 |
|------|------|
| `src/recovery/log_record.h` | ✅ 已完成（WAL 记录格式定义） |
| `src/recovery/wal_manager.h` | 接口定义 |
| `src/recovery/wal_manager.cpp` | **你的实现文件** |

### ✏️ TODO 清单（3 个函数）

| # | 函数 | 难度 | 说明 |
|---|------|------|------|
| 1 | `WALManager::AppendLog()` | ⭐⭐ | 分配 LSN + 序列化到缓冲区 |
| 2 | `WALManager::Flush()` | ⭐⭐ | write + fsync 刷盘（持久性关键！） |
| 3 | `WALManager::Recover()` | ⭐⭐⭐ | 读取日志 → Redo 重放 |

### 🔑 核心知识点
- **WAL 黄金法则**：先写日志，再写数据；提交前必须 fsync 日志
- **Group Commit**：多个事务日志合并一次 fsync，摊薄开销
- **ARIES 恢复三阶段**：Analysis → Redo → Undo
- Aurora 的 "Log is the Database"：存储节点只接收 WAL，异步重建页面

### ✅ 验证方法
**手动测试**（集成测试中暂无独立 WAL 测试，需自行编写）：
```cpp
// 在 test/ 下新建 test_wal.cpp
WALManager wal("/tmp/test_wal.log");
LogRecord rec = LogRecord::MakeTxnRecord(LogRecordType::TXN_BEGIN, 1, INVALID_LSN);
lsn_t lsn1 = wal.AppendLog(rec);
assert(lsn1 == 1);
wal.Flush();
assert(wal.GetFlushedLSN() == wal.GetCurrentLSN());
```

### 📝 自测清单
- [ ] AppendLog 返回单调递增的 LSN
- [ ] Flush 后 WAL 文件大小 > 0
- [ ] 缓冲区满时自动触发 Flush
- [ ] Recover 能读取之前写入的所有日志记录
- [ ] 理解：为什么 `fsync` 是性能瓶颈？Group Commit 如何优化？

---

## 阶段 5：MVCC 多版本并发控制（第 6-7 天）

### 🎯 目标
实现面试中**出现频率最高**的数据库内核知识点——MVCC 快照隔离。

### 📁 涉及文件
| 文件 | 说明 |
|------|------|
| `src/concurrency/mvcc.h` | 版本链 + 快照 + 管理器定义 |
| `src/concurrency/mvcc.cpp` | **你的实现文件** |

### ✏️ TODO 清单（5 个函数）

| # | 函数 | 难度 | 说明 |
|---|------|------|------|
| 1 | `Snapshot::IsVisible()` | ⭐⭐ | 判断事务 ID 对快照是否可见 |
| 2 | `MVCCManager::BeginTransaction()` | ⭐⭐ | 分配事务 ID + 创建快照 |
| 3 | `MVCCManager::CommitTransaction()` | ⭐ | 标记提交 + 移出活跃列表 |
| 4 | `MVCCManager::AbortTransaction()` | ⭐ | 标记回滚 + 清理 |
| 5 | `MVCCManager::IsVersionVisible()` | ⭐⭐⭐⭐ | **最核心**：xmin/xmax 可见性判断 |

### 🔑 核心知识点

**快照可见性规则**（面试必背）：
```
事务 xid 对快照 S 可见的条件：
  ① xid < S.xmin           → 可见（在快照前已提交）
  ② xid >= S.xmax          → 不可见（在快照后开始）
  ③ xid ∈ S.active_txns    → 不可见（快照时还在运行）
  ④ 其他                   → 可见（在快照前已提交）
```

**元组可见性判断**（PG 风格）：
```
Step1: 检查 xmin（创建者）
  - xmin 已回滚 → 不可见
  - xmin 对快照不可见 → 不可见

Step2: 检查 xmax（删除者）
  - xmax 无效(0) → 可见（没被删）
  - xmax 已回滚 → 可见（删除被撤回）
  - xmax 对快照不可见 → 可见（删除还没"生效"）
  - 否则 → 不可见（已被删除）
```

**PG vs MySQL MVCC 对比**：
| 特性 | PostgreSQL | MySQL InnoDB |
|------|-----------|-------------|
| 旧版本存储 | 原表中（需 VACUUM 清理） | Undo Log 中 |
| 版本链方向 | 新→旧 | 新→旧（通过 roll_ptr） |
| 快照实现 | xmin/xmax/xip 数组 | ReadView + trx_id |

### ✅ 验证方法
```bash
./integration_test 2>&1 | grep -A 2 "MVCC"
```
**预期结果**：
```
=== TEST: MVCC 可见性 ===
✅ PASS: MVCC 可见性
```

### 📝 自测清单
- [ ] txn1 创建的版本，txn2（快照在 txn1 提交前）看不到
- [ ] txn1 提交后，txn3（新快照）能看到
- [ ] txn1 回滚后，所有新事务看不到其创建的版本
- [ ] txn 自己创建再删除的版本，自己看不到
- [ ] 能用语言解释：Read Committed 和 Repeatable Read 的快照取法有什么区别？

---

## 阶段 6：B+Tree 索引（第 8-9 天）

### 🎯 目标
实现关系数据库最经典的索引结构，掌握分裂与合并算法。

### 📁 涉及文件
| 文件 | 说明 |
|------|------|
| `src/index/b_plus_tree.h` | 节点结构 + 接口定义 |
| `src/index/b_plus_tree.cpp` | **你的实现文件** |

### ✏️ TODO 清单（5 个函数）

| # | 函数 | 难度 | 说明 |
|---|------|------|------|
| 1 | `FindLeafNode()` | ⭐⭐ | 从根沿内部节点向下搜索 |
| 2 | `Search()` | ⭐⭐ | 定位叶子 → 二分查找 |
| 3 | `Insert()` | ⭐⭐⭐⭐ | **最复杂**：插入 + 分裂 + 递归上推 |
| 4 | `Delete()` | ⭐⭐⭐ | 删除 + 借/合并 + 递归处理 |
| 5 | `RangeScan()` | ⭐⭐ | 沿叶子链表顺序扫描 |

### 🔑 核心知识点
- **叶子分裂** vs **内部节点分裂**：叶子上推 key 的副本，内部节点上推 key 被移走
- **并发 B+Tree**：Latch Crabbing（螃蟹锁协议）
- B+Tree vs B-Tree：所有数据都在叶子 + 叶子链表 → 范围查询更高效
- PostgreSQL 使用 **Lehman-Yao** 算法（带 right-link 的 B+Tree）

### 🏗️ 建议分步实现

```
第 8 天上午：FindLeafNode + Search（只读路径）
第 8 天下午：Insert（不含分裂，先处理叶子未满的场景）
第 9 天上午：Insert（叶子分裂 + 内部节点分裂）
第 9 天下午：Delete + RangeScan
```

### ✅ 验证方法
```bash
./integration_test 2>&1 | grep -A 2 "B+Tree"
```
**预期结果**：
```
=== TEST: B+Tree 基本操作 ===
✅ PASS: B+Tree 基本操作
```

### 📝 自测清单
- [ ] 插入 100 个 key 后全部能查到
- [ ] 范围查询 [10, 20] 返回 11 个结果
- [ ] 删除 key=42 后查找返回 nullopt
- [ ] 插入超过 MAX_PAIRS 个 key 后触发正确分裂
- [ ] 大量随机删除后树结构仍然正确

---

## 阶段 7：存储计算分离（第 10 天）

### 🎯 目标
实现 Neon/Aurora 架构的核心交互：计算节点发送 WAL，存储节点回放重建页面。

### 📁 涉及文件
| 文件 | 说明 |
|------|------|
| `src/network/page_server.h` | PageServer + RPCClient 定义 |
| `src/network/page_server.cpp` | **你的实现文件** |

### ✏️ TODO 清单（5 个函数）

| # | 函数 | 难度 | 说明 |
|---|------|------|------|
| 1 | `PageServer::HandleGetPage()` | ⭐⭐⭐ | 查基础页面 → 回放 WAL → 返回重建页面 |
| 2 | `PageServer::HandlePutWAL()` | ⭐⭐ | 接收 WAL → 持久化 → 更新索引 |
| 3 | `PageServer::BackgroundRedoLoop()` | ⭐⭐⭐ | 后台线程消费 WAL 队列持续重建页面 |
| 4 | `RPCClient::FetchPageRemote()` | ⭐ | 封装 GetPage 请求 |
| 5 | `RPCClient::SendWAL()` | ⭐ | 封装 PutWAL 请求 |

### 🔑 核心知识点

**Neon vs Aurora 架构对比**（面试高频）：

| 特性 | Neon | Aurora |
|------|------|--------|
| WAL 持久化 | Safekeeper（Paxos 3副本） | Storage（Quorum 4/6） |
| 页面重建 | Pageserver 按需 redo | Storage 后台持续 redo |
| 历史版本 | 保留（支持 PITR） | 不保留（备份到 S3） |
| 冷启动 | 需要重建页面缓存 | 存储层已有最新页面 |
| 网络传输 | WAL only | WAL only（"Log is DB"） |

### ✅ 验证方法
**手动测试**：
```cpp
PageServer ps("/tmp/test_pageserver/");
RPCClient client(&ps);

// 发送 WAL
LogRecord rec = LogRecord::MakeDataRecord(...);
std::vector<char> wal_data(reinterpret_cast<char*>(&rec),
                           reinterpret_cast<char*>(&rec) + sizeof(rec));
client.SendWAL(wal_data, 1);

// 获取页面
char page[PAGE_SIZE];
bool ok = client.FetchPageRemote(rec.page_id_, rec.lsn_, page);
assert(ok);
assert(memcmp(page + rec.offset_, rec.new_data_, rec.new_data_len_) == 0);
```

### 📝 自测清单
- [ ] SendWAL 后 HandleGetPage 能返回包含最新修改的页面
- [ ] 多次 WAL 按 LSN 顺序正确叠加
- [ ] BackgroundRedoLoop 在有 WAL 时唤醒，无 WAL 时休眠
- [ ] Stop() 后后台线程能正确退出
- [ ] 能画出 Neon 架构的数据流图（Compute → Safekeeper → Pageserver → S3）

---

## 阶段 8：列式存储引擎（第 11 天）

### 🎯 目标
实现列存核心：列裁剪扫描 + RLE / 字典编码压缩。

### 📁 涉及文件
| 文件 | 说明 |
|------|------|
| `src/column/column_store.h` | 列存引擎定义 + 压缩算法说明 |
| `src/column/column_store.cpp` | **你的实现文件** |

### ✏️ TODO 清单（6 个函数）

| # | 函数 | 难度 | 说明 |
|---|------|------|------|
| 1 | `BatchInsert()` | ⭐⭐ | 列式批量插入 + 自动压缩 |
| 2 | `ProjectScan()` | ⭐⭐ | 只读指定列（列裁剪） |
| 3 | `CompressRLE()` | ⭐⭐ | Run-Length Encoding 压缩 |
| 4 | `DecompressRLE()` | ⭐ | RLE 解压 |
| 5 | `CompressDictionary()` | ⭐⭐ | 字典编码压缩 |
| 6 | `AutoSelectCompression()` | ⭐⭐ | 根据数据特征自动选算法 |

### 🔑 核心知识点
- **行存 vs 列存**：OLTP 用行存，OLAP 用列存
- **压缩率**：列存同类型数据聚集，压缩率可达 10x-100x
- 列存三大优势：I/O 减少（列裁剪）、高压缩率、SIMD 向量化

### ✅ 验证方法
```cpp
ColumnStore cs;
cs.DefineSchema("test", {{"id", ColumnType::INT32}, {"name", ColumnType::VARCHAR}});

// RLE 压缩测试
int32_t data[] = {1, 1, 1, 2, 2, 3};
auto compressed = ColumnStore::CompressRLE(
    reinterpret_cast<char*>(data), sizeof(data), ColumnType::INT32);
auto decompressed = ColumnStore::DecompressRLE(
    compressed.data(), compressed.size(), ColumnType::INT32);
assert(memcmp(decompressed.data(), data, sizeof(data)) == 0);
```

### 📝 自测清单
- [ ] RLE 压缩后再解压，数据完全一致
- [ ] 字典编码：5 个字符串 3 个 unique → 字典大小为 3
- [ ] ProjectScan 只返回请求的列，不返回其他列
- [ ] AutoSelectCompression 对连续重复数据选择 RLE

---

## 阶段 9：冷热分层存储（第 12 天上午）

### 🎯 目标
实现基于访问频率的自动数据温度评估与迁移策略。

### 📁 涉及文件
| 文件 | 说明 |
|------|------|
| `src/tiered/tiered_storage.h` | 分层策略定义 |
| `src/tiered/tiered_storage.cpp` | **你的实现文件** |

### ✏️ TODO 清单（3 个函数）

| # | 函数 | 难度 | 说明 |
|---|------|------|------|
| 1 | `RecordAccess()` | ⭐ | 记录访问时间 + 计数 |
| 2 | `EvaluateTemperature()` | ⭐⭐ | 根据 last_access_time 判断冷热 |
| 3 | `RunMigration()` | ⭐⭐ | 扫描所有页面，迁移变冷的数据 |

### 🔑 核心知识点
- Neon：热数据在 Pageserver SSD，冷数据自动迁移到 S3
- 迁移过程不能阻塞在线读写（copy-on-write 或双写策略）

### ✅ 验证方法
```cpp
TieredStorage ts;
ts.RecordAccess(1);
ts.RecordAccess(1);
assert(ts.GetTemperature(1) == DataTemperature::HOT);

// 模拟时间流逝（修改策略阈值为0测试）
TieringPolicy policy;
policy.hot_to_warm_seconds = 0;
TieredStorage ts2(policy);
ts2.RecordAccess(2);
// sleep 或 mock 时间后
auto temp = ts2.EvaluateTemperature(2);
```

### 📝 自测清单
- [ ] 刚访问过的页面温度为 HOT
- [ ] 长时间未访问的页面温度降为 WARM → COLD
- [ ] GetStats() 返回正确的各层页面计数
- [ ] RunMigration 不会迁移当前被 pin 住的页面

---

## 阶段 10：多租户资源隔离（第 12 天下午）

### 🎯 目标
实现 Cloud-Native 数据库的资源隔离与配额管理，防止 Noisy Neighbor。

### 📁 涉及文件
| 文件 | 说明 |
|------|------|
| `src/tenant/tenant_manager.h` | 配额与使用量定义 |
| `src/tenant/tenant_manager.cpp` | **你的实现文件** |

### ✏️ TODO 清单（7 个函数）

| # | 函数 | 难度 | 说明 |
|---|------|------|------|
| 1 | `CreateTenant()` | ⭐ | 分配 ID + 初始化配额 |
| 2 | `AcquireMemory()` | ⭐⭐ | 内存配额检查 + 扣减 |
| 3 | `AcquireBufferPages()` | ⭐⭐ | Buffer Pool 配额检查 |
| 4 | `AcquireIOPS()` | ⭐ | IOPS 限流 |
| 5 | `ReleaseMemory()` | ⭐ | 释放内存配额 |
| 6 | `ReleaseBufferPages()` | ⭐ | 释放 Buffer Pool 配额 |
| 7 | `ResetIOPSCounters()` | ⭐ | 周期性重置计数器 |

### 🔑 核心知识点
- `compare_exchange_weak` 循环避免 check-then-act 的 TOCTOU 竞态
- Neon 做法：每个租户独立 PG 进程（进程级隔离）
- Aurora Serverless：容器级隔离 + 弹性伸缩

### ✅ 验证方法
```bash
./integration_test 2>&1 | grep -A 2 "多租户"
```
**预期结果**：
```
=== TEST: 多租户资源隔离 ===
✅ PASS: 多租户资源隔离
```

### 📝 自测清单
- [ ] 租户A申请内存超过配额时返回 false
- [ ] 租户A超额不影响租户B
- [ ] 释放后可以重新申请
- [ ] ResetIOPSCounters 后所有租户 IOPS 计数归零
- [ ] 并发申请资源不会超出配额（原子操作正确）

---

## 阶段 11：分布式事务 2PC（第 13 天）

### 🎯 目标
实现 Two-Phase Commit 协议，理解分布式一致性的核心难题。

### 📁 涉及文件
| 文件 | 说明 |
|------|------|
| `src/distributed/distributed_txn.h` | Coordinator + Participant 定义 |
| `src/distributed/distributed_txn.cpp` | **你的实现文件** |

### ✏️ TODO 清单（4 个函数）

| # | 函数 | 难度 | 说明 |
|---|------|------|------|
| 1 | `Participant::Prepare()` | ⭐⭐ | 检查约束 → 预写 WAL → 投票 YES/NO |
| 2 | `Participant::Commit()` | ⭐ | 写 COMMIT → 释放锁 |
| 3 | `Participant::Abort()` | ⭐ | 回滚本地修改 |
| 4 | `Coordinator::ExecuteTransaction()` | ⭐⭐⭐ | **完整 2PC 流程**：Prepare → 决策 → Commit/Abort |

### 🔑 核心知识点

**2PC 流程图**：
```
Coordinator          Participant_1       Participant_2
    │                     │                    │
    │──── Prepare ───────>│                    │
    │──── Prepare ────────────────────────────>│
    │                     │                    │
    │<──── YES ──────────│                    │
    │<──── YES ───────────────────────────────│
    │                     │                    │
    │  [写入 COMMIT 到 WAL — Point of No Return]
    │                     │                    │
    │──── Commit ────────>│                    │
    │──── Commit ─────────────────────────────>│
    │                     │                    │
    │<──── ACK ──────────│                    │
    │<──── ACK ───────────────────────────────│
```

**面试必答的故障场景**：
| 故障时机 | 处理方案 |
|---------|---------|
| Coordinator 在 Phase 1 后崩溃 | 检查 WAL：有 COMMIT → 继续；无 → Abort |
| Participant 在 Prepare 后崩溃 | 重启后查询 Coordinator 事务最终状态 |
| 网络分区 | 超时 → 视为 NO → 全部 Abort |

### ✅ 验证方法
```bash
./integration_test 2>&1 | grep -A 2 "分布式事务"
```
**预期结果**：
```
=== TEST: 分布式事务 2PC ===
✅ PASS: 分布式事务 2PC
```

### 📝 自测清单
- [ ] 3 个参与者全部 Prepare 成功 → 事务 COMMITTED
- [ ] 模拟某个参与者 Prepare 返回 false → 全部 ABORTED
- [ ] 所有参与者状态一致（全 COMMITTED 或全 ABORTED）
- [ ] 能口述 2PC、3PC、Percolator 的区别

---

## 阶段 12：io_uring 异步 I/O（第 14 天）

### 🎯 目标
用 Linux 最先进的异步 I/O 接口替换同步 I/O，理解高性能存储引擎设计。

### 📁 涉及文件
| 文件 | 说明 |
|------|------|
| `src/storage/io_uring_engine.h` | io_uring 接口定义 + 原理图 |
| `src/storage/io_uring_engine.cpp` | **你的实现文件** |

### 前置条件
```bash
# 安装 liburing
sudo yum install -y liburing-devel   # CentOS
# 或
sudo apt install -y liburing-dev     # Ubuntu
```

### ✏️ TODO 清单（5 个函数）

| # | 函数 | 难度 | 说明 |
|---|------|------|------|
| 1 | 构造函数（初始化） | ⭐⭐ | `io_uring_queue_init()` |
| 2 | `Submit()` | ⭐⭐ | 填充 SQE → 提交单个请求 |
| 3 | `SubmitBatch()` | ⭐⭐⭐ | 批量填充 SQE → 一次 submit |
| 4 | `WaitCompletion()` | ⭐⭐ | 阻塞等待 CQE → 执行回调 |
| 5 | `PollCompletions()` | ⭐⭐ | 非阻塞轮询已完成的请求 |

### 🔑 核心知识点
```
io_uring 工作流程：

  用户态                    内核态
  ┌──────────┐            ┌──────────┐
  │    SQ    │ ───────>   │  执行I/O  │
  │ (提交队列)│            │          │
  └──────────┘            └────┬─────┘
                               │
  ┌──────────┐            ┌────▼─────┐
  │    CQ    │ <───────   │  完成通知  │
  │ (完成队列)│            │          │
  └──────────┘            └──────────┘
```

- 相比传统 I/O：减少系统调用次数（批量提交）、减少上下文切换
- 适合数据库 Buffer Pool 的批量页面预取

### ✅ 验证方法
```cpp
// 需要 liburing 环境
IoUringEngine engine(64);
int fd = open("/tmp/test_uring", O_RDWR | O_CREAT, 0644);
char write_buf[4096] = "io_uring test";
bool done = false;

IoRequest req{IoOpType::WRITE, fd, write_buf, 4096, 0,
              [&](int res) { done = (res >= 0); }};
engine.Submit(req);
engine.WaitCompletion();
assert(done);
```

### 📝 自测清单
- [ ] 单个 Write + Read 数据一致
- [ ] 批量提交 10 个 Read 比逐个 pread 更快
- [ ] PollCompletions 非阻塞，无完成请求时立即返回 0
- [ ] Fsync 请求正确执行
- [ ] 没有 liburing 时 fallback 到同步 I/O 仍能工作

---

## 总进度追踪表

| 阶段 | 模块 | TODO数 | 面试重要度 | 状态 |
|------|------|--------|-----------|------|
| 1 | DiskManager | 3 | ⭐⭐ | ✅ 已完成 |
| 2 | LRU Replacer | 4 | ⭐⭐⭐⭐ | ✅ 已完成 |
| 3 | Buffer Pool Manager | 5 | ⭐⭐⭐⭐⭐ | ✅ 已完成 |
| 4 | WAL Manager | 3 | ⭐⭐⭐⭐⭐ | ✅ 已完成 |
| 5 | MVCC | 5 | ⭐⭐⭐⭐⭐ | ☐ |
| 6 | B+Tree | 5 | ⭐⭐⭐⭐ | ☐ |
| 7 | PageServer（存储计算分离） | 5 | ⭐⭐⭐⭐⭐ | ☐ |
| 8 | ColumnStore（列存） | 6 | ⭐⭐⭐ | ☐ |
| 9 | TieredStorage（冷热分层） | 3 | ⭐⭐⭐ | ☐ |
| 10 | TenantManager（多租户） | 7 | ⭐⭐⭐ | ☐ |
| 11 | Distributed Txn（2PC） | 4 | ⭐⭐⭐⭐ | ☐ |
| 12 | IoUringEngine | 5 | ⭐⭐⭐ | ☐ |
| **合计** | **12 个模块** | **55** | | **已完成 4/12** |

---

## 面试通关自检

完成所有阶段后，确保你能回答以下问题：

### 存储引擎
- [ ] 数据库为什么以页为单位管理数据？页大小如何选择？
- [ ] Buffer Pool 满了怎么办？LRU 和 Clock-Sweep 的区别？
- [ ] 什么是 WAL？为什么要"先写日志再写数据"？
- [ ] fsync 的代价有多大？Group Commit 如何优化？

### 并发控制
- [ ] MVCC 如何实现读写互不阻塞？
- [ ] PostgreSQL 的 xmin/xmax 可见性判断规则？
- [ ] RC 和 RR 隔离级别的快照取法有什么不同？
- [ ] VACUUM 的作用是什么？为什么 MySQL 不需要 VACUUM？

### 索引
- [ ] B+Tree 和 B-Tree 的区别？为什么数据库选 B+Tree？
- [ ] B+Tree 的叶子分裂和内部节点分裂有什么不同？
- [ ] 什么是 Latch Crabbing？如何优化？

### 分布式与云原生
- [ ] 存储计算分离的优势和劣势？
- [ ] Neon 和 Aurora 架构有什么不同？
- [ ] 2PC 的阻塞问题怎么解决？
- [ ] 什么是 Noisy Neighbor？如何做多租户隔离？
- [ ] 冷热分层的迁移策略如何保证不影响在线业务？

### Raft 共识协议
- [ ] Raft 和 Paxos 的区别？为什么 Raft 更容易理解？
- [ ] Raft 的三大核心机制：Leader 选举、日志复制、安全性？
- [ ] 为什么选举超时需要随机化？Split Vote 是什么？
- [ ] 选举限制（Election Restriction）如何保证安全性？
- [ ] Log Matching Property 是什么？如何保证？
- [ ] Leader 为什么不能提交之前任期的日志？（Figure 8 问题）
- [ ] 网络分区时 Raft 如何保证一致性？
- [ ] Raft 集群为什么通常是奇数个节点？
- [ ] nextIndex 和 matchIndex 的作用？初始化为什么值？
- [ ] Raft 在 etcd、TiKV、CockroachDB 中的应用？

---

## 📦 模块 11：Raft 共识协议

### 学习目标
理解分布式一致性协议 Raft 的核心原理，包括 Leader 选举、日志复制、安全性保证，
以及网络分区、节点故障等异常场景的处理。

### 文件结构
```
src/raft/
├── raft_rpc.h          # RPC 消息定义（RequestVote、AppendEntries）
├── raft_log.h          # 日志管理接口
├── raft_log.cpp        # 日志管理实现
├── raft_node.h         # Raft 节点核心（选举、复制、状态机）
├── raft_node.cpp       # Raft 节点实现
├── raft_cluster.h      # 集群模拟器接口
└── raft_cluster.cpp    # 集群模拟器实现（网络分区、宕机模拟）

test/
└── raft_test.cpp       # 7 个测试场景
```

### 学习路径

#### 第一步：理解 Raft 基本概念（阅读 raft_rpc.h）
1. 理解三种节点角色：Follower、Candidate、Leader
2. 理解任期号（term）的作用——Raft 的逻辑时钟
3. 理解日志条目（LogEntry）的结构
4. 理解两种 RPC：RequestVote 和 AppendEntries

#### 第二步：掌握日志操作（阅读 raft_log.h → 实现 raft_log.cpp）
1. 理解哨兵条目的设计（简化边界处理）
2. 实现日志追加、获取、截断
3. 重点理解 AppendEntries 中的冲突处理逻辑
4. 运行 TestRaftLog 和 TestLogConflictResolution 验证

#### 第三步：实现 Leader 选举（阅读 raft_node.h → 实现选举部分）
1. 实现 HandleRequestVote：投票逻辑 + 选举限制
2. 实现 StartElection：发起选举流程
3. 实现 BecomeLeader：Leader 初始化
4. 理解随机化选举超时防止 Split Vote
5. 运行 TestLeaderElection 验证

#### 第四步：实现日志复制（实现 raft_node.cpp 剩余部分）
1. 实现 HandleAppendEntries：一致性检查 + 日志追加
2. 实现 SendHeartbeats：构造请求 + 处理响应
3. 实现 UpdateCommitIndex：多数派确认 + 提交
4. 实现 Propose：客户端命令入口
5. 运行 TestLogReplication 验证

#### 第五步：理解故障处理（通过测试学习）
1. 运行 TestLeaderFailover：理解 Leader 宕机后的重新选举
2. 运行 TestNetworkPartition：理解网络分区的处理
3. 运行 TestFiveNodeFaultTolerance：理解集群容错能力
4. 尝试修改测试，模拟更多故障场景

### 编译和运行
```bash
cd build
cmake .. && make raft_test
./raft_test
```

### 进阶挑战
- [ ] 实现日志持久化（写入磁盘，重启后恢复）
- [ ] 实现日志压缩（Snapshot）
- [ ] 实现成员变更（动态增减节点）
- [ ] 实现 PreVote 优化（防止网络分区节点扰乱集群）
- [ ] 将 RPC 模拟替换为真正的网络通信（gRPC/TCP）

---

> 🎯 **最终目标**：所有 `./integration_test` 和 `./raft_test` 测试通过，每个面试问题都能结合代码讲出原理。  
> 💪 **你已有 6 年 C++ 经验，底层实现对你不是问题。重点放在理解"为什么"而非"怎么写"。**
