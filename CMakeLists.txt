cmake_minimum_required(VERSION 3.30)
project(windows_tools)

set(CMAKE_CXX_STANDARD 20)

add_executable(export export.cpp)
add_executable(merge merge.cpp)

# 定义Cursor版本，添加特定定义
add_executable(mcp_cursor mcp/mcp.cpp)
target_compile_definitions(mcp_cursor PRIVATE MCP_CURSOR)

# 定义Windsurf版本，添加特定定义
add_executable(mcp_windsurf mcp/mcp.cpp)
target_compile_definitions(mcp_windsurf PRIVATE MCP_WINDSURF)
