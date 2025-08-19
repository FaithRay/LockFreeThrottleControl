#include <atomic>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

class ThrottleControl
{
public:
    ThrottleControl(uint32_t tps) : buffer_size_(tps), duration_(1000000000LL), timestamps_(tps)
    {
        if (tps == 0) {
            throw std::invalid_argument("TPS must be positive");
        }
        for (int i = 0; i < tps; ++i) {
            timestamps_[i].store(0, std::memory_order_relaxed);
        }
    }

    int64_t check_()
    {
        int64_t now = std::chrono::duration_cast<std::chrono::nanoseconds>(
                          std::chrono::high_resolution_clock::now().time_since_epoch())
                          .count();

        int current_index = index_.load(std::memory_order_acq_rel) % buffer_size_;
        int64_t expected = timestamps_[current_index].load(std::memory_order_acquire);

        return now - expected > duration_ ? 0 : duration_ - (now - expected);
    }

    int64_t update_()
    {
        int64_t now = std::chrono::duration_cast<std::chrono::nanoseconds>(
                          std::chrono::high_resolution_clock::now().time_since_epoch())
                          .count();

        for (int attempt = 0; attempt < buffer_size_; ++attempt) {
            int current_index = index_.load(std::memory_order_acq_rel) % buffer_size_;

            int64_t expected = timestamps_[current_index].load(std::memory_order_acquire);

            if (now - expected > duration_) {
                int32_t next_index = (current_index + 1) % buffer_size_;
                if (index_.compare_exchange_weak(current_index, next_index, std::memory_order_acq_rel,
                                                 std::memory_order_acquire)) {
                    if (timestamps_[current_index].compare_exchange_weak(expected, now, std::memory_order_acq_rel,
                                                                         std::memory_order_acquire)) {
                        return 0;
                    } else {
                        assert(false && "Failed to update timestamp");
                    }
                }
            } else {
                return duration_ - (now - expected);
            }
        }

        return duration_;
    }

    bool check() { return check_() == 0; }

    void update()
    {
        while (update_() > 0) {
            std::this_thread::yield();
        }
    }

    void check_and_wait()
    {
        int64_t remain = check_();
        if (remain > 0) {
            std::this_thread::sleep_for(std::chrono::nanoseconds(remain));
        }
    }

    std::string toString() const
    {
        std::string result;
        // current thread id
        result += "Thread:" + std::to_string(std::hash<std::thread::id>()(std::this_thread::get_id()));
        result += ",Index: " + std::to_string(index_.load(std::memory_order_acquire)) + ",data:";
        for (const auto &ts : timestamps_) {
            result += std::to_string(ts.load(std::memory_order_acquire)) + " ";
        }
        return result;
    }

private:
    uint32_t buffer_size_;
    int64_t duration_;
    std::vector<std::atomic<int64_t>> timestamps_;
    std::atomic<int> index_{0};
};
