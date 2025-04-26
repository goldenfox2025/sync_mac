#pragma once
#include <atomic>
#include <cassert>
#include <cstddef>
#include <utility>

template <typename T, uint32_t size>
class ring_buffer {
    static_assert((size >= 2) && ((size & (size - 1)) == 0),
                  "Size must be a power of 2 and at least 2");

    struct Cell {
        std::atomic<uint32_t> sequence;
        T data;
    };
    Cell buffer_[size];

    // fast modulo mask
    static constexpr uint32_t mask_     = size - 1;
    // 2*size 周期的 sequence 掩码
    static constexpr uint32_t seq_mask_ = (size << 1) - 1;

    alignas(64) std::atomic<uint32_t> enqueue_pos_{0};
    alignas(64) std::atomic<uint32_t> dequeue_pos_{0};

public:
    ring_buffer() {
        // 初始化 sequence=i
        for (uint32_t i = 0; i < size; ++i) {
            buffer_[i].sequence.store(i, std::memory_order_relaxed);
        }
    }

    // 入队
    bool push(T value) {
        uint32_t pos = enqueue_pos_.load(std::memory_order_relaxed);
        while (true) {
            auto* cell = &buffer_[pos & mask_];
            uint32_t seq = cell->sequence.load(std::memory_order_acquire);
            uint32_t expect_empty = pos & seq_mask_;          // 本轮该是空的标记
            uint32_t expect_full  = (expect_empty + 1) & seq_mask_;  // 本轮满的标记

            if (seq == expect_empty) {
                // 抢位置
                if (enqueue_pos_.compare_exchange_weak(
                        pos, pos + 1,
                        std::memory_order_relaxed, std::memory_order_relaxed)) {
                    // 写数据
                    cell->data = std::move(value);
                    // 标记为可读
                    cell->sequence.store(expect_full, std::memory_order_release);
                    return true;
                }
                // CAS 失败后 pos 被更新为最新值，继续重试
            }
            else if (seq == expect_full) {
                // 上一轮数据还没被消费，队满
                return false;
            }
            else {
                // 其他生产者在操作，拉最新的 pos 重试
                pos = enqueue_pos_.load(std::memory_order_relaxed);
            }
        }
    }

    // 出队
    bool get(T& value) {
        uint32_t pos = dequeue_pos_.load(std::memory_order_relaxed);
        while (true) {
            auto* cell = &buffer_[pos & mask_];
            uint32_t seq = cell->sequence.load(std::memory_order_acquire);
            uint32_t expect_full  = (pos + 1) & seq_mask_;      // 本轮可读的标记
            uint32_t expect_empty = pos & seq_mask_;            // 本轮空的标记

            if (seq == expect_full) {
                // 抢位置
                if (dequeue_pos_.compare_exchange_weak(
                        pos, pos + 1,
                        std::memory_order_relaxed, std::memory_order_relaxed)) {
                    // 读数据
                    value = std::move(cell->data);
                    // 标记为下轮可写
                    cell->sequence.store((pos + size) & seq_mask_, std::memory_order_release);
                    return true;
                }
            }
            else if (seq == expect_empty) {
                // 还没生产过，队空
                return false;
            }
            else {
                // 其他消费者在操作，拉最新的 pos 重试
                pos = dequeue_pos_.load(std::memory_order_relaxed);
            }
        }
    }

    constexpr size_t capacity() const {
        return size;
    }
};
