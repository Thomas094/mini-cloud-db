#include <cassert>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <unistd.h>

// 引入所有模块
#include "common/types.h"
#include "common/config.h"
#include "storage/disk_manager.h"
#include "storage/page.h"
#include "buffer/lru_replacer.h"
#include "buffer/buffer_pool_manager.h"
#include "recovery/log_record.h"
#include "recovery/wal_manager.h"
#include "concurrency/mvcc.h"
#include "index/b_plus_tree.h"
#include "network/page_server.h"
#include "column/column_store.h"
#include "tiered/tiered_storage.h"
#include "tenant/tenant_manager.h"
#include "distributed/distributed_txn.h"

using namespace minidb;

// ============================================================
// 测试框架（简单的断言宏）
// ============================================================
#define TEST_BEGIN(name) \
    std::cout << "=== TEST: " << (name) << " ===" << std::endl;

#define TEST_PASS(name) \
    std::cout << "✅ PASS: " << (name) << std::endl;

#define TEST_FAIL(name, msg) \
    std::cout << "❌ FAIL: " << (name) << " - " << (msg) << std::endl;

#define EXPECT_TRUE(expr) \
    if (!(expr)) { \
        std::cout << "  ASSERT FAILED: " #expr " at line " << __LINE__ << std::endl; \
        return false; \
    }

#define EXPECT_EQ(a, b) \
    if ((a) != (b)) { \
        std::cout << "  ASSERT FAILED: " #a " != " #b " at line " << __LINE__ << std::endl; \
        return false; \
    }

// ============================================================
// 测试1：DiskManager 基本读写
// ============================================================
bool TestDiskManager() {
    TEST_BEGIN("DiskManager 基本读写");

    try {
        DiskManager dm("/tmp/test_minidb.db");

        // 分配页面
        page_id_t pid = dm.AllocatePage();
        EXPECT_EQ(pid, 0);

        // 写入数据
        char write_buf[PAGE_SIZE] = {0};
        const char* msg = "Hello, MiniCloudDB!";
        std::memcpy(write_buf, msg, strlen(msg));
        dm.WritePage(pid, write_buf);

        // 读回验证
        char read_buf[PAGE_SIZE] = {0};
        dm.ReadPage(pid, read_buf);
        EXPECT_TRUE(std::memcmp(write_buf, read_buf, PAGE_SIZE) == 0);

        TEST_PASS("DiskManager 基本读写");
        return true;
    } catch (const std::exception& e) {
        TEST_FAIL("DiskManager 基本读写", e.what());
        return false;
    }
}

// ============================================================
// 测试2：LRU Replacer
// ============================================================
bool TestLRUReplacer() {
    TEST_BEGIN("LRU Replacer");

    try {
        LRUReplacer replacer(3);

        // 添加帧到候选列表
        replacer.Unpin(0);
        replacer.Unpin(1);
        replacer.Unpin(2);
        EXPECT_EQ(replacer.Size(), 3u);

        // Pin 帧1（从候选列表移除）
        replacer.Pin(1);
        EXPECT_EQ(replacer.Size(), 2u);

        // 淘汰应该选最久未使用的（帧0）
        frame_id_t victim;
        EXPECT_TRUE(replacer.Evict(&victim));
        EXPECT_EQ(victim, 0);

        TEST_PASS("LRU Replacer");
        return true;
    } catch (const std::exception& e) {
        TEST_FAIL("LRU Replacer", e.what());
        return false;
    }
}

// ============================================================
// 测试3：Buffer Pool Manager
// ============================================================
bool TestBufferPoolManager() {
    TEST_BEGIN("Buffer Pool Manager");

    try {
        auto dm = std::make_unique<DiskManager>("/tmp/test_minidb_bpm.db");
        BufferPoolManager bpm(10, dm.get());

        // 创建新页面
        page_id_t pid;
        Page* page = bpm.NewPage(&pid);
        EXPECT_TRUE(page != nullptr);
        EXPECT_EQ(pid, 0);

        // 写入数据
        const char* data = "BufferPool Test";
        std::memcpy(page->GetUserData(), data, strlen(data));

        // Unpin 并标记为脏
        EXPECT_TRUE(bpm.UnpinPage(pid, true));

        // 重新 Fetch
        Page* page2 = bpm.FetchPage(pid);
        EXPECT_TRUE(page2 != nullptr);
        EXPECT_TRUE(std::memcmp(page2->GetUserData(), data, strlen(data)) == 0);

        bpm.UnpinPage(pid, false);

        TEST_PASS("Buffer Pool Manager");
        return true;
    } catch (const std::exception& e) {
        TEST_FAIL("Buffer Pool Manager", e.what());
        return false;
    }
}

