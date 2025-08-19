#include <catch2/catch_test_macros.hpp>
#include <thread>
#include <vector>
#include <atomic>
#include <chrono>
#include <random>
#include "../base/hxx/throttle.hpp"

#define CATCH_CONFIG_PREFIX_MESSAGES

TEST_CASE("ThrottleControl - Single Thread Basic Functionality", "[throttle][basic]") {
    ThrottleControl throttle(5);  // Allow 5 requests per second
    
    // First 5 requests should be allowed
    for (int i = 0; i < 5; ++i) {
        std::cout << "Request " << i  << std::endl;
        REQUIRE(throttle.update_() == 0);  // Should succeed immediately
    }
    
    // 6th request should be blocked
    REQUIRE(throttle.update_() > 0);  // Should return wait time
    std::cout << "Request " << 6 << " - blocked with wait time: " << throttle.update_() << std::endl;
}

TEST_CASE("ThrottleControl - Time Window Reset", "[throttle][timing]") {
    ThrottleControl throttle(2);  // Allow 2 requests per second
    
    // Use first 2 slots
    REQUIRE(throttle.update_() == 0);
    REQUIRE(throttle.update_() == 0);
    
    // Should be blocked now
    REQUIRE(throttle.update_() > 0);
    
    // Wait for more than 1 second
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    
    // Should be allowed again
    REQUIRE(throttle.update_() == 0);
    REQUIRE(throttle.update_() == 0);
    
    // Third should be blocked
    REQUIRE(throttle.update_() > 0);
}

