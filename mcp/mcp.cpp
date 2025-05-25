#define NOMINMAX

#include <iostream>
#include <string>
#include <fstream>
#include <sstream>  // Added for std::stringstream
#include <windows.h>
#include <shlobj.h>
#include <filesystem> // Requires C++17
#include "../util/json.hpp" // Assuming nlohmann/json is in this relative path
#include <chrono>
#include <csignal>
#include <ctime>
#include <iomanip>
#include <limits> // Required for std::numeric_limits
#include <optional>
#include <vector> // For required array
#include <map>
#include <functional>
#include <thread>   // Added for heartbeat thread
#include <atomic>   // Added for stop flag
#include <mutex>    // Added for thread safety

using json = nlohmann::json;

// Mutexes for thread-safe output
std::mutex cout_mutex;
std::mutex log_mutex;

const DWORD NOTEPAD_TIMEOUT_MS = 6 * 60 * 1000;

// 根据编译变体设置不同的文本内容
#if defined(MCP_CURSOR)
const std::string TIMEOUT_TEXT =
  std::string(
    "**Important:**\nI'm still thinking about it, My thoughts aren't fully clear yet. Please use the mcp 'ask' tool to ask me if there are any new tasks again right now.\n") +
  std::string(
    "**Important:**\nI might repeat this same response. Don't worry, this is normal. It means I'm still thinking. Please continue asking me and don't give up.");
const std::string EXT_TEXT =
  "\n\n**Important:**\nWhen this task or answer is completed, use the mcp 'ask' tool to ask me if there are any new tasks.";
#elif defined(MCP_WINDSURF)
const std::string TIMEOUT_TEXT =
  std::string(
    "**Important:**\nI'm still thinking about it, My thoughts aren't fully clear yet. Please use the mcp 'ask' tool to ask me if there are any new tasks again right now.\n") +
  std::string(
    "**Important:**\nI might repeat this same response. Don't worry, this is normal. It means I'm still thinking. Please continue asking me and don't give up.");
const std::string EXT_TEXT =
  "\n\n**Important:**\nWhen this answer is completed, use the mcp 'ask' tool to ask me if there are any new question.";
#endif

// --- get_timestamp function remains unchanged ---
std::string get_timestamp() {
  auto now = std::chrono::system_clock::now();
  auto time = std::chrono::system_clock::to_time_t(now);
  std::tm local_tm;
  localtime_s(&local_tm, &time);
  std::stringstream ss;
  ss << std::put_time(&local_tm, "%Y-%m-%d %H:%M:%S");
  return ss.str();
}

// --- log_message function - MODIFIED FOR THREAD SAFETY ---
void log_message(const std::string &message) {
  std::lock_guard<std::mutex> lock(log_mutex); // Ensure thread-safe access to log file
  std::ofstream log_file("mcp_log.txt", std::ios::app);
  if (log_file.is_open()) { log_file << "[" << get_timestamp() << "] " << message << std::endl; }
  else {
    // Fallback to cerr if log file fails (cerr is generally more thread-safe for simple messages)
    std::cerr << "[" << get_timestamp() << "] Log Error: " << message << std::endl;
  }
}

// --- 编码转换辅助函数 ---
// Helper function to convert std::string from system ANSI codepage to UTF-8
std::optional<std::string> AnsiToUtf8(const std::string &ansi_str) {
  if (ansi_str.empty()) {
    return std::string();
  }
  int wide_len = MultiByteToWideChar(CP_ACP, MB_ERR_INVALID_CHARS, ansi_str.c_str(), static_cast<int>(ansi_str.length()), NULL, 0);
  if (wide_len == 0) {
    log_message("Error in AnsiToUtf8: MultiByteToWideChar (calc length) failed or invalid char. Error: " + std::to_string(GetLastError()));
    return std::nullopt;
  }
  std::wstring wide_str(wide_len, 0);
  if (MultiByteToWideChar(CP_ACP, MB_ERR_INVALID_CHARS, ansi_str.c_str(), static_cast<int>(ansi_str.length()), &wide_str[0], wide_len) == 0) {
    log_message("Error in AnsiToUtf8: MultiByteToWideChar (convert) failed. Error: " + std::to_string(GetLastError()));
    return std::nullopt;
  }
  int utf8_len = WideCharToMultiByte(CP_UTF8, 0, wide_str.c_str(), static_cast<int>(wide_str.length()), NULL, 0, NULL, NULL);
  if (utf8_len == 0) {
    log_message("Error in AnsiToUtf8: WideCharToMultiByte (calc length) failed. Error: " + std::to_string(GetLastError()));
    return std::nullopt;
  }
  std::string utf8_str(utf8_len, 0);
  if (WideCharToMultiByte(CP_UTF8, 0, wide_str.c_str(), static_cast<int>(wide_str.length()), &utf8_str[0], utf8_len, NULL, NULL) == 0) {
    log_message("Error in AnsiToUtf8: WideCharToMultiByte (convert) failed. Error: " + std::to_string(GetLastError()));
    return std::nullopt;
  }
  return utf8_str;
}

