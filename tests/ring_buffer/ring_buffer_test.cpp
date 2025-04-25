#include "../../include/ring_buffer/ring_buffer.h"
#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include <atomic>
#include <chrono>
#include <random>
#include <mutex>
#include <iostream>


// 单生产者单消费者测试 - 使用更可靠的验证方法
TEST(RingBufferTest, SingleProducerSingleConsumer) {
    ring_buffer<int, 128> rb;
    std::atomic<bool> producer_done(false);
    std::atomic<int> produced_count(0);
    std::atomic<int> consumed_count(0);
    const int NUM_ITEMS = 1000000;

    // 使用数组跟踪每个值是否被消费
    // 0 = 未生产, 1 = 已生产, 2 = 已消费
    std::vector<std::atomic<int>> item_status(NUM_ITEMS + 1);
    for (int i = 0; i <= NUM_ITEMS; i++) {
        item_status[i].store(0, std::memory_order_relaxed);
    }

    // 生产者线程 - 使用左值和右值混合
    std::thread producer([&]() {
        for (int i = 1; i <= NUM_ITEMS; i++) {
            // 先标记为已生产
            item_status[i].store(1, std::memory_order_release);
            // 确保标记对其他线程可见
            std::atomic_thread_fence(std::memory_order_seq_cst);

            if (i % 2 == 0) {
                // 使用左值
                int value = i;
                while (!rb.push(value)) {
                    std::this_thread::yield(); // 缓冲区满，让出CPU
                }
            } else {
                // 使用右值
                while (!rb.push(i)) {
                    std::this_thread::yield(); // 缓冲区满，让出CPU
                }
            }

            produced_count.fetch_add(1, std::memory_order_relaxed);
        }
        producer_done = true;
    });

    // 消费者线程
    std::thread consumer([&]() {
        int value;
        while (!producer_done || consumed_count < produced_count) {
            if (rb.get(value)) {
                // 确保值在有效范围内
                ASSERT_GT(value, 0);
                ASSERT_LE(value, NUM_ITEMS);

                // 标记为已消费，确保每个值只被消费一次
                int expected = 1; // 期望状态为"已生产"
                bool marked = item_status[value].compare_exchange_strong(
                    expected, 2, std::memory_order_acq_rel);

                // 如果成功标记为已消费，增加计数
                if (marked) {
                    consumed_count.fetch_add(1, std::memory_order_relaxed);
                } else {
                    // 如果标记失败，检查当前状态
                    int current_status = item_status[value].load(std::memory_order_acquire);

                    ASSERT_TRUE(false) << "Value " << value << " was either consumed multiple times or never produced"
                                       << ", status=" << current_status;
                }
            } else {
                std::this_thread::yield(); // 缓冲区空，让出CPU
            }
        }
    });

    producer.join();
    consumer.join();

    // 验证所有项目都被正确消费
    EXPECT_EQ(produced_count.load(), NUM_ITEMS);
    EXPECT_EQ(consumed_count.load(), NUM_ITEMS);

    // 验证每个值都被正确处理
    for (int i = 1; i <= NUM_ITEMS; i++) {
        EXPECT_EQ(item_status[i].load(), 2) << "Item " << i << " was not properly produced and consumed";
    }
}

