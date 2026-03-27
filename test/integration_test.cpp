#include <cassert>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>

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
        std::memcpy(page->GetData(), data, strlen(data));

        // Unpin 并标记为脏
        EXPECT_TRUE(bpm.UnpinPage(pid, true));

        // 重新 Fetch
        Page* page2 = bpm.FetchPage(pid);
        EXPECT_TRUE(page2 != nullptr);
        EXPECT_TRUE(std::memcmp(page2->GetData(), data, strlen(data)) == 0);

        bpm.UnpinPage(pid, false);

        TEST_PASS("Buffer Pool Manager");
        return true;
    } catch (const std::exception& e) {
        TEST_FAIL("Buffer Pool Manager", e.what());
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