// Helper function to convert std::string (as raw bytes) from UTF-16LE to UTF-8
std::optional<std::string> Utf16leBytesToUtf8(const std::string &utf16le_raw_bytes) {
  if (utf16le_raw_bytes.empty()) {
    return std::string();
  }
  if (utf16le_raw_bytes.length() % sizeof(wchar_t) != 0) {
    log_message("Warning in Utf16leBytesToUtf8: Raw byte string length is not a multiple of sizeof(wchar_t). Potential truncation.");
  }
  const wchar_t *wide_str_ptr = reinterpret_cast<const wchar_t *>(utf16le_raw_bytes.c_str());
  int num_wide_chars = static_cast<int>(utf16le_raw_bytes.length() / sizeof(wchar_t));
  if (num_wide_chars == 0 && !utf16le_raw_bytes.empty()) {
    log_message("Error in Utf16leBytesToUtf8: Not enough bytes for even one UTF-16 char.");
    return std::nullopt;
  }
  int utf8_len = WideCharToMultiByte(CP_UTF8, 0, wide_str_ptr, num_wide_chars, NULL, 0, NULL, NULL);
  if (utf8_len == 0) {
    log_message("Error in Utf16leBytesToUtf8: WideCharToMultiByte (calc length) failed. Error: " + std::to_string(GetLastError()));
    return std::nullopt;
  }
  std::string utf8_str(utf8_len, 0);
  if (WideCharToMultiByte(CP_UTF8, 0, wide_str_ptr, num_wide_chars, &utf8_str[0], utf8_len, NULL, NULL) == 0) {
    log_message("Error in Utf16leBytesToUtf8: WideCharToMultiByte (convert) failed. Error: " + std::to_string(GetLastError()));
    return std::nullopt;
  }
  return utf8_str;
}

// Function to attempt to convert a string (read as raw bytes) to UTF-8
std::string ensure_utf8(const std::string &raw_input_bytes) {
  if (raw_input_bytes.empty()) {
    return "";
  }
  // 1. Check for UTF-8 BOM (EF BB BF)
  if (raw_input_bytes.length() >= 3 &&
      static_cast<unsigned char>(raw_input_bytes[0]) == 0xEF &&
      static_cast<unsigned char>(raw_input_bytes[1]) == 0xBB &&
      static_cast<unsigned char>(raw_input_bytes[2]) == 0xBF) {
    log_message("ensure_utf8: Detected UTF-8 BOM. Stripping BOM and using as UTF-8.");
    return raw_input_bytes.substr(3);
  }
  // 2. Check for UTF-16LE BOM (FF FE)
  if (raw_input_bytes.length() >= 2 &&
      static_cast<unsigned char>(raw_input_bytes[0]) == 0xFF &&
      static_cast<unsigned char>(raw_input_bytes[1]) == 0xFE) {
    log_message("ensure_utf8: Detected UTF-16 LE BOM. Converting from UTF-16LE.");
    if (auto converted = Utf16leBytesToUtf8(raw_input_bytes.substr(2))) {
      return *converted;
    } else {
      log_message("ensure_utf8: UTF-16LE BOM detected, but Utf16leBytesToUtf8 conversion failed. Falling back.");
    }
  }
  // 3. Check for UTF-16BE BOM (FE FF)
  if (raw_input_bytes.length() >= 2 &&
      static_cast<unsigned char>(raw_input_bytes[0]) == 0xFE &&
      static_cast<unsigned char>(raw_input_bytes[1]) == 0xFF) {
    log_message("ensure_utf8: Detected UTF-16 BE BOM. Conversion not implemented yet. Falling back.");
  }
  // 4. If no BOM, attempt ANSI to UTF-8 conversion
  log_message("ensure_utf8: No definitive Unicode BOM found or prior conversion failed. Attempting ANSI to UTF-8 conversion.");
  if (auto converted_from_ansi = AnsiToUtf8(raw_input_bytes)) {
    log_message("ensure_utf8: ANSI to UTF-8 conversion successful.");
    return *converted_from_ansi;
  } else {
    log_message("ensure_utf8: ANSI to UTF-8 conversion also failed. Returning original bytes as a last resort (may cause errors).");
    return raw_input_bytes;
  }
}