// 多生产者单消费者测试 - 使用更可靠的验证方法
TEST(RingBufferTest, MultipleProducers) {
    ring_buffer<int, 128> rb;
    std::atomic<int> produced_count(0);
    std::atomic<int> consumed_count(0);
    const int NUM_PRODUCERS = 4;
    const int ITEMS_PER_PRODUCER = 2500;
    const int TOTAL_ITEMS = NUM_PRODUCERS * ITEMS_PER_PRODUCER;

    // 使用数组跟踪每个值是否被消费
    // 0 = 未生产, 1 = 已生产, 2 = 已消费
    std::vector<std::atomic<int>> item_status(TOTAL_ITEMS + 1);
    for (int i = 0; i <= TOTAL_ITEMS; i++) {
        item_status[i].store(0, std::memory_order_relaxed);
    }

    // 生产者线程 - 每个线程交替使用左值和右值
    std::vector<std::thread> producers;
    for (int p = 0; p < NUM_PRODUCERS; p++) {
        producers.emplace_back([&, p]() {
            for (int i = 1; i <= ITEMS_PER_PRODUCER; i++) {
                int value = p * ITEMS_PER_PRODUCER + i;

                // 先标记为已生产
                item_status[value].store(1, std::memory_order_release);
                // 确保标记对其他线程可见
                std::atomic_thread_fence(std::memory_order_seq_cst);

                // 然后写入环形缓冲区
                if (i % 2 == 0) {
                    // 使用左值
                    while (!rb.push(value)) {
                        std::this_thread::yield(); // 缓冲区满，让出CPU
                    }
                } else {
                    // 使用右值 - 创建一个副本用于移动
                    int temp_value = value;
                    while (!rb.push(std::move(temp_value))) {
                        std::this_thread::yield(); // 缓冲区满，让出CPU
                        // 如果移动后需要重试，重新创建副本
                        temp_value = value;
                    }
                }

                produced_count.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    // 消费者线程
    std::thread consumer([&]() {
        int value;

        // 消费直到达到预期的总项目数
        while (consumed_count < TOTAL_ITEMS) {
            if (rb.get(value)) {
                // 确保值在有效范围内
                ASSERT_GT(value, 0);
                ASSERT_LE(value, TOTAL_ITEMS);

                // 标记为已消费，确保每个值只被消费一次
                int expected = 1; // 期望状态为"已生产"
                bool marked = item_status[value].compare_exchange_strong(
                    expected, 2, std::memory_order_acq_rel);

                // 如果成功标记为已消费，增加计数
                if (marked) {
                    consumed_count.fetch_add(1, std::memory_order_relaxed);
                } else {
                    // 如果标记失败，检查当前状态
                    int current_status = item_status[value].load(std::memory_order_acquire);

                    ASSERT_TRUE(false) << "Value " << value << " was either consumed multiple times or never produced"
                                       << ", status=" << current_status;
                }
            } else {
                std::this_thread::yield(); // 缓冲区空，让出CPU
            }
        }
    });

    // 等待所有线程完成
    for (auto& p : producers) {
        p.join();
    }
    consumer.join();

    // 验证所有项目都被正确消费
    EXPECT_EQ(produced_count.load(), TOTAL_ITEMS);
    EXPECT_EQ(consumed_count.load(), TOTAL_ITEMS);

    // 验证每个值都被正确处理
    for (int i = 1; i <= TOTAL_ITEMS; i++) {
        EXPECT_EQ(item_status[i].load(), 2) << "Item " << i << " was not properly produced and consumed";
    }
}

// 单生产者多消费者测试 - 使用更可靠的验证方法
TEST(RingBufferTest, MultipleConsumers) {
    ring_buffer<int, 128> rb;
    std::atomic<bool> producer_done(false);
    std::atomic<int> produced_count(0);
    std::atomic<int> consumed_count(0);
    const int NUM_CONSUMERS = 4;
    const int TOTAL_ITEMS = 1000000;

    // 使用数组跟踪每个值是否被消费
    // 0 = 未生产, 1 = 已生产, 2 = 已消费
    std::vector<std::atomic<int>> item_status(TOTAL_ITEMS + 1);
    for (int i = 0; i <= TOTAL_ITEMS; i++) {
        item_status[i].store(0, std::memory_order_relaxed);
    }

    // 生产者线程 - 交替使用左值和右值
    std::thread producer([&]() {
        for (int i = 1; i <= TOTAL_ITEMS; i++) {
            // 先标记为已生产
            item_status[i].store(1, std::memory_order_release);
            // 确保标记对其他线程可见
            std::atomic_thread_fence(std::memory_order_seq_cst);

            if (i % 3 == 0) {
                // 使用左值
                int value = i;
                while (!rb.push(value)) {
                    std::this_thread::yield(); // 缓冲区满，让出CPU
                }
            } else if (i % 3 == 1) {
                // 使用右值字面量
                while (!rb.push(i)) {
                    std::this_thread::yield(); // 缓冲区满，让出CPU
                }
            } else {
                // 使用std::move将左值转换为右值
                int value = i;
                while (!rb.push(std::move(value))) {
                    std::this_thread::yield(); // 缓冲区满，让出CPU
                }
            }

            produced_count.fetch_add(1, std::memory_order_relaxed);
        }
        producer_done = true;
    });

    // 消费者线程
    std::vector<std::thread> consumers;
    for (int c = 0; c < NUM_CONSUMERS; c++) {
        consumers.emplace_back([&]() {
            int value;
            while (!producer_done || consumed_count < produced_count) {
                if (rb.get(value)) {
                    // 确保值在有效范围内
                    ASSERT_GT(value, 0);
                    ASSERT_LE(value, TOTAL_ITEMS);

                    // 标记为已消费，确保每个值只被消费一次
                    int expected = 1; // 期望状态为"已生产"
                    bool marked = item_status[value].compare_exchange_strong(
                        expected, 2, std::memory_order_acq_rel);

                    // 如果成功标记为已消费，增加计数
                    if (marked) {
                        consumed_count.fetch_add(1, std::memory_order_relaxed);
                    } else {
                        // 如果标记失败，检查当前状态
                        int current_status = item_status[value].load(std::memory_order_acquire);
                        if (current_status == 0) {
                            ASSERT_TRUE(false) << "Value " << value << " was consumed but never produced (status=0)";
                        } else if (current_status == 2) {
                            ASSERT_TRUE(false) << "Value " << value << " was consumed multiple times (status=2)";
                        } else {
                            ASSERT_TRUE(false) << "Value " << value << " has unexpected status: " << current_status;
                        }
                    }
                } else {
                    std::this_thread::yield(); // 缓冲区空，让出CPU
                }
            }
        });
    }

    producer.join();
    for (auto& c : consumers) {
        c.join();
    }

    // 验证所有项目都被正确消费
    EXPECT_EQ(produced_count.load(), TOTAL_ITEMS);
    EXPECT_EQ(consumed_count.load(), TOTAL_ITEMS);

    // 验证每个值都被正确处理
    for (int i = 1; i <= TOTAL_ITEMS; i++) {
        EXPECT_EQ(item_status[i].load(), 2) << "Item " << i << " was not properly produced and consumed";
    }
}

// 多生产者多消费者测试 - 使用更可靠的验证方法
TEST(RingBufferTest, MultipleProducersMultipleConsumers) {
    ring_buffer<int, 128> rb;
    std::atomic<int> produced_count(0);
    std::atomic<int> consumed_count(0);

    const int NUM_PRODUCERS = 4;
    const int NUM_CONSUMERS = 4;
    const int ITEMS_PER_PRODUCER = 2500;
    const int TOTAL_ITEMS = NUM_PRODUCERS * ITEMS_PER_PRODUCER;

    // 使用数组跟踪每个值是否被消费
    // 0 = 未生产, 1 = 已生产, 2 = 已消费
    std::vector<std::atomic<int>> item_status(TOTAL_ITEMS + 1);
    for (int i = 0; i <= TOTAL_ITEMS; i++) {
        item_status[i].store(0, std::memory_order_relaxed);
    }

    // 生产者线程 - 使用不同的值类型
    std::vector<std::thread> producers;
    for (int p = 0; p < NUM_PRODUCERS; p++) {
        producers.emplace_back([&, p]() {
            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_int_distribution<> delay(0, 2);
            std::uniform_int_distribution<> value_type(0, 2); // 决定使用哪种值类型

            for (int i = 1; i <= ITEMS_PER_PRODUCER; i++) {
                // 生成唯一的值 (1-based)
                int value = p * ITEMS_PER_PRODUCER + i;

                // 随机延迟以增加竞争
                if (delay(gen) == 0) {
                    std::this_thread::sleep_for(std::chrono::microseconds(1));
                }

                // 先标记为已生产
                item_status[value].store(1, std::memory_order_release);
                // 确保标记对其他线程可见
                std::atomic_thread_fence(std::memory_order_seq_cst);

                bool pushed = false;
                // 随机选择值类型：左值、右值字面量或std::move
                int type = value_type(gen);

                while (!pushed) {
                    if (type == 0) {
                        // 使用左值
                        pushed = rb.push(value);
                    } else if (type == 1) {
                        // 创建临时变量（右值）
                        pushed = rb.push(int(value));
                    } else {
                        // 创建一个副本用于移动
                        int temp_value = value;
                        pushed = rb.push(std::move(temp_value));
                    }

                    if (!pushed) {
                        std::this_thread::yield();
                    }
                }

                produced_count.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    // 消费者线程
    std::vector<std::thread> consumers;
    for (int c = 0; c < NUM_CONSUMERS; c++) {
        consumers.emplace_back([&]() {
            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_int_distribution<> delay(0, 2);
            int value;

            while (consumed_count < TOTAL_ITEMS) {
                // 随机延迟以增加竞争
                if (delay(gen) == 0) {
                    std::this_thread::sleep_for(std::chrono::microseconds(1));
                }

                if (rb.get(value)) {
                    // 确保值在有效范围内
                    ASSERT_GT(value, 0);
                    ASSERT_LE(value, TOTAL_ITEMS);

                    // 标记为已消费，确保每个值只被消费一次
                    int expected = 1; // 期望状态为"已生产"
                    bool marked = item_status[value].compare_exchange_strong(
                        expected, 2, std::memory_order_acq_rel);

                    // 如果成功标记为已消费，增加计数
                    if (marked) {
                        consumed_count.fetch_add(1, std::memory_order_relaxed);
                    } else {
                        // 如果标记失败，检查当前状态
                        int current_status = item_status[value].load(std::memory_order_acquire);
                        if (current_status == 0) {
                            // 计算该值应该在的位置
                            int producer_idx = (value - 1) / ITEMS_PER_PRODUCER;
                            int item_idx = (value - 1) % ITEMS_PER_PRODUCER + 1;

                            ASSERT_TRUE(false) << "Value " << value << " was consumed but never produced (status=0)"
                                               << ", producer=" << producer_idx
                                               << ", item=" << item_idx;
                        } else if (current_status == 2) {
                            ASSERT_TRUE(false) << "Value " << value << " was consumed multiple times (status=2)";
                        } else {
                            ASSERT_TRUE(false) << "Value " << value << " has unexpected status: " << current_status;
                        }
                    }
                } else {
                    std::this_thread::yield(); // 缓冲区空，让出CPU
                }
            }
        });
    }

    // 等待所有线程完成
    for (auto& p : producers) {
        p.join();
    }
    for (auto& c : consumers) {
        c.join();
    }

    // 验证所有项目都被正确生产和消费
    EXPECT_EQ(produced_count.load(), TOTAL_ITEMS);
    EXPECT_EQ(consumed_count.load(), TOTAL_ITEMS);

    // 验证每个值都被正确处理
    for (int i = 1; i <= TOTAL_ITEMS; i++) {
        EXPECT_EQ(item_status[i].load(), 2) << "Item " << i << " was not properly produced and consumed";
    }
}

// 高竞争压力测试 - 使用更可靠的验证方法
TEST(RingBufferTest, StressTest) {
    ring_buffer<int, 16> rb; // 使用小缓冲区增加竞争
    std::atomic<int> produced_count(0);
    std::atomic<int> consumed_count(0);

    const int NUM_PRODUCERS = 8;
    const int NUM_CONSUMERS = 8;
    const int ITEMS_PER_PRODUCER = 1000;
    const int TOTAL_ITEMS = NUM_PRODUCERS * ITEMS_PER_PRODUCER;

    // 使用数组跟踪每个值是否被消费
    // 0 = 未生产, 1 = 已生产, 2 = 已消费
    std::vector<std::atomic<int>> item_status(TOTAL_ITEMS + 1);
    for (int i = 0; i <= TOTAL_ITEMS; i++) {
        item_status[i].store(0, std::memory_order_relaxed);
    }

    // 生产者线程 - 使用指数退避策略
    std::vector<std::thread> producers;
    for (int p = 0; p < NUM_PRODUCERS; p++) {
        producers.emplace_back([&, p]() {
            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_int_distribution<> value_type(0, 1); // 决定使用左值还是右值

            for (int i = 1; i <= ITEMS_PER_PRODUCER; i++) {
                // 生成唯一的值 (1-based)
                int value = p * ITEMS_PER_PRODUCER + i;

                // 先标记为已生产
                item_status[value].store(1, std::memory_order_release);
                // 确保标记对其他线程可见
                std::atomic_thread_fence(std::memory_order_seq_cst);

                // 使用指数退避策略尝试推送
                int backoff = 1;
                bool pushed = false;

                while (!pushed) {
                    // 随机选择使用左值还是右值
                    if (value_type(gen) == 0) {
                        // 使用左值
                        pushed = rb.push(value);
                    } else {
                        // 创建一个副本用于移动
                        int temp_value = value;
                        pushed = rb.push(std::move(temp_value));
                    }

                    if (!pushed) {
                        // 指数退避以减少竞争
                        if (backoff < 1000) {
                            backoff *= 2;
                        }
                        std::this_thread::sleep_for(std::chrono::microseconds(backoff));
                    }
                }

                produced_count.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    // 消费者线程 - 使用退避策略
    std::vector<std::thread> consumers;
    for (int c = 0; c < NUM_CONSUMERS; c++) {
        consumers.emplace_back([&]() {
            int value;
            int backoff = 1;

            while (consumed_count < TOTAL_ITEMS) {
                if (rb.get(value)) {
                    // 确保值在有效范围内
                    ASSERT_GT(value, 0);
                    ASSERT_LE(value, TOTAL_ITEMS);

                    // 标记为已消费，确保每个值只被消费一次
                    int expected = 1; // 期望状态为"已生产"
                    bool marked = item_status[value].compare_exchange_strong(
                        expected, 2, std::memory_order_acq_rel);

                    // 如果成功标记为已消费，增加计数
                    if (marked) {
                        consumed_count.fetch_add(1, std::memory_order_relaxed);
                    } else {
                        // 如果标记失败，检查当前状态
                        int current_status = item_status[value].load(std::memory_order_acquire);
                        if (current_status == 0) {
                            ASSERT_TRUE(false) << "Value " << value << " was consumed but never produced (status=0)";
                        } else if (current_status == 2) {
                            ASSERT_TRUE(false) << "Value " << value << " was consumed multiple times (status=2)";
                        } else {
                            ASSERT_TRUE(false) << "Value " << value << " has unexpected status: " << current_status;
                        }
                    }

                    backoff = 1; // 重置退避时间
                } else {
                    // 使用退避策略减少竞争
                    if (backoff < 1000) {
                        backoff *= 2;
                    }
                    std::this_thread::sleep_for(std::chrono::microseconds(backoff));
                }
            }
        });
    }

    // 等待所有线程完成
    for (auto& p : producers) {
        p.join();
    }
    for (auto& c : consumers) {
        c.join();
    }

    // 验证所有项目都被正确生产和消费
    EXPECT_EQ(produced_count.load(), TOTAL_ITEMS);
    EXPECT_EQ(consumed_count.load(), TOTAL_ITEMS);

    // 验证每个值都被正确处理
    for (int i = 1; i <= TOTAL_ITEMS; i++) {
        EXPECT_EQ(item_status[i].load(), 2) << "Item " << i << " was not properly produced and consumed";
    }
}

// Custom type test
struct TestStruct {
    int id;
    std::string data;

    // 默认构造函数
    TestStruct() : id(0), data("") {}

    // 带参数的构造函数
    TestStruct(int i, std::string s) : id(i), data(std::move(s)) {}

    // 移动构造函数
    TestStruct(TestStruct&& other) noexcept : id(other.id), data(std::move(other.data)) {}

    // 移动赋值运算符
    TestStruct& operator=(TestStruct&& other) noexcept {
        if (this != &other) {
            id = other.id;
            data = std::move(other.data);
        }
        return *this;
    }

    // 拷贝构造函数
    TestStruct(const TestStruct& other) : id(other.id), data(other.data) {}

    // 拷贝赋值运算符
    TestStruct& operator=(const TestStruct& other) {
        if (this != &other) {
            id = other.id;
            data = other.data;
        }
        return *this;
    }
};

// 测试自定义类型的左值和右值
TEST(RingBufferTest, CustomTypeTest) {
    ring_buffer<TestStruct, 16> rb;

    // 测试右值 - 使用临时对象（右值）
    for (int i = 0; i < 5; i++) {
        EXPECT_TRUE(rb.push(TestStruct(i, "rvalue_" + std::to_string(i))));
    }

    // 测试左值 - 使用命名对象（左值）
    for (int i = 5; i < 10; i++) {
        TestStruct obj(i, "lvalue_" + std::to_string(i));
        EXPECT_TRUE(rb.push(obj));
    }

    // 弹出并验证所有元素
    TestStruct value;
    for (int i = 0; i < 10; i++) {
        EXPECT_TRUE(rb.get(value));
        EXPECT_EQ(value.id, i);

        // 验证数据前缀是否正确
        std::string expected_prefix = (i < 5) ? "rvalue_" : "lvalue_";
        EXPECT_EQ(value.data.substr(0, expected_prefix.length()), expected_prefix);
    }

    // 缓冲区应该为空
    EXPECT_FALSE(rb.get(value));
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}