// ============================================================
// 测试3.5：WAL Manager
// ============================================================
bool TestWALManager() {
    TEST_BEGIN("WAL Manager");

    try {
        const std::string wal_file = "/tmp/test_minidb_wal.log";
        const std::string db_file = "/tmp/test_minidb_wal_bpm.db";

        // 清理旧文件
        unlink(wal_file.c_str());
        unlink(db_file.c_str());

        // ---- 测试1：AppendLog 返回单调递增的 LSN ----
        {
            WALManager wal(wal_file);
            LogRecord rec1 = LogRecord::MakeTxnRecord(LogRecordType::TXN_BEGIN, 1, INVALID_LSN);
            lsn_t lsn1 = wal.AppendLog(rec1);
            EXPECT_EQ(lsn1, 1u);

            LogRecord rec2 = LogRecord::MakeTxnRecord(LogRecordType::TXN_BEGIN, 2, INVALID_LSN);
            lsn_t lsn2 = wal.AppendLog(rec2);
            EXPECT_EQ(lsn2, 2u);

            EXPECT_TRUE(lsn2 > lsn1);

            // ---- 测试2：Flush 后 flushed_lsn 更新 ----
            wal.Flush();
            EXPECT_EQ(wal.GetFlushedLSN(), wal.GetCurrentLSN());
        }

        // 清理，重新开始完整的 Recover 测试
        unlink(wal_file.c_str());
        unlink(db_file.c_str());

        // ---- 测试3：Recover Redo 正确性 ----
        {
            // 写入 WAL 日志：模拟事务1写入数据并提交
            WALManager wal(wal_file);

            // 事务1 BEGIN
            LogRecord begin_rec = LogRecord::MakeTxnRecord(
                LogRecordType::TXN_BEGIN, /*txn_id=*/1, INVALID_LSN);
            lsn_t lsn1 = wal.AppendLog(begin_rec);

            // 事务1 INSERT：向 page 0, offset 0 写入 "Hello"
            const char* insert_data = "Hello";
            LogRecord insert_rec = LogRecord::MakeDataRecord(
                LogRecordType::INSERT, /*txn_id=*/1, /*prev_lsn=*/lsn1,
                /*page_id=*/0, /*offset=*/0,
                /*old_data=*/nullptr, /*old_len=*/0,
                /*new_data=*/insert_data, /*new_len=*/5);
            lsn_t lsn2 = wal.AppendLog(insert_rec);

            // 事务1 COMMIT
            LogRecord commit_rec = LogRecord::MakeTxnRecord(
                LogRecordType::TXN_COMMIT, /*txn_id=*/1, /*prev_lsn=*/lsn2);
            wal.AppendLog(commit_rec);

            wal.Flush();
        }

        // 用新的 BPM 进行 Recover，验证 Redo 结果
        {
            auto dm = std::make_unique<DiskManager>(db_file);
            BufferPoolManager bpm(10, dm.get());

            // 先创建 page 0（Recover 需要 FetchPage 能找到它）
            page_id_t pid;
            Page* page = bpm.NewPage(&pid);
            EXPECT_TRUE(page != nullptr);
            EXPECT_EQ(pid, 0);
            bpm.UnpinPage(pid, false);

            // 执行 Recover
            WALManager wal(wal_file);
            wal.Recover(&bpm);

            // 验证 page 0 的数据被正确 Redo
            Page* recovered_page = bpm.FetchPage(0);
            EXPECT_TRUE(recovered_page != nullptr);
            EXPECT_TRUE(std::memcmp(recovered_page->GetUserData(), "Hello", 5) == 0);
            bpm.UnpinPage(0, false);
        }

        // 清理，测试 Undo
        unlink(wal_file.c_str());
        unlink(db_file.c_str());

        // ---- 测试4：Recover Undo（未提交事务回滚） ----
        {
            WALManager wal(wal_file);

            // 事务2 BEGIN
            LogRecord begin_rec = LogRecord::MakeTxnRecord(
                LogRecordType::TXN_BEGIN, /*txn_id=*/2, INVALID_LSN);
            lsn_t lsn1 = wal.AppendLog(begin_rec);

            // 事务2 UPDATE：向 page 0, offset 0 写入 "World"（旧数据为全零）
            char old_data[5] = {0};
            const char* new_data = "World";
            LogRecord update_rec = LogRecord::MakeDataRecord(
                LogRecordType::UPDATE, /*txn_id=*/2, /*prev_lsn=*/lsn1,
                /*page_id=*/0, /*offset=*/0,
                /*old_data=*/old_data, /*old_len=*/5,
                /*new_data=*/new_data, /*new_len=*/5);
            wal.AppendLog(update_rec);

            // 注意：事务2 没有 COMMIT 也没有 ABORT（模拟崩溃）
            wal.Flush();
        }

        // Recover 应该 Undo 事务2 的修改
        {
            auto dm = std::make_unique<DiskManager>(db_file);
            BufferPoolManager bpm(10, dm.get());

            // 创建 page 0
            page_id_t pid;
            Page* page = bpm.NewPage(&pid);
            EXPECT_TRUE(page != nullptr);
            bpm.UnpinPage(pid, false);

            // 执行 Recover
            WALManager wal(wal_file);
            wal.Recover(&bpm);

            // 验证 page 0 的数据被 Undo 回旧值（全零）
            Page* recovered_page = bpm.FetchPage(0);
            EXPECT_TRUE(recovered_page != nullptr);
            char expected[5] = {0};
            EXPECT_TRUE(std::memcmp(recovered_page->GetUserData(), expected, 5) == 0);
            bpm.UnpinPage(0, false);
        }

        // 清理临时文件
        unlink(wal_file.c_str());
        unlink(db_file.c_str());

        TEST_PASS("WAL Manager");
        return true;
    } catch (const std::exception& e) {
        TEST_FAIL("WAL Manager", e.what());
        return false;
    }
}

