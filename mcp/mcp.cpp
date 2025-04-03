#define NOMINMAX
#include <iostream>
#include <string>
#include <fstream>
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

using json = nlohmann::json;

// --- execute_notepad_edit function remains unchanged ---
std::string execute_notepad_edit(const std::string& cmd = "")
{
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
    if (!ShellExecuteExW(&sei) || !sei.hProcess)
    {
        DeleteFileW(temp_file_path.c_str());
        return "Error: Unable to start Notepad";
    }
    WaitForSingleObject(sei.hProcess, INFINITE);
    CloseHandle(sei.hProcess);
    std::ifstream input_file(temp_file_path);
    if (!input_file.is_open())
    {
        DeleteFileW(temp_file_path.c_str());
        return "Error: Unable to read temporary file";
    }
    std::string content((std::istreambuf_iterator<char>(input_file)), std::istreambuf_iterator<char>());
    input_file.close();
    DeleteFileW(temp_file_path.c_str());
    return content;
}

// --- get_timestamp function remains unchanged ---
std::string get_timestamp()
{
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::tm local_tm;
    localtime_s(&local_tm, &time);
    std::stringstream ss;
    ss << std::put_time(&local_tm, "%Y-%m-%d %H:%M:%S");
    return ss.str();
}

// --- log_message function remains unchanged ---
void log_message(const std::string& message)
{
    std::ofstream log_file("mcp_log.txt", std::ios::app);
    if (log_file.is_open()) { log_file << "[" << get_timestamp() << "] " << message << std::endl; }
    else { std::cerr << "Error opening log file!" << std::endl; }
}

// Tool structure to represent a tool with metadata and execution logic
struct Tool
{
    std::string name;
    std::string description;
    json parameters; // Parameters schema
    std::vector<std::string> required_parameters;
    std::function<json(const json&)> execute; // Function to execute the tool

    // Generate input schema from tool definition
    json generateInputSchema() const
    {
        json schema;
        schema["type"] = "object";
        schema["properties"] = parameters;
        schema["required"] = json::array();
        for (const auto& param : required_parameters)
        {
            schema["required"].push_back(param);
        }
        return schema;
    }

    // Generate tool description for tools/list
    json generateToolDescription() const
    {
        json tool;
        tool["name"] = name;
        tool["description"] = description;
        tool["inputSchema"] = generateInputSchema();
        return tool;
    }
};

// ToolRegistry to manage all available tools
class ToolRegistry
{
private:
    std::map<std::string, Tool> tools;

public:
    // Register a new tool
    void registerTool(const Tool& tool)
    {
        tools[tool.name] = tool;
    }

    // Check if a tool exists
    bool hasTool(const std::string& name) const
    {
        return tools.find(name) != tools.end();
    }

    // Get a tool by name
    const Tool& getTool(const std::string& name) const
    {
        if (!hasTool(name))
        {
            throw std::runtime_error("Unknown tool name: " + name);
        }
        return tools.at(name);
    }

    // Get all tools
    std::vector<Tool> getAllTools() const
    {
        std::vector<Tool> result;
        for (const auto& pair : tools)
        {
            result.push_back(pair.second);
        }
        return result;
    }