// Function to write to stdout in a thread-safe manner
void write_to_stdout(const json &data, const std::string &log_prefix = "") {
  try {
    std::string data_str;
    if (data.is_string()) {
      data_str = data.get<std::string>();
    } else {
      data_str = data.dump();
    }

    {
      // Lock cout only for the duration of printing
      std::lock_guard<std::mutex> lock(cout_mutex);
      std::cout << data_str << std::endl; // std::endl also flushes
    }

    // Optional logging
    if (!log_prefix.empty()) {
      log_message(log_prefix + ": " + data_str);
    }
  }
  catch (const std::exception &e) {
    log_message("Error writing to stdout: " + std::string(e.what()));
  }
}

// --- execute_notepad_edit function with 3-minute timeout and improved process termination ---
std::string execute_notepad_edit(const std::string &cmd = "") {
  wchar_t temp_path[MAX_PATH];
  if (GetTempPathW(MAX_PATH, temp_path) == 0) { return "Error: Unable to get temporary directory path"; }
  std::wstring temp_file_path = std::wstring(temp_path) + L"notepad_edit_" + std::to_wstring(GetTickCount64()) +
                                L".txt";
  std::ofstream file(temp_file_path);
  if (!file.is_open()) { return "Error: Unable to create temporary file"; }
  if (!cmd.empty()) { file << cmd; }
  file.close();

  SHELLEXECUTEINFOW sei = {0};
  sei.cbSize = sizeof(SHELLEXECUTEINFOW);
  sei.fMask = SEE_MASK_NOCLOSEPROCESS;
  sei.lpVerb = L"open";
  sei.lpFile = L"notepad.exe";
  sei.lpParameters = temp_file_path.c_str();
  sei.nShow = SW_SHOW;

  if (!ShellExecuteExW(&sei) || !sei.hProcess) {
    DeleteFileW(temp_file_path.c_str());
    return "Error: Unable to start Notepad";
  }

  // Store the process ID for potential fallback termination
  DWORD notepadProcessId = GetProcessId(sei.hProcess);
  log_message("Started notepad process with ID: " + std::to_string(notepadProcessId));

  // Wait for notepad to close with a timeout
  DWORD wait_result = WaitForSingleObject(sei.hProcess, NOTEPAD_TIMEOUT_MS);

  // Check if the wait timed out
  if (wait_result == WAIT_TIMEOUT) {
    log_message("Notepad input timed out after 3 minutes. Attempting to terminate notepad process.");

    // First try to find and close the notepad window gracefully
    bool gracefulTermination = false;
    HWND notepadWindow = FindWindowW(L"Notepad", NULL);
    if (notepadWindow) {
      log_message("Found notepad window, attempting graceful termination first.");
      // Try to send WM_CLOSE message to notepad
      if (SendMessageW(notepadWindow, WM_CLOSE, 0, 0)) {
        // Wait briefly to see if it closes
        if (WaitForSingleObject(sei.hProcess, 1000) != WAIT_TIMEOUT) {
          log_message("Notepad closed gracefully.");
          gracefulTermination = true;
        }
      }
    }

    // If graceful termination failed, use TerminateProcess
    if (!gracefulTermination) {
      log_message("Attempting to forcefully terminate notepad process.");
      if (!TerminateProcess(sei.hProcess, 1)) {
        DWORD error = GetLastError();
        log_message("Failed to terminate notepad process with TerminateProcess. Error code: " + std::to_string(error));

        // Fallback: use taskkill command
        log_message("Attempting fallback termination with taskkill command.");
        std::string taskkillCmd = "taskkill /F /PID " + std::to_string(notepadProcessId);
        system(taskkillCmd.c_str());

        // Wait briefly to see if taskkill worked
        if (WaitForSingleObject(sei.hProcess, 1000) == WAIT_TIMEOUT) {
          log_message("Warning: All attempts to terminate notepad failed. Process may still be running.");
        } else {
          log_message("Notepad terminated successfully using taskkill.");
        }
      } else {
        log_message("Notepad terminated successfully using TerminateProcess.");
      }
    }

    // Clean up resources
    CloseHandle(sei.hProcess);

    // Delete the temporary file
    if (DeleteFileW(temp_file_path.c_str())) {
      log_message("Temporary file deleted successfully.");
    } else {
      DWORD error = GetLastError();
      log_message("Failed to delete temporary file. Error code: " + std::to_string(error));
    }

    // Return the specified message
    return TIMEOUT_TEXT;
  }

  // Normal case - notepad was closed by the user
  log_message("Notepad closed normally by user.");
  CloseHandle(sei.hProcess);

  std::ifstream input_file(temp_file_path, std::ios::binary);
  if (!input_file.is_open()) {
    DeleteFileW(temp_file_path.c_str());
    return "Error: Unable to read temporary file";
  }

  std::string content((std::istreambuf_iterator<char>(input_file)), std::istreambuf_iterator<char>());
  input_file.close();

  // --- BEGIN Added Logging ---
  // 记录原始字节日志和BOM
  if (!content.empty()) {
    std::stringstream hex_bytes_log;
    hex_bytes_log << "Notepad raw content (length " << content.length() << ", first ~64 bytes hex): ";
    for (size_t i = 0; i < std::min(content.length(), static_cast<size_t>(64)); ++i) {
      hex_bytes_log << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(static_cast<unsigned char>(content[i])) << " ";
    }
    log_message(hex_bytes_log.str());

    // 检查BOM
    if (content.length() >= 2) {
      if (static_cast<unsigned char>(content[0]) == 0xFF && static_cast<unsigned char>(content[1]) == 0xFE) {
        log_message("Notepad content: Detected UTF-16 LE BOM.");
      } else if (static_cast<unsigned char>(content[0]) == 0xFE && static_cast<unsigned char>(content[1]) == 0xFF) {
        log_message("Notepad content: Detected UTF-16 BE BOM.");
      }
    }
    if (content.length() >= 3) {
      if (static_cast<unsigned char>(content[0]) == 0xEF && static_cast<unsigned char>(content[1]) == 0xBB && static_cast<unsigned char>(content[2]) == 0xBF) {
        log_message("Notepad content: Detected UTF-8 BOM.");
      }
    }
  } else {
    log_message("Notepad content: Raw content is empty.");
  }
  // --- END Added Logging ---

  DeleteFileW(temp_file_path.c_str());

  // 编码转换
  std::string utf8_content = ensure_utf8(content);
  if (content.length() > 0 && utf8_content.empty() && content != utf8_content) {
    log_message("Warning: ensure_utf8 produced empty string from non-empty input. Original length: " + std::to_string(content.length()));
  } else if (content != utf8_content) {
    log_message("Content encoding converted/adjusted by ensure_utf8. Original length: " + std::to_string(content.length()) + ", New length: " +
                std::to_string(utf8_content.length()));
  }
  return utf8_content;
}