// ============================================================
// 测试4：MVCC 可见性
// ============================================================
bool TestMVCC() {
    TEST_BEGIN("MVCC 可见性");

    try {
        MVCCManager mvcc;

        // 事务1开始并写入数据
        txn_id_t txn1 = mvcc.BeginTransaction();

        // 事务2开始（此时txn1还在运行）
        txn_id_t txn2 = mvcc.BeginTransaction();

        // txn1 创建的版本
        TupleVersion v1;
        v1.xmin = txn1;
        v1.xmax = INVALID_TXN_ID;

        // txn2 看不到 txn1（因为 txn1 在 txn2 的快照中是活跃的）
        EXPECT_TRUE(!mvcc.IsVersionVisible(v1, txn2));

        // txn1 提交
        mvcc.CommitTransaction(txn1);

        // 事务3开始（txn1已提交）
        txn_id_t txn3 = mvcc.BeginTransaction();

        // txn3 能看到 txn1 的数据
        EXPECT_TRUE(mvcc.IsVersionVisible(v1, txn3));

        // txn2 仍然看不到 txn1（快照在txn1提交前创建的）
        EXPECT_TRUE(!mvcc.IsVersionVisible(v1, txn2));

        mvcc.CommitTransaction(txn2);
        mvcc.CommitTransaction(txn3);

        TEST_PASS("MVCC 可见性");
        return true;
    } catch (const std::exception& e) {
        TEST_FAIL("MVCC 可见性", e.what());
        return false;
    }
}

// ============================================================
// 测试5：B+Tree 基本操作
// ============================================================
bool TestBPlusTree() {
    TEST_BEGIN("B+Tree 基本操作");

    try {
        BPlusTree tree;

        // 插入
        for (int i = 0; i < 100; i++) {
            ValueType val{static_cast<page_id_t>(i), static_cast<uint16_t>(i % 256)};
            EXPECT_TRUE(tree.Insert(i, val));
        }

        // 查找
        auto result = tree.Search(42);
        EXPECT_TRUE(result.has_value());
        EXPECT_EQ(result->page_id, 42);

        // 范围查询
        auto range = tree.RangeScan(10, 20);
        EXPECT_EQ(range.size(), 11u);  // [10, 20] 共11个

        // 删除
        EXPECT_TRUE(tree.Delete(42));
        auto deleted = tree.Search(42);
        EXPECT_TRUE(!deleted.has_value());

        TEST_PASS("B+Tree 基本操作");
        return true;
    } catch (const std::exception& e) {
        TEST_FAIL("B+Tree 基本操作", e.what());
        return false;
    }
}

