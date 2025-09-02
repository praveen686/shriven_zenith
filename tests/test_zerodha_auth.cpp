#include "trading/auth/zerodha/zerodha_auth.h"
#include "config/config.h"
#include "common/logging.h"
#include <cstdio>
#include <cstring>
#include <unistd.h>

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    
    printf("=== Zerodha Authentication Test ===\n");
    
    // Step 1: Initialize ConfigManager
    printf("1. Initializing ConfigManager...\n");
    if (!Trading::ConfigManager::init()) {
        fprintf(stderr, "Failed to initialize ConfigManager\n");
        return 1;
    }
    printf("   ConfigManager initialized successfully\n");
    const auto& config = Trading::ConfigManager::getConfig();
    printf("   Env file: %s\n", config.paths.env_file);
    printf("   Logs dir: %s\n", config.paths.logs_dir);
    
    // Step 2: Initialize logging to auth logs directory
    char log_file[512];
    snprintf(log_file, sizeof(log_file), "%s/test_zerodha.log", Trading::ConfigManager::getLogsDir());
    if (log_file[0] == '\0') {
        fprintf(stderr, "Failed to get log file path\n");
        return 1;
    }
    printf("2. Initializing logging to: %s\n", log_file);
    Common::initLogging(log_file);
    
    LOG_INFO("=== Starting Zerodha Authentication Test ===");
    
    // Step 3: Load credentials from environment (loaded by ConfigManager)
    printf("3. Loading credentials from environment...\n");
    Trading::Zerodha::Credentials creds{};
    
    const char* user_id = getenv("ZERODHA_USER_ID");
    const char* password = getenv("ZERODHA_PASSWORD");
    const char* totp_secret = getenv("ZERODHA_TOTP_SECRET");
    const char* api_key = getenv("ZERODHA_API_KEY");
    const char* api_secret = getenv("ZERODHA_API_SECRET");
    
    if (!user_id || !password || !totp_secret || !api_key || !api_secret) {
        LOG_ERROR("Missing required environment variables");
        fprintf(stderr, "Missing required environment variables\n");
        fprintf(stderr, "Please ensure .env file contains:\n");
        fprintf(stderr, "  ZERODHA_USER_ID\n");
        fprintf(stderr, "  ZERODHA_PASSWORD\n");
        fprintf(stderr, "  ZERODHA_TOTP_SECRET\n");
        fprintf(stderr, "  ZERODHA_API_KEY\n");
        fprintf(stderr, "  ZERODHA_API_SECRET\n");
        Common::shutdownLogging();
        return 1;
    }
    
    strncpy(creds.user_id, user_id, sizeof(creds.user_id) - 1);
    strncpy(creds.password, password, sizeof(creds.password) - 1);
    strncpy(creds.totp_secret, totp_secret, sizeof(creds.totp_secret) - 1);
    strncpy(creds.api_key, api_key, sizeof(creds.api_key) - 1);
    strncpy(creds.api_secret, api_secret, sizeof(creds.api_secret) - 1);
    
    printf("   Loaded credentials for user: %s\n", creds.user_id);
    LOG_INFO("Loaded credentials for user: %s", creds.user_id);
    
    // Step 4: Test TOTP generation
    printf("4. Testing TOTP generation...\n");
    Trading::Zerodha::TOTPGenerator totp_gen;
    if (!totp_gen.init(creds.totp_secret)) {
        LOG_ERROR("Failed to initialize TOTP generator");
        fprintf(stderr, "Failed to initialize TOTP generator\n");
        Common::shutdownLogging();
        return 1;
    }
    
    char totp_code[8];
    if (!totp_gen.generate(totp_code, sizeof(totp_code))) {
        LOG_ERROR("Failed to generate TOTP");
        fprintf(stderr, "Failed to generate TOTP\n");
        Common::shutdownLogging();
        return 1;
    }
    
    printf("   Generated TOTP: %s\n", totp_code);
    LOG_INFO("Generated TOTP successfully");
    
    // Step 5: Initialize ZerodhaAuthManager
    printf("5. Initializing ZerodhaAuthManager...\n");
    if (!Trading::Zerodha::ZerodhaAuthManager::init(creds)) {
        LOG_ERROR("Failed to initialize and authenticate");
        fprintf(stderr, "Failed to initialize and authenticate\n");
        Common::shutdownLogging();
        return 1;
    }
    
    auto* auth = Trading::Zerodha::ZerodhaAuthManager::getInstance();
    if (!auth) {
        LOG_ERROR("Failed to get auth instance");
        fprintf(stderr, "Failed to get auth instance\n");
        Common::shutdownLogging();
        return 1;
    }
    
    // Step 6: Check authentication status
    printf("6. Checking authentication status...\n");
    if (auth->isAuthenticated()) {
        printf("   ✓ Authentication successful!\n");
        printf("   Access token: %.20s...\n", auth->getAccessToken());
        printf("   API key: %s\n", auth->getApiKey());
        LOG_INFO("Authentication successful");
        LOG_INFO("Access token obtained: %.20s...", auth->getAccessToken());
    } else {
        printf("   ✗ Authentication failed\n");
        LOG_ERROR("Authentication failed");
    }
    
    // Step 7: Test token refresh check
    printf("7. Testing token refresh check...\n");
    if (auth->needsRefresh()) {
        printf("   Token needs refresh\n");
        LOG_INFO("Token needs refresh");
        
        if (auth->refreshToken()) {
            printf("   ✓ Token refreshed successfully\n");
            LOG_INFO("Token refreshed successfully");
        } else {
            printf("   ✗ Token refresh failed\n");
            LOG_ERROR("Token refresh failed");
        }
    } else {
        printf("   Token is still valid\n");
        LOG_INFO("Token is still valid");
    }
    
    // Step 8: Test request signing
    printf("8. Testing request signing...\n");
    const char* test_data = "test_order_data";
    char signature[256];
    if (auth->signRequest(test_data, signature, sizeof(signature))) {
        printf("   ✓ Request signed successfully\n");
        printf("   Signature: %.40s...\n", signature);
        LOG_INFO("Request signed successfully");
    } else {
        printf("   ✗ Request signing failed\n");
        LOG_ERROR("Request signing failed");
    }
    
    // Step 9: Fetch and display profile information
    printf("9. Fetching profile information...\n");
    char api_response[8192];
    
    if (auth->fetchProfile(api_response, sizeof(api_response))) {
        printf("   ✓ Profile fetched successfully\n");
        printf("   Profile data:\n");
        // Parse and display key profile fields (simple parsing without JSON library)
        char* parsed_user_id = strstr(api_response, "\"user_id\":\"");
        if (parsed_user_id) {
            parsed_user_id += 11;
            char* end = strchr(parsed_user_id, '"');
            if (end) {
                *end = '\0';
                printf("     - User ID: %s\n", parsed_user_id);
                *end = '"';
            }
        }
        
        char* email = strstr(api_response, "\"email\":\"");
        if (email) {
            email += 9;
            char* end = strchr(email, '"');
            if (end) {
                *end = '\0';
                printf("     - Email: %s\n", email);
                *end = '"';
            }
        }
        
        char* user_name = strstr(api_response, "\"user_name\":\"");
        if (user_name) {
            user_name += 13;
            char* end = strchr(user_name, '"');
            if (end) {
                *end = '\0';
                printf("     - Name: %s\n", user_name);
                *end = '"';
            }
        }
        
        char* broker = strstr(api_response, "\"broker\":\"");
        if (broker) {
            broker += 10;
            char* end = strchr(broker, '"');
            if (end) {
                *end = '\0';
                printf("     - Broker: %s\n", broker);
                *end = '"';
            }
        }
        
        LOG_INFO("Profile fetched: %s", api_response);
    } else {
        printf("   ✗ Failed to fetch profile\n");
        LOG_ERROR("Failed to fetch profile");
    }
    
    // Step 10: Fetch positions
    printf("10. Fetching positions...\n");
    if (auth->fetchPositions(api_response, sizeof(api_response))) {
        printf("   ✓ Positions fetched successfully\n");
        // Check if there are any positions
        if (strstr(api_response, "\"net\":") != nullptr) {
            printf("   Found active positions\n");
        } else {
            printf("   No active positions\n");
        }
        LOG_INFO("Positions response length: %zu", strlen(api_response));
    } else {
        printf("   ✗ Failed to fetch positions\n");
        LOG_ERROR("Failed to fetch positions");
    }
    
    // Step 11: Fetch holdings
    printf("11. Fetching holdings...\n");
    if (auth->fetchHoldings(api_response, sizeof(api_response))) {
        printf("   ✓ Holdings fetched successfully\n");
        // Count holdings
        int holdings_count = 0;
        char* ptr = api_response;
        while ((ptr = strstr(ptr, "\"tradingsymbol\":")) != nullptr) {
            holdings_count++;
            ptr++;
        }
        printf("   Total holdings: %d\n", holdings_count);
        LOG_INFO("Holdings count: %d", holdings_count);
    } else {
        printf("   ✗ Failed to fetch holdings\n");
        LOG_ERROR("Failed to fetch holdings");
    }
    
    // Step 12: Fetch orders
    printf("12. Fetching orders...\n");
    if (auth->fetchOrders(api_response, sizeof(api_response))) {
        printf("   ✓ Orders fetched successfully\n");
        // Count orders
        int orders_count = 0;
        char* ptr = api_response;
        while ((ptr = strstr(ptr, "\"order_id\":")) != nullptr) {
            orders_count++;
            ptr++;
        }
        printf("   Total orders today: %d\n", orders_count);
        LOG_INFO("Orders count: %d", orders_count);
    } else {
        printf("   ✗ Failed to fetch orders\n");
        LOG_ERROR("Failed to fetch orders");
    }
    
    // Step 13: Fetch funds/margins
    printf("13. Fetching funds/margins...\n");
    if (auth->fetchFunds(api_response, sizeof(api_response))) {
        printf("   ✓ Funds fetched successfully\n");
        
        // Parse available cash
        char* cash = strstr(api_response, "\"cash\":");
        if (cash) {
            cash += 7;
            double available_cash = strtod(cash, nullptr);
            printf("   Available cash: %.2f\n", available_cash);
            LOG_INFO("Available cash: %.2f", available_cash);
        }
        
        // Parse available margin
        char* margin = strstr(api_response, "\"available\":{\"adhoc_margin\":");
        if (margin) {
            margin += 29;
            double available_margin = strtod(margin, nullptr);
            printf("   Available margin: %.2f\n", available_margin);
            LOG_INFO("Available margin: %.2f", available_margin);
        }
    } else {
        printf("   ✗ Failed to fetch funds\n");
        LOG_ERROR("Failed to fetch funds");
    }
    
    // Step 14: Cleanup
    printf("14. Cleaning up...\n");
    Trading::Zerodha::ZerodhaAuthManager::shutdown();
    LOG_INFO("=== Zerodha Authentication Test Complete ===");
    Common::shutdownLogging();
    
    printf("\n=== Test Complete ===\n");
    printf("Check log file for details: %s\n", log_file);
    
    return 0;
}