// Tool structure to represent a tool with metadata and execution logic
struct Tool {
  std::string name;
  std::string description;
  json parameters; // Parameters schema
  std::vector<std::string> required_parameters;
  std::function<json(const json &)> execute; // Function to execute the tool

  // Generate input schema from tool definition
  json generateInputSchema() const {
    json schema;
    schema["type"] = "object";
    schema["properties"] = parameters;
    schema["required"] = json::array();
    for (const auto &param: required_parameters) {
      schema["required"].push_back(param);
    }
    return schema;
  }

  // Generate tool description for tools/list
  json generateToolDescription() const {
    json tool;
    tool["name"] = name;
    tool["description"] = description;
    tool["inputSchema"] = generateInputSchema();
    return tool;
  }
};

// ToolRegistry to manage all available tools
class ToolRegistry {
  private:
  std::map<std::string, Tool> tools;

  public:
  // Register a new tool
  void registerTool(const Tool &tool) {
    tools[tool.name] = tool;
  }

  // Check if a tool exists
  bool hasTool(const std::string &name) const {
    return tools.find(name) != tools.end();
  }

  // Get a tool by name
  const Tool &getTool(const std::string &name) const {
    if (!hasTool(name)) {
      throw std::runtime_error("Unknown tool name: " + name);
    }
    return tools.at(name);
  }