// ============================================================
// 测试6：分布式事务 2PC
// ============================================================
bool TestDistributedTxn() {
    TEST_BEGIN("分布式事务 2PC");

    try {
        Coordinator coordinator;
        auto p1 = std::make_shared<Participant>(1);
        auto p2 = std::make_shared<Participant>(2);
        auto p3 = std::make_shared<Participant>(3);

        coordinator.AddParticipant(p1);
        coordinator.AddParticipant(p2);
        coordinator.AddParticipant(p3);

        // 执行分布式事务
        txn_id_t txn_id = 1001;
        bool committed = coordinator.ExecuteTransaction(txn_id);

        if (committed) {
            EXPECT_EQ(coordinator.GetTxnState(txn_id), DistributedTxnState::COMMITTED);
            EXPECT_EQ(p1->GetState(txn_id), ParticipantState::COMMITTED);
            EXPECT_EQ(p2->GetState(txn_id), ParticipantState::COMMITTED);
            EXPECT_EQ(p3->GetState(txn_id), ParticipantState::COMMITTED);
        }

        TEST_PASS("分布式事务 2PC");
        return true;
    } catch (const std::exception& e) {
        TEST_FAIL("分布式事务 2PC", e.what());
        return false;
    }
}

// ============================================================
// 测试7：多租户资源隔离
// ============================================================
bool TestTenantManager() {
    TEST_BEGIN("多租户资源隔离");

    try {
        TenantManager tm;

        ResourceQuota quota;
        quota.max_memory_bytes = 1024 * 1024;  // 1MB
        quota.max_iops = 100;

        tenant_id_t t1 = tm.CreateTenant("tenant_a", quota);
        tenant_id_t t2 = tm.CreateTenant("tenant_b", quota);

        // 租户A 申请内存
        EXPECT_TRUE(tm.AcquireMemory(t1, 512 * 1024));  // 512KB - 应成功
        EXPECT_TRUE(tm.AcquireMemory(t1, 256 * 1024));  // 256KB - 应成功

        // 租户A 再申请 - 应超额失败
        EXPECT_TRUE(!tm.AcquireMemory(t1, 512 * 1024));  // 总量超过1MB

        // 租户B 不受影响
        EXPECT_TRUE(tm.AcquireMemory(t2, 512 * 1024));  // 应成功

        // 释放
        tm.ReleaseMemory(t1, 256 * 1024);
        EXPECT_TRUE(tm.AcquireMemory(t1, 256 * 1024));  // 释放后再申请 - 应成功

        TEST_PASS("多租户资源隔离");
        return true;
    } catch (const std::exception& e) {
        TEST_FAIL("多租户资源隔离", e.what());
        return false;
    }
}

// ============================================================
// 主函数
// ============================================================
int main() {
    std::cout << "\n";
    std::cout << "╔══════════════════════════════════════════════╗\n";
    std::cout << "║     Mini Cloud-Native Database Test Suite     ║\n";
    std::cout << "╚══════════════════════════════════════════════╝\n";
    std::cout << "\n";

    int passed = 0;
    int failed = 0;

    auto run = [&](const char* name, bool (*test_func)()) {
        try {
            if (test_func()) {
                passed++;
            } else {
                failed++;
            }
        } catch (const std::exception& e) {
            TEST_FAIL(name, e.what());
            failed++;
        }
        std::cout << std::endl;
    };

    run("DiskManager", TestDiskManager);
    run("LRUReplacer", TestLRUReplacer);
    run("BufferPoolManager", TestBufferPoolManager);
    run("WALManager", TestWALManager);
    run("MVCC", TestMVCC);
    run("BPlusTree", TestBPlusTree);
    run("DistributedTxn", TestDistributedTxn);
    run("TenantManager", TestTenantManager);

    std::cout << "══════════════════════════════════════════════\n";
    std::cout << "Total: " << (passed + failed) << "  ";
    std::cout << "Passed: " << passed << "  ";
    std::cout << "Failed: " << failed << "\n";
    std::cout << "══════════════════════════════════════════════\n";

    return failed > 0 ? 1 : 0;
}
