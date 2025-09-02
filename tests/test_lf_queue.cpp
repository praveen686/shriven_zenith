#include <iostream>
#include <cassert>
#include "../common/lf_queue.h"

struct TestData {
    int value;
    char padding[60]; // Make it cache-line sized
};

int main() {
    std::cout << "Testing SPSCLFQueue..." << std::endl;
    
    // Test 1: Construction and destruction
    {
        Common::SPSCLFQueue<TestData, 1024> queue;
        std::cout << "✓ Queue constructed successfully" << std::endl;
        std::cout << "✓ Capacity: " << queue.capacity() << std::endl;
        assert(queue.capacity() == 1024);
        assert(queue.size() == 0);
    }
    std::cout << "✓ Queue destroyed successfully (no segfault!)" << std::endl;
    
    // Test 2: Basic enqueue/dequeue
    {
        Common::SPSCLFQueue<TestData, 16> queue;
        
        // Producer writes
        auto* write_slot = queue.getNextToWriteTo();
        assert(write_slot != nullptr);
        write_slot->value = 42;
        queue.updateWriteIndex();
        
        assert(queue.size() == 1);
        
        // Consumer reads
        auto* read_slot = queue.getNextToRead();
        assert(read_slot != nullptr);
        assert(read_slot->value == 42);
        queue.updateReadIndex();
        
        assert(queue.size() == 0);
        std::cout << "✓ Basic enqueue/dequeue works" << std::endl;
    }
    
    // Test 3: Queue full handling
    {
        Common::SPSCLFQueue<TestData, 4> queue;
        
        // Fill the queue
        for (int i = 0; i < 4; ++i) {
            auto* slot = queue.getNextToWriteTo();
            assert(slot != nullptr);
            slot->value = i;
            queue.updateWriteIndex();
        }
        
        // Queue should be full now
        auto* slot = queue.getNextToWriteTo();
        assert(slot == nullptr);
        std::cout << "✓ Queue full detection works" << std::endl;
        
        // Read one element
        auto* read_slot = queue.getNextToRead();
        assert(read_slot != nullptr);
        queue.updateReadIndex();
        
        // Now we should be able to write again
        slot = queue.getNextToWriteTo();
        assert(slot != nullptr);
        std::cout << "✓ Queue wrap-around works" << std::endl;
    }
    
    std::cout << "\n✅ All tests passed! The lf_queue memory bug is fixed." << std::endl;
    return 0;
}