  // Get all tools
  std::vector<Tool> getAllTools() const {
    std::vector<Tool> result;
    for (const auto &pair: tools) {
      result.push_back(pair.second);
    }
    return result;
  }

  // Execute a tool by name
  json executeTool(const std::string &name, const json &arguments) const {
    const Tool &tool = getTool(name);

    try {
      // Validate required parameters
      for (const auto &required: tool.required_parameters) {
        if (!arguments.contains(required)) {
          throw std::runtime_error("Missing required parameter '" + required + "' for " + name + " tool");
        }

        // Type validation can be more sophisticated if needed
        // if (!arguments[required].is_string())
        // {
        //     throw std::runtime_error("Parameter '" + required + "' for " + name + " tool must be a string");
        // }
      }

      // Execute the tool and catch any exceptions it might throw
      return tool.execute(arguments);
    }
    catch (const std::exception &e) {
      // Log the error for debugging purposes
      log_message("Error executing tool '" + name + "': " + e.what());

      // Re-throw to be handled at higher level
      throw;
    }
  }
};

// Global tool registry
ToolRegistry g_toolRegistry;

// --- handle_initialize_request function remains unchanged ---
json handle_initialize_request() {
  json result;
  result["serverInfo"] = {
    {"name",    "mcp_local"},
    {"version", "0.1.0"}
  };
  result["capabilities"] = {
    {"resources", json::object()},
    {"tools",     json::object()}
  };
  result["protocolVersion"] = "2024-11-05";
  return result;
}

// Handle tools/list request - now uses the tool registry
json handle_tools_list_request() {
  json result;
  result["tools"] = json::array();

  // Get all tools from registry and add their descriptions
  auto tools = g_toolRegistry.getAllTools();
  for (const auto &tool: tools) {
    result["tools"].push_back(tool.generateToolDescription());
  }

  return result;
}

// Handle tools/call request - now uses the tool registry
json handle_tools_call_request(const json &request) {
  // Validate full request structure first
  if (!request.contains("params")) {
    throw std::runtime_error("Missing 'params' field in tools/call request");
  }

  const auto &params = request["params"];
  if (!params.is_object()) {
    throw std::runtime_error("The 'params' field must be an object in tools/call request");
  }

  // Validate name parameter
  if (!params.contains("name")) {
    throw std::runtime_error("Missing 'name' field in tools/call params");
  }

  if (!params["name"].is_string()) {
    throw std::runtime_error("The 'name' field must be a string in tools/call params");
  }

  std::string tool_name = params["name"].get<std::string>();

  // Check if the tool exists
  if (!g_toolRegistry.hasTool(tool_name)) {
    throw std::runtime_error("Unknown tool name: '" + tool_name + "'");
  }

  // Validate arguments parameter
  if (!params.contains("arguments")) {
    throw std::runtime_error("Missing 'arguments' field in tools/call params");
  }

  if (!params["arguments"].is_object()) {
    throw std::runtime_error("The 'arguments' field must be an object in tools/call params");
  }

  const json &arguments = params["arguments"];

  // Execute tool from registry (which will perform its own validation of required parameters)
  // Note: This call might block for a long time
  return g_toolRegistry.executeTool(tool_name, arguments);
}

// --- Heartbeat Function ---
void send_heartbeat(std::atomic<bool> &stop_flag, json progress_token) {
  std::string token_str;
  if (progress_token.is_string()) {
    token_str = progress_token.get<std::string>();
  } else if (progress_token.is_number()) {
    token_str = progress_token.dump(); // Use JSON serialization for numbers
  } else {
    // Fallback if token is weird, though it should be valid from request ID
    token_str = "invalid_token_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
    log_message("Warning: Heartbeat using fallback token string: " + token_str);
  }

  log_message("Heartbeat thread started for token: " + token_str);

  while (!stop_flag.load(std::memory_order_relaxed)) {
    // Sleep for 5 seconds
    std::this_thread::sleep_for(std::chrono::seconds(5));

    // Check the flag again after waking up, before sending
    if (stop_flag.load(std::memory_order_relaxed)) {
      break;
    }

    // Construct the LSP-style progress notification
    json heartbeat_notification;
    heartbeat_notification["jsonrpc"] = "2.0";
    heartbeat_notification["method"] = "$/progress"; // Standard LSP progress method
    heartbeat_notification["params"] = {
      {"token", progress_token}, // Use the original JSON token (string or number)
      {
       "value", {
                  {"kind", "report"}, // Indicates an update
                  {"message", "Processing tool call..."} // Simple heartbeat message
                }
      }
    };

    // Serialize and send the notification safely using the write_to_stdout function
    write_to_stdout(heartbeat_notification, "Sent heartbeat for token: " + token_str);
  }
  log_message("Heartbeat thread stopped for token: " + token_str);
}

