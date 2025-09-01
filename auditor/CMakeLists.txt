# ClaudeAuditor - Ultra-Low Latency Code Auditor
cmake_minimum_required(VERSION 3.20)

# Create auditor library
add_library(ClaudeAuditor STATIC
    claude_auditor.cpp
    claude_auditor.h
)

# Set strict compiler flags (same as main project)
target_compile_options(ClaudeAuditor PRIVATE
    -Wall
    -Wextra
    -Werror
    -Wpedantic
    -Wconversion
    -Wsign-conversion
    -Wold-style-cast
    -Wformat-security
    -Weffc++
    -Wno-unused
    -march=native
    -O3
)

# Include directories
target_include_directories(ClaudeAuditor PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}
)

# Link against Common library
target_link_libraries(ClaudeAuditor
    CommonImpl
    pthread
)

# Create auditor executable
add_executable(claude_audit
    audit_main.cpp
)

target_link_libraries(claude_audit
    ClaudeAuditor
    CommonImpl
    pthread
)

# Install targets
install(TARGETS ClaudeAuditor claude_audit
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)

# Install headers
install(FILES claude_auditor.h
    DESTINATION include/auditor
)

# Add custom target for pre-commit hook
add_custom_target(audit
    COMMAND claude_audit --source ${CMAKE_SOURCE_DIR} --fail-on-critical
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Running Claude Auditor..."
)

# Add as a test
enable_testing()
add_test(NAME ClaudeAudit
    COMMAND claude_audit --source ${CMAKE_SOURCE_DIR} --junit audit_results.xml
)