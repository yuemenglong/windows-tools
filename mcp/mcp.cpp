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

// Function to write to stdout in a thread-safe manner
void write_to_stdout(const json &data, const std::string &log_prefix = "") {
  try {
    std::string data_str;
    if (data.is_string()) {
      data_str = data.get<std::string>();
    } else {
      data_str = data.dump();
    }

    { // Lock cout only for the duration of printing
      std::lock_guard<std::mutex> lock(cout_mutex);
      std::cout << data_str << std::endl; // std::endl also flushes
    }

    // Optional logging
    if (!log_prefix.empty()) {
      log_message(log_prefix + ": " + data_str);
    }
  } catch (const std::exception &e) {
    log_message("Error writing to stdout: " + std::string(e.what()));
  }
}

// --- execute_notepad_edit function with 3-minute timeout ---
std::string execute_notepad_edit(const std::string &cmd = "") {
  // Define timeout constant (3 minutes in milliseconds)
  const DWORD NOTEPAD_TIMEOUT_MS = 3 * 60 * 1000; // 3 minutes
  const std::string TIMEOUT_MESSAGE = "Please summarize the current chat and think about the next steps. Answer in Chinese.";

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

  // Wait for notepad to close with a timeout
  DWORD wait_result = WaitForSingleObject(sei.hProcess, NOTEPAD_TIMEOUT_MS);

  // Check if the wait timed out
  if (wait_result == WAIT_TIMEOUT) {
    log_message("Notepad input timed out after 3 minutes. Terminating notepad process.");

    // Terminate the notepad process
    TerminateProcess(sei.hProcess, 1);
    CloseHandle(sei.hProcess);

    // Delete the temporary file
    DeleteFileW(temp_file_path.c_str());

    // Return the specified message
    return TIMEOUT_MESSAGE;
  }

  // Normal case - notepad was closed by the user
  CloseHandle(sei.hProcess);

  std::ifstream input_file(temp_file_path);
  if (!input_file.is_open()) {
    DeleteFileW(temp_file_path.c_str());
    return "Error: Unable to read temporary file";
  }

  std::string content((std::istreambuf_iterator<char>(input_file)), std::istreambuf_iterator<char>());
  input_file.close();
  DeleteFileW(temp_file_path.c_str());
  return content;
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
  result["serverInfo"] = {{"name",    "mcp_local"},
                          {"version", "0.1.0"}};
  result["capabilities"] = {{"resources", json::object()},
                            {"tools",     json::object()}};
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
      {"value", {
                  {"kind", "report"}, // Indicates an update
                  {"message", "Processing tool call..."} // Simple heartbeat message
                }}
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
        log_message("Warning: tools/call received without a valid ID. Cannot start heartbeat or send response reliably.");
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
      } catch (...) {
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
  shell_execute_tool.description = "Execute shell command via temporary file and notepad (blocks until notepad closes)"; // Clarified description
  shell_execute_tool.parameters = {
    {"cmd", {{"type", "string"}, {"description", "Shell command or text content to place in temporary file for notepad"}}}
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
  ask_tool.description = "Ask user a question via temporary file and notepad (blocks until notepad closes)"; // Clarified description
  ask_tool.parameters = {
    {"question", {{"type", "string"}, {"description", "Question or prompt to display in notepad"}}}
  };
  ask_tool.required_parameters = {"question"};
  ask_tool.execute = [](const json &arguments) -> json {
    std::string question = arguments["question"].get<std::string>();
    log_message("Executing ask tool with question: " + question);
    std::string response_text = execute_notepad_edit(question); // This call blocks
    log_message("ask tool finished, response length: " + std::to_string(response_text.length()));

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
          std::string method_name = request["method"].is_string() ? request["method"].get<std::string>() : "[invalid method]";
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