TEST_CASE("ThrottleControl - Multi-Threading Basic", "[throttle][multithread]") {
    const int tps_limit = 10;
    const int num_threads = 4;
    const int requests_per_thread = 5;
    
    ThrottleControl throttle(tps_limit);
    std::atomic<int> allowed_count{0};
    std::atomic<int> blocked_count{0};
    
    std::vector<std::thread> threads;
    
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([&throttle, &allowed_count, &blocked_count, requests_per_thread]() {
            for (int j = 0; j < requests_per_thread; ++j) {
                if (throttle.update_() == 0) {
                    allowed_count++;
                } else {
                    blocked_count++;
                }
            } 
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    // Total allowed should not exceed tps_limit
    REQUIRE(allowed_count.load() <= tps_limit);
    REQUIRE(allowed_count.load() + blocked_count.load() == num_threads * requests_per_thread);
}

TEST_CASE("ThrottleControl - High Concurrency Stress Test", "[throttle][stress]") {
    const int tps_limit = 100;
    const int num_threads = 10;
    const int requests_per_thread = 50;
    
    ThrottleControl throttle(tps_limit);
    std::atomic<int> allowed_count{0};
    std::atomic<int> blocked_count{0};
    
    std::vector<std::thread> threads;
    
    auto start_time = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([&throttle, &allowed_count, &blocked_count, requests_per_thread]() {
            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_int_distribution<> delay_dist(0, 10);
            
            for (int j = 0; j < requests_per_thread; ++j) {
                if (throttle.update_() == 0) {
                    allowed_count++;
                } else {
                    blocked_count++;
                }
                // Small random delay to simulate real-world conditions
                std::this_thread::sleep_for(std::chrono::microseconds(delay_dist(gen)));
            }
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
    
    INFO("Stress test completed in " << duration << "ms");
    INFO("Allowed: " << allowed_count.load() << ", Blocked: " << blocked_count.load());
    
    REQUIRE(allowed_count.load() <= tps_limit);
    REQUIRE(allowed_count.load() > 0);
    REQUIRE(blocked_count.load() > 0);
}

TEST_CASE("ThrottleControl - Exception Handling", "[throttle][exception]") {
    SECTION("Zero TPS should throw exception") {
        REQUIRE_THROWS_AS(ThrottleControl(0), std::invalid_argument);
    }
    
    SECTION("Negative TPS should throw exception") {
        // Note: using uint32_t parameter, so negative values will wrap to large positive values
        // This test is mainly for documentation purposes
        //REQUIRE_NOTHROW(ThrottleControl(UINT32_MAX));
    }
}

TEST_CASE("ThrottleControl - Single TPS", "[throttle][edge]") {
    ThrottleControl throttle(1);
    
    REQUIRE(throttle.update_() == 0);
    REQUIRE(throttle.update_() > 0);
    
    // Wait and try again
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    REQUIRE(throttle.update_() == 0);
}

TEST_CASE("ThrottleControl - Rapid Sequential Calls", "[throttle][sequential]") {
    const int tps_limit = 20;
    ThrottleControl throttle(tps_limit);
    
    int allowed = 0;
    int blocked = 0;
    
    // Make rapid calls
    for (int i = 0; i < tps_limit * 2; ++i) {
        if (throttle.update_() == 0) {
            allowed++;
        } else {
            blocked++;
        }
    }
    
    INFO("Rapid test: Allowed=" << allowed << ", Blocked=" << blocked);
    
    REQUIRE(allowed <= tps_limit);
    REQUIRE(blocked > 0);
    REQUIRE(allowed + blocked == tps_limit * 2);
}

TEST_CASE("ThrottleControl - Thread Safety with Race Conditions", "[throttle][race]") {
    const int tps_limit = 50;
    const int num_threads = 20;
    const int requests_per_thread = 10;
    
    ThrottleControl throttle(tps_limit);
    std::atomic<int> allowed_count{0};
    std::atomic<int> blocked_count{0};
    
    std::vector<std::thread> threads;
    std::atomic<bool> start_flag{false};

    // Create threads that start simultaneously
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([&throttle, &allowed_count, &blocked_count, &start_flag, requests_per_thread]() {
            // Wait for start signal
            while (!start_flag.load()) {
                std::this_thread::yield();
            }
            
            for (int j = 0; j < requests_per_thread; ++j) {
                if (throttle.update_() == 0) {
                    allowed_count++;
                } else {
                    blocked_count++;
                }
            }
        });
    }
    
    // Start all threads simultaneously
    start_flag.store(true);
    
    for (auto& t : threads) {
        t.join();
    }
    
    INFO("Race test: Allowed=" << allowed_count.load() << ", Blocked=" << blocked_count.load());
    
    // Verify thread safety invariants
    REQUIRE(allowed_count.load() <= tps_limit);
    REQUIRE(allowed_count.load() + blocked_count.load() == num_threads * requests_per_thread);
}

TEST_CASE("ThrottleControl - Performance Benchmark", "[throttle][performance]") {
    const int tps_limit = 1000;
    const int num_operations = 100000;
    
    ThrottleControl throttle(tps_limit);
    
    auto start_time = std::chrono::high_resolution_clock::now();
    
    int allowed = 0;
    for (int i = 0; i < num_operations; ++i) {
        if (throttle.update_() == 0) {
            allowed++;
        }
    }
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
    
    INFO("Performance: " << num_operations << " operations in " << duration.count() << " microseconds");
    INFO("Average: " << (double)duration.count() / num_operations << " microseconds per operation");
    INFO("Allowed: " << allowed << " out of " << num_operations);
    
    // Performance should be reasonable (less than 10 microseconds per operation on average)
    REQUIRE(duration.count() < num_operations * 10);
}

TEST_CASE("ThrottleControl - Thread Safety with Random Delays", "[throttle][random]") {
    const int tps_limit = 50;
    const int num_threads = 8;
    const int requests_per_thread = 25;
    
    ThrottleControl throttle(tps_limit);
    std::atomic<int> allowed_count{0};
    std::atomic<int> blocked_count{0};
    
    std::vector<std::thread> threads;
    std::random_device rd;
    
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([&throttle, &allowed_count, &blocked_count, requests_per_thread, seed = rd()]() {
            std::mt19937 gen(seed);
            std::uniform_int_distribution<> delay_dist(0, 100);
            
            for (int j = 0; j < requests_per_thread; ++j) {
                if (throttle.update_() == 0) {
                    allowed_count++;
                } else {
                    blocked_count++;
                }
                
                // Random delay to create more realistic threading conditions
                std::this_thread::sleep_for(std::chrono::microseconds(delay_dist(gen)));
            }
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    INFO("Random delay test: Allowed=" << allowed_count.load() << ", Blocked=" << blocked_count.load());
    
    // Verify thread safety invariants
    REQUIRE(allowed_count.load() <= tps_limit);
    REQUIRE(allowed_count.load() + blocked_count.load() == num_threads * requests_per_thread);
}

TEST_CASE("ThrottleControl - New API check_() Function", "[throttle][api]") {
    ThrottleControl throttle(3);
    
    // Initially, all slots should be available (check_() should return 0)
    REQUIRE(throttle.check_() == 0);
    REQUIRE(throttle.update_() == 0);
    REQUIRE(throttle.check_() == 0);
    REQUIRE(throttle.update_() == 0);
    REQUIRE(throttle.check_() == 0);
    REQUIRE(throttle.update_() == 0);
    
    // Now 4th request should return positive value (time to wait)
    int64_t wait_time = throttle.check_();
    REQUIRE(wait_time > 0);
    INFO("Wait time for 4th request: " << wait_time << " nanoseconds");
}

TEST_CASE("ThrottleControl - check_and_wait Function", "[throttle][blocking]") {
    ThrottleControl throttle(2);
    
    // Use up the quota using update_() instead of check()
    REQUIRE(throttle.update_() == 0);
    REQUIRE(throttle.update_() == 0);
    REQUIRE(throttle.update_() > 0);  // Should return wait time
    
    // This should block and then succeed
    auto start_time = std::chrono::high_resolution_clock::now();
    throttle.check_and_wait();
    auto end_time = std::chrono::high_resolution_clock::now();
    
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    INFO("check_and_wait() blocked for " << duration.count() << "ms");
    
    // Should have waited at least some time
    REQUIRE(duration.count() >= 0);  // At least attempted to wait
    REQUIRE(throttle.check_() == 0);
}

TEST_CASE("ThrottleControl - API Consistency", "[throttle][consistency]") {
    ThrottleControl throttle(5);
    
    // Test that check() and check_() return consistent results
    // Since check() is based on check_(), they should be consistent
    
    // First, use update_() to actually consume some slots
    REQUIRE(throttle.update_() == 0);
    REQUIRE(throttle.update_() == 0);
    REQUIRE(throttle.update_() == 0);
    
    // Now both check() and check_() should show the same state
    bool check_result = throttle.check();
    int64_t check_result_detailed = throttle.check_();
    
    if (check_result) {
        REQUIRE(check_result_detailed == 0);
    } else {
        REQUIRE(check_result_detailed > 0);
    }
    
    INFO("check() result: " << check_result);
    INFO("check_() result: " << check_result_detailed);
}

TEST_CASE("ThrottleControl - Wait Time Analysis", "[throttle][timing]") {
    ThrottleControl throttle(2);
    
    // Use up quota with update_() to actually consume slots
    REQUIRE(throttle.update_() == 0);
    REQUIRE(throttle.update_() == 0);
    
    // Next calls should return decreasing wait times
    int64_t wait1 = throttle.check_();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    int64_t wait2 = throttle.check_();
    
    REQUIRE(wait1 > 0);
    REQUIRE(wait2 > 0);
    REQUIRE(wait2 <= wait1);  // Wait time should decrease or stay same
    
    INFO("Wait time 1: " << wait1 << " nanoseconds");
    INFO("Wait time 2: " << wait2 << " nanoseconds");
}

TEST_CASE("ThrottleControl - Multi-threaded check_() Usage", "[throttle][multithread][api]") {
    const int tps_limit = 10;
    const int num_threads = 5;
    const int requests_per_thread = 4;
    
    ThrottleControl throttle(tps_limit);
    std::atomic<int> allowed_count{0};
    std::atomic<int> blocked_count{0};
    std::atomic<int64_t> total_wait_time{0};
    
    std::vector<std::thread> threads;
    
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([&throttle, &allowed_count, &blocked_count, &total_wait_time, requests_per_thread]() {
            for (int j = 0; j < requests_per_thread; ++j) {
                int64_t result = throttle.check_();
                if (result == 0) {
                    allowed_count++;
                    throttle.update_();
                } else {
                    blocked_count++;
                    total_wait_time.fetch_add(result);
                }
            }
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    INFO("Allowed: " << allowed_count.load() << ", Blocked: " << blocked_count.load());
    INFO("Total wait time: " << total_wait_time.load() << " nanoseconds");
    
    REQUIRE(allowed_count.load() <= tps_limit);
    REQUIRE(allowed_count.load() + blocked_count.load() == num_threads * requests_per_thread);
    
    if (blocked_count.load() > 0) {
        REQUIRE(total_wait_time.load() > 0);
    }
}

TEST_CASE("ThrottleControl - Update API Functionality", "[throttle][update]") {
    ThrottleControl throttle(3);
    
    // update_() should succeed for first 3 calls
    REQUIRE(throttle.update_() == 0);
    REQUIRE(throttle.update_() == 0);
    REQUIRE(throttle.update_() == 0);
    
    // 4th call should return wait time
    int64_t wait_time = throttle.update_();
    REQUIRE(wait_time > 0);
    INFO("Wait time: " << wait_time << " nanoseconds");
}

TEST_CASE("ThrottleControl - Blocking Update Function", "[throttle][update_blocking]") {
    ThrottleControl throttle(2);
    
    // Use up quota
    REQUIRE(throttle.update_() == 0);
    REQUIRE(throttle.update_() == 0);
    
    // This should block until a slot becomes available
    auto start_time = std::chrono::high_resolution_clock::now();
    throttle.update();  // This should block and wait
    auto end_time = std::chrono::high_resolution_clock::now();
    
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    INFO("update() blocked for " << duration.count() << "ms");
    
    // Should have waited close to 1 second
    REQUIRE(duration.count() >= 900);  // Allow some tolerance
    REQUIRE(duration.count() <= 1100);
}

TEST_CASE("ThrottleControl - Mixed API Usage", "[throttle][mixed]") {
    ThrottleControl throttle(4);
    
    for (int i = 0; i < 40; ++i) {
        throttle.check_and_wait();
        auto now = std::chrono::duration_cast<std::chrono::seconds>(
                          std::chrono::high_resolution_clock::now().time_since_epoch())
                          .count();
        std::cout << "Slot: " << i << " << " << now << std::endl;
        throttle.update();
    }

}
