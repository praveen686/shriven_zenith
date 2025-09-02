# CMake generated Testfile for 
# Source directory: /home/isoula/om/shriven_zenith/auditor
# Build directory: /home/isoula/om/shriven_zenith/cmake/build-release/auditor
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test([=[ClaudeAudit]=] "/home/isoula/om/shriven_zenith/cmake/build-release/auditor/claude_audit" "--source" "/home/isoula/om/shriven_zenith" "--junit" "audit_results.xml")
set_tests_properties([=[ClaudeAudit]=] PROPERTIES  _BACKTRACE_TRIPLES "/home/isoula/om/shriven_zenith/auditor/CMakeLists.txt;70;add_test;/home/isoula/om/shriven_zenith/auditor/CMakeLists.txt;0;")
