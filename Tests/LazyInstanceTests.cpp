#include "emugl/common/lazy_instance.h"
#include <atomic>
#include <cassert>
#include <thread>
#include <vector>

static std::atomic<int> constructions{0};
struct Value {
    int words[128];
    Value() {
        ++constructions;
        for (int i = 0; i < 128; ++i) words[i] = i * 7;
    }
};
static emugl::LazyInstance<Value> singleton = LAZY_INSTANCE_INIT;
int main() {
    std::atomic<bool> start{false};
    std::vector<std::thread> threads;
    for (int i = 0; i < 16; ++i) threads.emplace_back([&] {
        while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
        for (int repeat = 0; repeat < 10000; ++repeat) {
            Value& value = singleton.get();
            for (int n = 0; n < 128; ++n) assert(value.words[n] == n * 7);
        }
    });
    start.store(true, std::memory_order_release);
    for (auto& thread : threads) thread.join();
    assert(constructions == 1);
}
