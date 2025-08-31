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
#include "../bldg_blocks/config.h"
#include "../bldg_blocks/logging.h"

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
    std::cout << "BldgBlocks Examples Runner\n";
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
    
    // Initialize configuration
    std::string config_path = "config.toml";
    
    // Check if config file exists in current dir or parent dir
    if (!std::filesystem::exists(config_path)) {
        if (std::filesystem::exists("../config.toml")) {
            config_path = "../config.toml";
        } else if (std::filesystem::exists("../../config.toml")) {
            config_path = "../../config.toml";
        }
    }
    
    if (BldgBlocks::initConfig(config_path)) {
        std::cout << "Loaded configuration from: " << config_path << "\n";
        
        // Set environment variable for logger to use
        std::string logs_dir = BldgBlocks::config().getLogsDir();
        setenv("SHRIVEN_LOGS_DIR", logs_dir.c_str(), 1);
        std::cout << "Logs will be written to: " << logs_dir << "\n";
        
        // Create logs directory if it doesn't exist
        std::string mkdir_cmd = "mkdir -p " + logs_dir;
        [[maybe_unused]] int result = system(mkdir_cmd.c_str());
        
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
        BldgBlocks::initLogging(log_file);
    } else {
        std::cout << "Warning: Could not load config from " << config_path << ", using defaults\n";
    }
    
    std::cout << "=== BldgBlocks Examples ===\n";
    std::cout << "Running: " << example_name << "\n\n";
    
    // Log the start of example
    if (BldgBlocks::g_opt_logger) {
        BldgBlocks::g_opt_logger->info("Starting example: %s", example_name.c_str());
    }
    
    try {
        examples[example_name]();
    } catch (const std::exception& e) {
        if (BldgBlocks::g_opt_logger) {
            BldgBlocks::g_opt_logger->error("Example failed: %s", e.what());
        }
        std::cout << "Error running example: " << e.what() << std::endl;
        BldgBlocks::shutdownLogging();
        return 1;
    }
    
    // Log completion
    if (BldgBlocks::g_opt_logger) {
        BldgBlocks::g_opt_logger->info("Example completed successfully: %s", example_name.c_str());
        
        // Print stats
        auto stats = BldgBlocks::g_opt_logger->getStats();
        std::cout << "\nLogging Statistics:\n";
        std::cout << "  Messages written: " << stats.messages_written << "\n";
        std::cout << "  Messages dropped: " << stats.messages_dropped << "\n";
        std::cout << "  Bytes written: " << stats.bytes_written << "\n";
    }
    
    std::cout << "\n=== Example completed successfully ===\n";
    
    // Cleanup
    BldgBlocks::shutdownLogging();
    return 0;
}