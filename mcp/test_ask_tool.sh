#!/bin/bash

# Test script for the "ask" tool in mcp.cpp

# Path to the mcp executable
MCP_EXECUTABLE="../cmake-build-release/mcp_local.exe"

# Check if mcp executable exists
if [ ! -f "$MCP_EXECUTABLE" ]; then
    echo "Error: $MCP_EXECUTABLE not found!"
    echo "Please make sure you have built the mcp program and it's in the current directory."
    exit 1
fi

# Function to send a JSON-RPC request to mcp and get the response
send_request() {
    local request="$1"
    echo "Sending request: $request"
    echo "$request" | $MCP_EXECUTABLE
}

# Ask tool request with a sample question - format based on actual logs
ask_tool_request='{"method":"tools/call","params":{"name":"ask","arguments":{"question":"✅ Task complete. Next task? (Leave blank or say  to stop)"}},"jsonrpc":"2.0","id":1}'

echo "3. Testing ask tool..."
echo "A Notepad window will open. Enter your response and close Notepad to continue."
send_request "$ask_tool_request"
echo

echo "=== Test completed ==="
