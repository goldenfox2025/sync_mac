#pragma once
#include <atomic>
#include <cassert>
#include <cstddef>
#include <iostream>

template <typename store_type, uint32_t size>
class ring_buffer {
private:
    static_assert((size >= 2) && ((size & (size - 1)) == 0),
                  "Size must be a power of 2 and at least 2");
    struct Cell {
        std::atomic<uint32_t> sequence;
        store_type data;
    };
    Cell buffer_[size];

    const uint32_t mask_ = size - 1;

    // 入队位置，使用alignas避免伪共享
    alignas(64) std::atomic<uint32_t> enqueue_pos_{0};

    // 出队位置，使用alignas避免伪共享
    alignas(64) std::atomic<uint32_t> dequeue_pos_{0};

public:

    ring_buffer() {
        for (uint32_t i = 0; i < size; ++i) {
            buffer_[i].sequence.store(i, std::memory_order_relaxed);
        }
    }

    ~ring_buffer() {}

    bool push(store_type value) {
        Cell* cell;
        uint32_t pos = enqueue_pos_.load(std::memory_order_relaxed);

        for (;;) {
            // 计算单元格索引并获取单元格
            cell = &buffer_[pos & mask_];

            // 获取单元格的序列号，使用seq_cst确保看到最新值
            uint32_t seq = cell->sequence.load(std::memory_order_seq_cst);

            // 计算序列号与位置的差值
            // 使用有符号整数差值，处理序列号回绕
            int32_t diff = static_cast<int32_t>(seq - pos);

            // 如果序列号等于位置，说明单元格可写入
            if (diff == 0) {
                // 尝试原子地更新入队位置
                // 这确保了在多生产者环境中，每个位置只能被一个生产者写入
                if (enqueue_pos_.compare_exchange_strong(  // 使用strong版本减少伪失败
                        pos, pos + 1,
                        std::memory_order_seq_cst)) {  // 使用seq_cst确保全局内存顺序
                    break;  // 成功获取写入权限
                }
            }
            // 如果序列号小于位置，说明缓冲区已满
            else if (diff < 0) {
                return false;
            }
            // 如果序列号大于位置，说明有其他生产者已经更新了入队位置
            else {
                pos = enqueue_pos_.load(std::memory_order_seq_cst);  // 使用seq_cst确保看到最新值
            }
        }

      
   
        // 写入数据
        cell->data = std::move(value);


        cell->sequence.store(pos + 1, std::memory_order_seq_cst);

        return true;
    }

    /**
     * 从缓冲区获取元素
     *
     * @param value 用于存储获取的元素
     * @return 成功返回true，缓冲区为空返回false
     */
    bool get(store_type& value) {
        Cell* cell;
        uint32_t pos = dequeue_pos_.load(std::memory_order_relaxed);

        for (;;) {
            // 计算单元格索引并获取单元格
            cell = &buffer_[pos & mask_];

            // 获取单元格的序列号，使用seq_cst确保看到最新值
            uint32_t seq = cell->sequence.load(std::memory_order_seq_cst);

            // 计算序列号与位置+1的差值
            // 使用有符号整数差值，处理序列号回绕
            int32_t diff = static_cast<int32_t>(seq - (pos + 1));

            // 如果序列号等于位置+1，说明单元格可读取
            if (diff == 0) {
                // 尝试原子地更新出队位置
                // 这确保了在多消费者环境中，每个值只能被一个消费者读取
                if (dequeue_pos_.compare_exchange_strong(  // 使用strong版本减少伪失败
                        pos, pos + 1,
                        std::memory_order_seq_cst)) {  // 使用seq_cst确保全局内存顺序
                    break;  // 成功获取读取权限
                }
            }
            // 如果序列号小于位置+1，说明缓冲区为空
            else if (diff < 0) {
                return false;
            }
            // 如果序列号大于位置+1，说明有其他消费者已经更新了出队位置
            else {
                pos = dequeue_pos_.load(std::memory_order_seq_cst);  // 使用seq_cst确保看到最新值
            }
        }



        // 再次检查序列号，确保没有其他线程修改了单元格状态
        uint32_t seq_check = cell->sequence.load(std::memory_order_seq_cst);
        if (seq_check != pos + 1) {

            uint32_t expected = pos + 1;
            dequeue_pos_.compare_exchange_strong(expected, pos, std::memory_order_seq_cst);
            return false;
        }

        // 读取数据
        value = std::move(cell->data);


        cell->sequence.store(pos + size, std::memory_order_seq_cst);

        return true;
    }

    /**
     * 获取缓冲区容量
     *
     * @return 缓冲区容量
     */
    constexpr size_t capacity() const {
        return size;
    }


};