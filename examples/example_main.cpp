#include <iostream>
#include <string>
#include <map>
#include <functional>
#include <cstdlib>
#include <filesystem>
#include <chrono>
#include <ctime>

#include "mem_pool_examples.h"
#include "lf_queue_examples.h" 
#include "thread_examples.h"
#include "../common/logging.h"

// Forward declarations
static void showUsage();

// Example registry
std::map<std::string, std::function<void()>> examples = {
    {"mem_pool", memPoolExamples},
    {"lf_queue", lockFreeQueueExamples}, 
    {"thread_utils", threadUtilsExamples},
    {"all", []() {
        std::cout << "Running all examples...\n\n";
        memPoolExamples();
        lockFreeQueueExamples();
        threadUtilsExamples();
    }}
};

static void showUsage() {
    std::cout << "Common Examples Runner\n";
    std::cout << "Usage: examples <example_name>\n\n";
    std::cout << "Available examples:\n";
    for (const auto& [name, func] : examples) {
        std::cout << "  " << name << "\n";
    }
    std::cout << "\nExample: examples mem_pool\n";
    std::cout << "Example: examples all\n";
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        showUsage();
        return 1;
    }
    
    std::string example_name = argv[1];
    
    if (examples.find(example_name) == examples.end()) {
        std::cout << "Unknown example: " << example_name << "\n\n";
        showUsage();
        return 1;
    }
    
    // Initialize logging with default path
    std::string logs_dir = "logs";
    
    // Create logs directory if it doesn't exist
    if (!std::filesystem::exists(logs_dir)) {
        std::filesystem::create_directories(logs_dir);
    }
    
    // Initialize global logger with path based on module/example name
    // Use the example name as the module identifier
    std::string module_name = (example_name == "all") ? "examples" : example_name;
    
    // Add timestamp for unique log files per run (optional)
    auto now = std::chrono::system_clock::now();
    auto time_point = std::chrono::system_clock::to_time_t(now);
    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y%m%d_%H%M%S", localtime(&time_point));
    
    // Construct log filename: <module>_<timestamp>.log
    std::string log_file = logs_dir + "/" + module_name + "_" + timestamp + ".log";
    std::cout << "Log file: " << log_file << "\n";
    Common::initLogging(log_file);
    
    std::cout << "=== Common Examples ===\n";
    std::cout << "Running: " << example_name << "\n\n";
    
    // Log the start of example
    if (Common::g_logger) {
        Common::g_logger->info("Starting example: %s", example_name.c_str());
    }
    
    try {
        examples[example_name]();
    } catch (const std::exception& e) {
        if (Common::g_logger) {
            Common::g_logger->error("Example failed: %s", e.what());
        }
        std::cout << "Error running example: " << e.what() << std::endl;
        Common::shutdownLogging();
        return 1;
    }
    
    // Log completion
    if (Common::g_logger) {
        Common::g_logger->info("Example completed successfully: %s", example_name.c_str());
        
        // Print stats
        auto stats = Common::g_logger->getStats();
        std::cout << "\nLogging Statistics:\n";
        std::cout << "  Messages written: " << stats.messages_written << "\n";
        std::cout << "  Messages dropped: " << stats.messages_dropped << "\n";
        std::cout << "  Bytes written: " << stats.bytes_written << "\n";
    }
    
    std::cout << "\n=== Example completed successfully ===\n";
    
    // Cleanup
    Common::shutdownLogging();
    return 0;
}