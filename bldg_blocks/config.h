#pragma once

#include <string>
#include <unordered_map>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <memory>
#include <variant>
#include <vector>

namespace BldgBlocks {

// Simple TOML-like config reader (subset implementation)
class Config {
public:
    using Value = std::variant<std::string, int64_t, double, bool>;
    using Section = std::unordered_map<std::string, Value>;
    
private:
    std::unordered_map<std::string, Section> sections_;
    std::string config_path_;
    std::string project_root_;
    
    // Singleton instance
    static std::unique_ptr<Config> instance_;
    
    Config() : sections_(), config_path_(), project_root_() {}
    
    std::string trim(const std::string& str) {
        size_t first = str.find_first_not_of(" \t\n\r");
        if (first == std::string::npos) return "";
        size_t last = str.find_last_not_of(" \t\n\r");
        return str.substr(first, (last - first + 1));
    }
    
    Value parseValue(const std::string& val) {
        std::string v = trim(val);
        
        // Remove quotes if present
        if ((v.front() == '"' && v.back() == '"') || 
            (v.front() == '\'' && v.back() == '\'')) {
            return v.substr(1, v.length() - 2);
        }
        
        // Boolean
        if (v == "true") return true;
        if (v == "false") return false;
        
        // Try integer
        try {
            size_t pos;
            int64_t i = std::stoll(v, &pos);
            if (pos == v.length()) return i;
        } catch (...) {}
        
        // Try double
        try {
            size_t pos;
            double d = std::stod(v, &pos);
            if (pos == v.length()) return d;
        } catch (...) {}
        
        // Default to string
        return v;
    }
    
public:
    static Config& getInstance() {
        if (!instance_) {
            instance_ = std::unique_ptr<Config>(new Config());
        }
        return *instance_;
    }
    
    bool load(const std::string& path) {
        config_path_ = path;
        
        // Determine project root (parent of config file)
        std::filesystem::path config_full_path = std::filesystem::absolute(path);
        project_root_ = config_full_path.parent_path().string();
        
        std::ifstream file(path);
        if (!file.is_open()) {
            return false;
        }
        
        std::string line;
        std::string current_section = "global";
        
        while (std::getline(file, line)) {
            line = trim(line);
            
            // Skip comments and empty lines
            if (line.empty() || line[0] == '#') continue;
            
            // Section header
            if (line[0] == '[' && line.back() == ']') {
                current_section = line.substr(1, line.length() - 2);
                if (sections_.find(current_section) == sections_.end()) {
                    sections_[current_section] = Section();
                }
                continue;
            }
            
            // Key-value pair
            size_t eq_pos = line.find('=');
            if (eq_pos != std::string::npos) {
                std::string key = trim(line.substr(0, eq_pos));
                std::string value = trim(line.substr(eq_pos + 1));
                
                if (sections_.find(current_section) == sections_.end()) {
                    sections_[current_section] = Section();
                }
                sections_[current_section][key] = parseValue(value);
            }
        }
        
        return true;
    }
    
    template<typename T>
    T get(const std::string& section, const std::string& key, const T& default_value = T{}) const {
        auto sec_it = sections_.find(section);
        if (sec_it == sections_.end()) return default_value;
        
        auto key_it = sec_it->second.find(key);
        if (key_it == sec_it->second.end()) return default_value;
        
        if (auto* val = std::get_if<T>(&key_it->second)) {
            return *val;
        }
        
        // Handle type conversions for common cases
        if constexpr (std::is_same_v<T, std::string>) {
            if (auto* val = std::get_if<int64_t>(&key_it->second)) {
                return std::to_string(*val);
            }
            if (auto* val = std::get_if<double>(&key_it->second)) {
                return std::to_string(*val);
            }
            if (auto* val = std::get_if<bool>(&key_it->second)) {
                return *val ? "true" : "false";
            }
        }
        
        return default_value;
    }
    
    // Get path relative to project root
    std::string getPath(const std::string& section, const std::string& key) const {
        std::string path = get<std::string>(section, key, "");
        if (path.empty()) return "";
        
        // If absolute path, return as-is
        if (std::filesystem::path(path).is_absolute()) {
            return path;
        }
        
        // Otherwise, make it relative to project root
        return project_root_ + "/" + path;
    }
    
    // Convenience methods for common paths
    std::string getLogsDir() const {
        std::string dir = getPath("paths", "logs_dir");
        if (dir.empty()) dir = project_root_ + "/logs";
        
        // Create directory if it doesn't exist
        std::filesystem::create_directories(dir);
        return dir;
    }
    
    std::string getCacheDir() const {
        std::string dir = getPath("paths", "cache_dir");
        if (dir.empty()) dir = project_root_ + "/cache";
        
        // Create directory if it doesn't exist
        std::filesystem::create_directories(dir);
        return dir;
    }
    
    std::string getDataDir() const {
        std::string dir = getPath("paths", "data_dir");
        if (dir.empty()) dir = project_root_ + "/data";
        
        // Create directory if it doesn't exist
        std::filesystem::create_directories(dir);
        return dir;
    }
    
    // Get logging configuration
    bool isLoggingEnabled() const {
        return get<bool>("logging", "enabled", true);
    }
    
    std::string getLogLevel() const {
        return get<std::string>("logging", "level", "INFO");
    }
    
    std::string getLogFilePrefix() const {
        return get<std::string>("logging", "file_prefix", "trading");
    }
    
    int64_t getLogBufferSize() const {
        return get<int64_t>("logging", "buffer_size", 16384);
    }
    
    // Get memory pool configuration
    bool isNumaAware() const {
        return get<bool>("memory_pool", "numa_aware", true);
    }
    
    int64_t getNumaNode() const {
        return get<int64_t>("memory_pool", "numa_node", -1);
    }
    
    // Get thread configuration
    bool isCpuAffinityEnabled() const {
        return get<bool>("threading", "cpu_affinity_enabled", true);
    }
    
    int64_t getThreadPoolSize() const {
        return get<int64_t>("threading", "thread_pool_size", 4);
    }
    
    void print() const {
        for (const auto& [section, values] : sections_) {
            std::cout << "[" << section << "]" << std::endl;
            for (const auto& [key, value] : values) {
                std::cout << "  " << key << " = ";
                std::visit([](const auto& v) { std::cout << v; }, value);
                std::cout << std::endl;
            }
        }
    }
};

// Initialize static member
std::unique_ptr<Config> Config::instance_ = nullptr;

// Global config accessor
inline Config& config() {
    return Config::getInstance();
}

// Initialize config (call this at program start)
inline bool initConfig(const std::string& config_path = "config.toml") {
    return config().load(config_path);
}

} // namespace BldgBlocks