// --- process_request function - MODIFIED FOR HEARTBEAT ---
json process_request(const json &request) {
  json response;
  response["jsonrpc"] = "2.0";
  // Ensure we capture the ID early for use in heartbeat and response
  json request_id = request.contains("id") ? request["id"] : nullptr;
  response["id"] = request_id;

  log_message("Processing request ID: " + (request_id.is_null() ? "null" : request_id.dump()));


  try {
    // Check for required JSON-RPC fields
    if (!request.contains("jsonrpc") || request["jsonrpc"] != "2.0") {
      throw std::runtime_error("Invalid or missing jsonrpc version. Must be 2.0");
    }

    if (!request.contains("method") || !request["method"].is_string()) {
      throw std::runtime_error("Missing or invalid method parameter");
    }

    std::string method = request["method"].get<std::string>();

    if (method == "initialize") {
      response["result"] = handle_initialize_request();
    } else if (method == "tools/list") {
      response["result"] = handle_tools_list_request();
    } else if (method == "tools/call") {
      // --- Heartbeat Integration START ---
      if (request_id.is_null()) {
        log_message(
          "Warning: tools/call received without a valid ID. Cannot start heartbeat or send response reliably.");
        throw std::runtime_error("tools/call requires a valid request ID.");
      }
      if (!request_id.is_string() && !request_id.is_number()) {
        log_message("Warning: tools/call request ID is neither string nor number. Using fallback for heartbeat token.");
        // Let heartbeat function handle fallback token generation if needed, but log here.
      }

      std::atomic<bool> stop_heartbeat_flag(false);
      std::thread heartbeat_thread;
      json progress_token = request_id; // Use the request ID as the progress token

      log_message("Attempting to start heartbeat thread for token: " + progress_token.dump());
      // Launch heartbeat thread
      heartbeat_thread = std::thread(send_heartbeat, std::ref(stop_heartbeat_flag), progress_token);
      // --- Heartbeat Integration END ---

      json tool_result;
      bool tool_call_succeeded = false;
      try {
        // Execute the actual tool call (potentially long-running)
        log_message("Executing tool call...");
        tool_result = handle_tools_call_request(request);
        response["result"] = tool_result; // Assign result on success
        tool_call_succeeded = true;
        log_message("Tool call execution finished successfully.");
      }
      catch (...) {
        // Catch any exception from handle_tools_call_request or g_toolRegistry.executeTool
        log_message("Exception caught during tool execution. Preparing to stop heartbeat and rethrow...");
        // Signal heartbeat thread to stop (will be joined below)
        stop_heartbeat_flag.store(true, std::memory_order_relaxed);
        // Clean up heartbeat thread immediately
        if (heartbeat_thread.joinable()) {
          heartbeat_thread.join();
          log_message("Heartbeat thread joined after exception during tool call.");
        } else {
          log_message("Warning: Heartbeat thread was not joinable after exception.");
        }
        throw; // Re-throw the exception to be caught by the outer error handlers
      }

      // --- Heartbeat Cleanup on Success START ---
      if (tool_call_succeeded) {
        log_message("Tool execution successful. Stopping heartbeat thread...");
        stop_heartbeat_flag.store(true, std::memory_order_relaxed);
        if (heartbeat_thread.joinable()) {
          heartbeat_thread.join();
          log_message("Heartbeat thread joined after successful tool call.");
        } else {
          log_message("Warning: Heartbeat thread was not joinable after success.");
        }
      }
      // --- Heartbeat Cleanup on Success END ---
    } else {
      // Method not found error - JSON-RPC error code -32601
      log_message("Method not found: " + method);
      response["error"] = {
        {"code",    -32601},
        {"message", "Method not found: " + method}
      };
      response.erase("result"); // Remove result field on error
      // No return here, let it fall through to the end
    }
  }
  catch (const json::exception &e) {
    // JSON parse error or invalid parameters - JSON-RPC error code -32602
    log_message(std::string("JSON processing error: ") + e.what());
    response["error"] = {
      {"code",    -32602},
      {"message", "Invalid parameters or JSON structure error"},
      {"data",    e.what()}
    };
    response.erase("result");
  }
  catch (const std::runtime_error &e) {
    // Application specific error (including missing params, unknown tool, etc.)
    // JSON-RPC error code -32000 to -32099 (using -32000 generically)
    log_message("Runtime error: " + std::string(e.what()));
    response["error"] = {
      {"code",    -32000},
      {"message", e.what()}
    };
    response.erase("result");
  }
  catch (const std::exception &e) {
    // Internal error - JSON-RPC error code -32603
    log_message("Internal error: " + std::string(e.what()));
    response["error"] = {
      {"code",    -32603},
      {"message", "Internal server error"},
      {"data",    e.what()}
    };
    response.erase("result");
  }
  catch (...) {
    // Unknown error - JSON-RPC error code -32603
    log_message("Unknown exception caught in process_request");
    response["error"] = {
      {"code",    -32603},
      {"message", "Unknown internal server error"}
    };
    response.erase("result");
  }

  // Final logging of the response before returning
  if (response.contains("error")) {
    log_message("Sending error response: " + response.dump());
  } else {
    log_message("Sending success response: " + response.dump());
  }
  return response;
}