    // Execute a tool by name
    json executeTool(const std::string& name, const json& arguments) const
    {
        const Tool& tool = getTool(name);

        try
        {
            // Validate required parameters
            for (const auto& required : tool.required_parameters)
            {
                if (!arguments.contains(required))
                {
                    throw std::runtime_error("Missing required parameter '" + required + "' for " + name + " tool");
                }

                if (!arguments[required].is_string())
                {
                    throw std::runtime_error("Parameter '" + required + "' for " + name + " tool must be a string");
                }
            }

            // Execute the tool and catch any exceptions it might throw
            return tool.execute(arguments);
        }
        catch (const std::exception& e)
        {
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
json handle_initialize_request()
{
    json result;
    result["serverInfo"] = {{"name", "mcp_local"}, {"version", "0.1.0"}};
    result["capabilities"] = {{"resources", json::object()}, {"tools", json::object()}};
    result["protocolVersion"] = "2024-11-05";
    return result;
}

// Handle tools/list request - now uses the tool registry
json handle_tools_list_request()
{
    json result;
    result["tools"] = json::array();

    // Get all tools from registry and add their descriptions
    auto tools = g_toolRegistry.getAllTools();
    for (const auto& tool : tools)
    {
        result["tools"].push_back(tool.generateToolDescription());
    }

    return result;
}

// Handle tools/call request - now uses the tool registry
json handle_tools_call_request(const json& request)
{
    // Validate full request structure first
    if (!request.contains("params"))
    {
        throw std::runtime_error("Missing 'params' field in tools/call request");
    }

    const auto& params = request["params"];
    if (!params.is_object())
    {
        throw std::runtime_error("The 'params' field must be an object in tools/call request");
    }

    // Validate name parameter
    if (!params.contains("name"))
    {
        throw std::runtime_error("Missing 'name' field in tools/call params");
    }

    if (!params["name"].is_string())
    {
        throw std::runtime_error("The 'name' field must be a string in tools/call params");
    }

    std::string tool_name = params["name"].get<std::string>();

    // Check if the tool exists
    if (!g_toolRegistry.hasTool(tool_name))
    {
        throw std::runtime_error("Unknown tool name: '" + tool_name + "'");
    }

    // Validate arguments parameter
    if (!params.contains("arguments"))
    {
        throw std::runtime_error("Missing 'arguments' field in tools/call params");
    }

    if (!params["arguments"].is_object())
    {
        throw std::runtime_error("The 'arguments' field must be an object in tools/call params");
    }

    const json& arguments = params["arguments"];

    // Execute tool from registry (which will perform its own validation of required parameters)
    return g_toolRegistry.executeTool(tool_name, arguments);
}

// --- process_request function - simplified since tools are now managed by registry
json process_request(const json& request)
{
    json response;
    response["jsonrpc"] = "2.0";
    response["id"] = request.contains("id") ? request["id"] : nullptr; // Ensure we always have an id

    try
    {
        // Check for required JSON-RPC fields
        if (!request.contains("jsonrpc") || request["jsonrpc"] != "2.0")
        {
            throw std::runtime_error("Invalid or missing jsonrpc version. Must be 2.0");
        }

        if (!request.contains("method") || !request["method"].is_string())
        {
            throw std::runtime_error("Missing or invalid method parameter");
        }

        std::string method = request["method"].get<std::string>();

        if (method == "initialize")
        {
            response["result"] = handle_initialize_request();
        }
        else if (method == "tools/list")
        {
            response["result"] = handle_tools_list_request();
        }
        else if (method == "tools/call")
        {
            response["result"] = handle_tools_call_request(request);
        }
        else
        {
            // Method not found error - JSON-RPC error code -32601
            log_message("Method not found: " + method);
            response["error"] = {
                {"code", -32601},
                {"message", "Method not found: " + method}
            };
            response.erase("result");
            return response;
        }
    }
    catch (const json::exception& e)
    {
        // JSON parse error or invalid parameters - JSON-RPC error code -32602
        log_message(std::string("JSON processing error for request: ") + e.what());
        response["error"] = {
            {"code", -32602},
            {"message", "Invalid parameters or JSON structure error"},
            {"data", e.what()}
        };
        response.erase("result");
    }
    catch (const std::runtime_error& e)
    {
        // Application specific error - JSON-RPC error code -32000 to -32099
        log_message("Runtime error: " + std::string(e.what()));
        response["error"] = {
            {"code", -32000},
            {"message", e.what()}
        };
        response.erase("result");
    }
    catch (const std::exception& e)
    {
        // Internal error - JSON-RPC error code -32603
        log_message("Internal error: " + std::string(e.what()));
        response["error"] = {
            {"code", -32603},
            {"message", "Internal server error"},
            {"data", e.what()}
        };
        response.erase("result");
    }
    catch (...)
    {
        // Unknown error - JSON-RPC error code -32603
        log_message("Unknown exception caught in process_request");
        response["error"] = {
            {"code", -32603},
            {"message", "Unknown internal server error"}
        };
        response.erase("result");
    }

    log_message("Response: " + response.dump());
    return response;
}

// --- handle_signal function remains unchanged ---
void handle_signal(int signal)
{
    if (signal == SIGINT)
    {
        log_message("Received SIGINT (Ctrl+C), exiting gracefully.");
        exit(0);
    }
}

// Initialize tools - register all tools in the registry
void initialize_tools()
{
    // Shell execute tool
    Tool shell_execute_tool;
    shell_execute_tool.name = "shell_execute";
    shell_execute_tool.description = "Execute shell command";
    shell_execute_tool.parameters = {
        {"cmd", {{"type", "string"}, {"description", "Shell command to execute"}}}
    };
    shell_execute_tool.required_parameters = {"cmd"};
    shell_execute_tool.execute = [](const json& arguments) -> json
    {
        std::string cmd = arguments["cmd"].get<std::string>();
        // Create the content array with proper object structure
        json contentItem = {
            {"type", "text"},
            {"text", execute_notepad_edit(cmd)}
        };
        
        return {{"content", json::array({contentItem})}};
    };

    // Ask tool
    Tool ask_tool;
    ask_tool.name = "ask";
    ask_tool.description = "Ask user for any information or questions";
    ask_tool.parameters = {
        {"question", {{"type", "string"}, {"description", "Information or question to ask"}}}
    };
    ask_tool.required_parameters = {"question"};
    ask_tool.execute = [](const json& arguments) -> json {
        std::string question = arguments["question"].get<std::string>();
        std::string response = execute_notepad_edit(question);
        
        // Create the content array with proper object structure
        // Each item in the content array should be an object, not a string
        json contentItem = {
            {"type", "text"},
            {"text", response}
        };
        
        return {{"content", json::array({contentItem})}};
    };

    // Register tools
    g_toolRegistry.registerTool(shell_execute_tool);
    g_toolRegistry.registerTool(ask_tool);
}

// --- main function - now includes tool initialization ---
int main()
{
    signal(SIGINT, handle_signal);
    log_message("MCP service started, initializing tools...");

    // Initialize and register all tools
    try
    {
        initialize_tools();
        log_message("Tools initialized successfully");
    }
    catch (const std::exception& e)
    {
        log_message("Error initializing tools: " + std::string(e.what()));
        return 1;
    }

    log_message("Waiting for JSON-RPC input via stdin...");
    std::string input_line;
    while (true)
    {
        if (!std::getline(std::cin, input_line))
        {
            if (std::cin.eof())
            {
                log_message("Standard input closed (EOF), program exiting.");
                break;
            }
            if (std::cin.fail())
            {
                log_message("Standard input stream error. Resetting stream state.");
                std::cin.clear();
                std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
                continue;
            }
            log_message("Input stream encountered an unknown issue, exiting.");
            break;
        }
        if (input_line.empty()) { continue; }

        json response;
        try
        {
            json request = json::parse(input_line);
            log_message("Request received: " + request.dump());

            // All notification messages should have an "id" field that's not null
            if (!request.contains("id") || request["id"].is_null())
            {
                log_message("Received notification (no/null id), ignoring: " + request.dump());
                continue;
            }

            response = process_request(request);
        }
        catch (const json::parse_error& e)
        {
            log_message("JSON parsing error: " + std::string(e.what()) + " on input: " + input_line);

            // Handle parse error according to JSON-RPC spec
            response = {
                {"jsonrpc", "2.0"},
                {"id", nullptr}, // We can't know the id if parsing failed
                {
                    "error", {
                        {"code", -32700},
                        {"message", "Parse error"},
                        {"data", e.what()}
                    }
                }
            };
        }
        catch (const std::exception& e)
        {
            log_message("Unexpected error in main loop: " + std::string(e.what()));

            // Handle generic error
            response = {
                {"jsonrpc", "2.0"},
                {"id", nullptr}, // Can't determine id reliably here
                {
                    "error", {
                        {"code", -32603},
                        {"message", "Internal server error in main loop"},
                        {"data", e.what()}
                    }
                }
            };
        }
        catch (...)
        {
            log_message("Caught unknown exception in main loop!");

            // Handle unknown error
            response = {
                {"jsonrpc", "2.0"},
                {"id", nullptr},
                {
                    "error", {
                        {"code", -32603},
                        {"message", "An unknown internal server error occurred."}
                    }
                }
            };
        }

        // Send the response
        std::cout << response.dump() << std::endl;
        log_message("Response sent: " + response.dump());
    }

    log_message("MCP service shutting down.");
    return 0;
}