// --- handle_signal function remains unchanged ---
void handle_signal(int signal) {
  if (signal == SIGINT) {
    log_message("Received SIGINT (Ctrl+C), exiting gracefully.");
    // Consider signaling any active heartbeat threads to stop here if needed,
    // although exiting might be sufficient depending on requirements.
    exit(0);
  }
}

// Initialize tools - register all tools in the registry
void initialize_tools() {
  // Shell execute tool
  Tool shell_execute_tool;
  shell_execute_tool.name = "shell_execute";
  shell_execute_tool.description = "Execute shell command via temporary file and notepad (blocks until notepad closes)";
  // Clarified description
  shell_execute_tool.parameters = {
    {
      "cmd",
      {{"type", "string"}, {"description", "Shell command or text content to place in temporary file for notepad"}}
    }
  };
  shell_execute_tool.required_parameters = {"cmd"};
  shell_execute_tool.execute = [](const json &arguments) -> json {
    std::string cmd = arguments["cmd"].get<std::string>();
    log_message("Executing shell_execute tool with cmd/content: " + cmd);
    std::string result_text = execute_notepad_edit(cmd); // This call blocks
    log_message("shell_execute tool finished, result length: " + std::to_string(result_text.length()));

    // Create the content array with proper object structure
    json contentItem = {
      {"type", "text"},
      {"text", result_text}
    };

    return {{"content", json::array({contentItem})}};
  };

  // Ask tool
  Tool ask_tool;
  ask_tool.name = "ask";
  ask_tool.description = "Ask user a question via temporary file and notepad (blocks until notepad closes)";
  // Clarified description
  ask_tool.parameters = {
    {"question", {{"type", "string"}, {"description", "Question or prompt to display in notepad"}}}
  };
  ask_tool.required_parameters = {"question"};
  ask_tool.execute = [](const json &arguments) -> json {
    std::string question = arguments["question"].get<std::string>();
    log_message("Executing ask tool with question: " + question);
    std::string response_text = execute_notepad_edit(question); // This call blocks
    log_message("ask tool finished, response length: " + std::to_string(response_text.length()));

    // Check if the response is not an error message or timeout message
    if (response_text != TIMEOUT_TEXT &&
        response_text.substr(0, 6) != "Error:" &&
        !response_text.empty()) {
      // For normal responses, append the requested message
      response_text += EXT_TEXT;
      log_message("Added standard message to normal response");

      // --- BEGIN Added Logging ---
      log_message("ask_tool: Combined response_text with EXT_TEXT length: " + std::to_string(response_text.length()));
      std::stringstream combined_hex_bytes_log;
      combined_hex_bytes_log << "ask_tool: Combined response_text (first ~64 bytes hex): ";
      for (size_t i = 0; i < std::min(response_text.length(), static_cast<size_t>(64)); ++i) {
        combined_hex_bytes_log << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(static_cast<unsigned char>(response_text[i])) << " ";
      }
      log_message(combined_hex_bytes_log.str());
      // --- END Added Logging ---
    }

    // Create the content array with proper object structure
    json contentItem = {
      {"type", "text"},
      {"text", response_text}
    };

    return {{"content", json::array({contentItem})}};
  };

  // Register tools
  g_toolRegistry.registerTool(shell_execute_tool);
  g_toolRegistry.registerTool(ask_tool);
}

// --- main function - now includes tool initialization ---
int main() {
  signal(SIGINT, handle_signal);
  signal(SIGTERM, handle_signal);

  log_message("MCP service started, initializing tools...");

  // Initialize and register all tools
  try {
    initialize_tools();
    log_message("Tools initialized successfully");
  }
  catch (const std::exception &e) {
    log_message("Error initializing tools: " + std::string(e.what()));
    return 1;
  }

  log_message("Waiting for JSON-RPC input via stdin...");
  std::string input_line;
  while (true) {
    // Read line safely
    {
      // No need for mutex here as stdin reading is synchronous in the main loop
      if (!std::getline(std::cin, input_line)) {
        if (std::cin.eof()) {
          log_message("Standard input closed (EOF), program exiting.");
          break; // Exit loop on EOF
        }
        if (std::cin.fail()) {
          // Attempt to recover from potential bad state (e.g., binary input)
          log_message("Standard input stream error (fail bit set). Clearing state and ignoring line.");
          std::cin.clear(); // Clear error flags
          // Discard the problematic input up to the next newline
          std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
          continue; // Try reading next line
        }
        // Other unhandled stream error
        log_message("Input stream encountered an unknown issue, exiting.");
        break; // Exit loop on other errors
      }
    } // End of stdin reading block

    if (input_line.empty()) {
      log_message("Received empty line, ignoring.");
      continue;
    }

    log_message("Raw input received: " + input_line);

    json response;
    try {
      json request = json::parse(input_line);
      log_message("Parsed request: " + request.dump());

      // Ignore notifications (requests without an ID or with null ID)
      // The check is now also implicitly done in process_request for tools/call
      if (!request.contains("id") || request["id"].is_null()) {
        if (request.contains("method")) {
          std::string method_name = request["method"].is_string()
                                    ? request["method"].get<std::string>()
                                    : "[invalid method]";
          log_message("Received notification (method: " + method_name + "), ignoring.");
        } else {
          log_message("Received notification (no method), ignoring: " + request.dump());
        }
        continue; // Skip processing notifications
      }

      // Process valid requests (those with an ID)
      response = process_request(request);
    }
    catch (const json::parse_error &e) {
      log_message("JSON parsing error: " + std::string(e.what()) + " on input: " + input_line);
      // Construct JSON-RPC Parse Error response (-32700)
      response = {
        {"jsonrpc", "2.0"},
        {"id",      nullptr}, // ID is unknown if parsing failed
        {
         "error",   {
                      {"code", -32700},
                      {"message", "Parse error"},
                      {"data", e.what()}
                    }
        }
      };
    }
    catch (const std::exception &e) {
      // Catch any other unexpected errors during parsing or initial checks in main
      log_message("Unexpected error in main processing loop: " + std::string(e.what()));
      response = {
        {"jsonrpc", "2.0"},
        {"id",      nullptr}, // ID might be unknown if error happened before extraction
        {
         "error",   {
                      {"code", -32603}, // Internal Error
                      {"message", "Internal server error in main loop"},
                      {"data", e.what()}
                    }
        }
      };
    }
    catch (...) {
      // Catch truly unknown exceptions
      log_message("Caught unknown exception type in main loop!");
      response = {
        {"jsonrpc", "2.0"},
        {"id",      nullptr},
        {
         "error",   {
                      {"code", -32603},
                      {"message", "An unknown internal server error occurred."}
                    }
        }
      };
    }

    // Send the response (if one was generated)
    // process_request now handles constructing both success and error responses with IDs
    if (!response.empty()) {
      // Use the write_to_stdout function to send the response
      write_to_stdout(response);
    } else {
      // This case should ideally not happen if process_request always returns a response
      // for requests with IDs, or if parsing fails.
      log_message("Warning: No response generated for input line: " + input_line);
    }
  } // End while(true) loop

  log_message("MCP service shutting down.");
  return 0;
}
