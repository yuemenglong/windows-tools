#include <algorithm>
#include <conio.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map> // Needed for potential sorting if not using vectors+sort
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>
#include <iterator> // Required for std::istreambuf_iterator
#include <array>    // Good for fixed-size BOMs

// Define BOM sequences as constants
const std::array<unsigned char, 3> UTF8_BOM = {0xEF, 0xBB, 0xBF};
const std::array<unsigned char, 2> UTF16LE_BOM = {0xFF, 0xFE};
const std::array<unsigned char, 2> UTF16BE_BOM = {0xFE, 0xFF};
const std::array<unsigned char, 4> UTF32LE_BOM = {0xFF, 0xFE, 0x00, 0x00};
const std::array<unsigned char, 4> UTF32BE_BOM = {0x00, 0x00, 0xFE, 0xFF};

namespace fs = std::filesystem;

// --- 全局配置 (unchanged) ---
static const std::vector<std::regex> ignorePatterns = {
  std::regex("venv"),
  std::regex("node_modules"),
  std::regex("__pycache__"),
  std::regex("build"),
  std::regex("dist"),
  std::regex("bin"),
  std::regex("obj"),
  std::regex("target"),
  std::regex("cmake-build-debug"),
  std::regex("cmake-build-release"),
  std::regex("json.hpp"),
  std::regex("\\..+") // 匹配以.开头的任何目录/文件（.和..除外）
};
static const std::vector<std::regex> antiIgnorePatterns = {
  std::regex("\\.cursor"),
};
static const std::vector<std::regex> specialFilePatterns = {
  std::regex("CMakeLists\\.txt", std::regex::icase),
  std::regex("README\\.md", std::regex::icase),
  std::regex("readme\\.txt", std::regex::icase)};
static const std::set<std::string> codeExtensions = {
  ".c", ".cpp", ".h", ".hpp", ".cc", ".cxx", ".hxx", ".java",
  ".py", ".js", ".ts", ".go", ".dart", ".kt", ".kts", ".cs",
  ".gradle", ".properties", ".yml", ".yaml", ".mdc", ".rs"};

// --- 辅助函数 (mostly unchanged) ---
bool isCodeFile(const std::string &extension) {
  std::string lowerExtension = extension;
  std::transform(lowerExtension.begin(), lowerExtension.end(),
                 lowerExtension.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return codeExtensions.count(lowerExtension);
}

// shouldIgnorePath remains the same
bool shouldIgnorePath(const fs::path &path) {
  try {
    if (path.filename() == "." || path.filename() == "..") {
      return true;
    }
    // Check relative path parts against ignore patterns
    // Using generic string conversion which handles separators better
    std::string relativePathStr =
      path.lexically_relative(fs::current_path())
        .generic_string(); // Example, adjust base if needed

    // Check parts of the path against patterns
    fs::path tempPath = path;
    while (!tempPath.empty() && tempPath != tempPath.parent_path()) {
      std::string partStr = tempPath.filename().string();
      if (partStr == "." || partStr == "..") {
        tempPath = tempPath.parent_path();
        continue;
      }

      // Anti-ignore patterns take precedence
      for (const auto &antiPattern: antiIgnorePatterns) {
        if (std::regex_match(partStr, antiPattern)) {
          return false; // Don't ignore if an anti-pattern matches
        }
      }
      // Ignore patterns
      for (const auto &pattern: ignorePatterns) {
        if (std::regex_match(partStr, pattern)) {
          // Now check if any anti-ignore pattern *overrides* this ignore for
          // the *same* part
          bool overridden = false;
          for (const auto &antiPattern: antiIgnorePatterns) {
            if (std::regex_match(partStr, antiPattern)) {
              overridden = true;
              break;
            }
          }
          if (!overridden)
            return true; // Ignore if pattern matches and is not overridden
        }
      }
      tempPath = tempPath.parent_path();
    }

  } catch (const std::exception &e) {
    std::cerr << "警告: 检查路径时出错 '" << path.string() << "': " << e.what()
              << std::endl;
    return true;
  }
  return false;
}

bool isSpecialFile(const std::string &filename) {
  for (const auto &pattern: specialFilePatterns) {
    if (std::regex_match(filename, pattern)) {
      return true;
    }
  }
  return false;
}

// readFileContent remains the same
/**
 * @brief Reads the entire content of a file as binary data, detects and removes
 *        known BOMs (UTF-8, UTF-16 LE/BE, UTF-32 LE/BE).
 * @param filePath Path to the file.
 * @return File content as a string (byte sequence), with BOM removed if found.
 *         Returns an empty string on error.
 * @warning This function removes the BOM but does NOT transcode the content.
 *          If the file was UTF-16 or UTF-32, the returned string will contain
 *          the raw byte sequence of that encoding (minus the BOM). Subsequent
 *          processing (like writing to UTF-8 XML) might require transcoding.
 */
std::string readFileContent(const fs::path &filePath) {
  std::ifstream file(filePath, std::ios::in | std::ios::binary);
  if (!file) {
    std::cerr << "错误: 无法打开文件: " << filePath.string() << std::endl;
    return "";
  }

  // Read the entire file into a string (as raw bytes)
  std::string content((std::istreambuf_iterator<char>(file)),
                      std::istreambuf_iterator<char>());

  if (file.bad()) {
    std::cerr << "错误: 读取文件时出错: " << filePath.string() << std::endl;
    return "";
  }

  // --- Check for and remove BOMs ---
  // Order matters: check longer BOMs before shorter ones if they share prefixes.
  size_t bomSize = 0;
  std::string detectedBomType = "None";

  // Helper lambda to check if content starts with a specific BOM sequence
  auto startsWith = [&](const auto &bom) {
    if (content.size() < bom.size()) {
      return false;
    }
    for (size_t i = 0; i < bom.size(); ++i) {
      // Compare using unsigned char values
      if (static_cast<unsigned char>(content[i]) != bom[i]) {
        return false;
      }
    }
    return true;
  };

  if (startsWith(UTF32LE_BOM)) {
    bomSize = UTF32LE_BOM.size();
    detectedBomType = "UTF-32 LE";
  } else if (startsWith(UTF32BE_BOM)) {
    bomSize = UTF32BE_BOM.size();
    detectedBomType = "UTF-32 BE";
  } else if (startsWith(UTF16LE_BOM)) { // Check after UTF32LE due to shared prefix FF FE
    bomSize = UTF16LE_BOM.size();
    detectedBomType = "UTF-16 LE";
  } else if (startsWith(UTF16BE_BOM)) { // Check after UTF32BE (though no prefix overlap)
    bomSize = UTF16BE_BOM.size();
    detectedBomType = "UTF-16 BE";
  } else if (startsWith(UTF8_BOM)) {
    bomSize = UTF8_BOM.size();
    detectedBomType = "UTF-8";
  }

  if (bomSize > 0) {
    content.erase(0, bomSize); // Remove the detected BOM
    // Optional: Log the removal
    // std::cout << "  (已移除 " << detectedBomType << " BOM: " << filePath.filename().string() << ")" << std::endl;
  }
  // --- End BOM Check ---

  // **Important Caveat:**
  // The content string now holds the file's byte sequence *without* the BOM.
  // If the original encoding was UTF-16 or UTF-32, this string still contains
  // those bytes. Since the XML output must be UTF-8, simply removing the BOM
  // from UTF-16/UTF-32 files might lead to invalid XML if the content contains
  // non-ASCII characters. A full transcoding step would be needed for robust
  // handling of arbitrary UTF-16/UTF-32 input if required.
  // This implementation fulfills the request to *remove the BOM only*.

  return content;
}

// escapeXmlChars remains mostly the same, ensure it handles CDATA end correctly
std::string escapeXmlChars(const std::string &input) {
  std::string result;
  result.reserve(input.size()); // Reserve initial space

  for (size_t i = 0; i < input.size(); ++i) {
    char c = input[i];

    // Filter out invalid XML 1.0 characters (basic C0 controls except TAB, LF,
    // CR)
    if ((c >= 0x00 && c <= 0x08) || c == 0x0B || c == 0x0C ||
        (c >= 0x0E && c <= 0x1F) || c == 0x7F) {
      // Replace with a placeholder like '?' or simply skip
      // result += '?'; // Optional: replace with placeholder
      continue; // Skip invalid character
    }

    // Handle CDATA section end marker `]]>`
    if (c == ']' && i + 2 < input.size() && input[i + 1] == ']' &&
        input[i + 2] == '>') {
      // Replace "]]>" with "]]>" or split the CDATA section
      // Simplest safe replacement inside CDATA is often ]]>]]><![CDATA[
      // But just escaping the > within the sequence is common: ]]>
      // Another method: split the text: ]] ]]> <![CDATA[ >
      result += "]]>"; // Escape the '>'
      i += 2;          // Skip the next two characters ']' and '>'
    } else {
      // For other characters, append directly.
      // Inside CDATA, '<', '&' don't strictly need escaping, but be careful if
      // mixing contexts. Let's keep it simple for CDATA: only handle ']]>'
      result += c;
    }
  }
  return result;
}

// NEW: Helper to escape characters for XML attribute values
std::string escapeXmlAttribute(const std::string &input) {
  std::string result;
  result.reserve(input.size());
  for (char c: input) {
    switch (c) {
      case '&':
        result += "&";
        break;
      case '<':
        result += "<";
        break;
      case '>':
        result += ">";
        break; // Must escape > in attributes
      case '\"':
        result += "\"";
        break;
      case '\'':
        result += "'";
        break;
        // Control characters (like newline, tab) are generally invalid in
        // attributes or should be handled carefully depending on XML parser
        // expectations. Let's assume filenames won't contain raw newlines/tabs that
        // need preserving in attributes. If they might, they should probably be
        // encoded (e.g., 	 for tab). For simplicity, we'll copy other characters
        // directly.
      default:
        // Basic check for other invalid XML chars if needed, similar to
        // escapeXmlChars
        if ((c >= 0x00 && c <= 0x1F) && c != '\t' && c != '\n' && c != '\r') {
          // Skip or replace invalid control characters for attributes
          continue;
        } else {
          result += c;
        }
        break;
    }
  }
  return result;
}

// NEW: Helper for indentation
std::string indent(int level) {
  return std::string(level * 2, ' '); // 2 spaces per level
}

// --- NEW Recursive Directory Processing Function ---

/**
 * @brief Recursively processes a directory and writes XML structure.
 * @param currentDir The directory to process.
 * @param xmlFile The output XML file stream.
 * @param indentLevel Current indentation level for pretty printing.
 * @param rootPath The absolute root path of the scan (for logging/context).
 * @param mergedFiles Counter for merged files (passed by reference).
 * @param skippedFiles Counter for skipped files (passed by reference).
 * @param skippedDirs Counter for skipped directories (passed by reference).
 */
void processDirectoryRecursive(const fs::path &currentDir,
                               std::ofstream &xmlFile, int indentLevel,
                               const fs::path &rootPath, int &mergedFiles,
                               int &skippedFilesNonCode,
                               int &skippedFilesIgnored, int &skippedDirs) {
  std::vector<fs::directory_entry> subdirs;
  std::vector<fs::directory_entry> files;
  std::error_code ec;

  // Iterate over direct children
  try {
    for (const auto &entry: fs::directory_iterator(
      currentDir, fs::directory_options::skip_permission_denied, ec)) {
      if (ec) {
        std::cerr << "警告: 访问目录条目时出错 '" << currentDir.string()
                  << "': " << ec.message() << ", 跳过此条目。" << std::endl;
        skippedFilesIgnored++; // Count error as ignored
        ec.clear();            // Clear error to try next entry
        continue;
      }

      const fs::path &entryPath = entry.path();

      // Check if path should be ignored (using the full path)
      if (shouldIgnorePath(entryPath)) {
        std::error_code type_ec;
        if (entry.is_directory(type_ec)) {
          skippedDirs++;
          std::cout << indent(indentLevel)
                    << "跳过忽略目录: " << entryPath.filename().string()
                    << std::endl;
        } else if (!type_ec) { // Only count as skipped file if not a directory
          skippedFilesIgnored++;
          std::cout << indent(indentLevel)
                    << "跳过忽略文件/条目: " << entryPath.filename().string()
                    << std::endl;
        } else {
          // Error determining type of ignored path
          skippedFilesIgnored++;
          std::cerr << "警告: 检查忽略路径类型时出错 '" << entryPath.string()
                    << "': " << type_ec.message() << std::endl;
        }
        continue; // Skip this ignored entry
      }

      // Classify entry (directory or file)
      std::error_code type_ec;
      if (entry.is_directory(type_ec)) {
        subdirs.push_back(entry);
      } else if (entry.is_regular_file(type_ec)) {
        files.push_back(entry);
      } else if (type_ec) {
        std::cerr << "警告: 检查条目类型时出错 '" << entryPath.string()
                  << "': " << type_ec.message() << ", 跳过。" << std::endl;
        skippedFilesIgnored++;
      } else {
        // Neither a directory nor a regular file (symlink, etc.)
        std::cout << indent(indentLevel)
                  << "跳过非目录/常规文件: " << entryPath.filename().string()
                  << std::endl;
        skippedFilesIgnored++;
      }
      if (type_ec) { // Log error if is_directory or is_regular_file failed
        std::cerr << "警告: 检查条目类型时出错 '" << entryPath.string()
                  << "': " << type_ec.message() << ", 跳过。" << std::endl;
        skippedFilesIgnored++;
      }
    }
    if (ec) { // Error during iteration itself (e.g., read error after starting)
      std::cerr << "警告: 迭代目录时出错 '" << currentDir.string()
                << "': " << ec.message() << std::endl;
      // Decide whether to continue or stop; here we just report and continue
      // processing collected items
    }
  } catch (const fs::filesystem_error &e) {
    std::cerr << "错误: 迭代目录时发生文件系统异常 '" << currentDir.string()
              << "': " << e.what() << std::endl;
    return; // Stop processing this directory on severe error
  } catch (const std::exception &e) {
    std::cerr << "错误: 迭代目录时发生异常 '" << currentDir.string()
              << "': " << e.what() << std::endl;
    return; // Stop processing this directory on severe error
  }

  // Sort directories and files alphabetically by filename
  std::sort(subdirs.begin(), subdirs.end(),
            [](const fs::directory_entry &a, const fs::directory_entry &b) {
              return a.path().filename() < b.path().filename();
            });
  std::sort(files.begin(), files.end(),
            [](const fs::directory_entry &a, const fs::directory_entry &b) {
              return a.path().filename() < b.path().filename();
            });

  // Process Directories first
  for (const auto &dirEntry: subdirs) {
    std::string dirName = dirEntry.path().filename().string();
    std::cout << indent(indentLevel) << "处理目录: " << dirName << std::endl;
    xmlFile << indent(indentLevel) << "<dir name=\""
            << escapeXmlAttribute(dirName) << "\">\n";
    processDirectoryRecursive(dirEntry.path(), xmlFile, indentLevel + 1,
                              rootPath, mergedFiles, skippedFilesNonCode,
                              skippedFilesIgnored, skippedDirs);
    xmlFile << indent(indentLevel) << "</dir>\n";
  }

  // Process Files next
  for (const auto &fileEntry: files) {
    fs::path filePath = fileEntry.path();
    std::string filename = filePath.filename().string();
    std::string extension =
      filePath.has_extension() ? filePath.extension().string() : "";

    if (isCodeFile(extension) || isSpecialFile(filename)) {
      std::cout << indent(indentLevel) << "处理文件: " << filename << std::endl;
      std::string content = readFileContent(filePath);
      if (content.empty() && !fs::is_empty(filePath, ec)) {
        // readFileContent already prints errors, but we count it as skipped
        // here
        std::cerr << indent(indentLevel + 1) << "警告: 文件 '" << filename
                  << "' 读取内容为空或失败，跳过XML写入。" << std::endl;
        skippedFilesIgnored++;
        continue; // Skip writing this file
      }

      xmlFile << indent(indentLevel) << "<file name=\""
              << escapeXmlAttribute(filename) << "\">\n";
      xmlFile << indent(indentLevel + 1)
              << "<![CDATA["; // Start CDATA on new indented line

      // Write content, escaping CDATA-problematic sequences
      // Add a newline before content if content is not empty, for readability
      if (!content.empty()) {
        xmlFile << "\n" << indent(indentLevel + 2); // Indent content start
        // Need to handle indentation within the CDATA content if desired,
        // but usually raw content is preferred. escapeXmlChars handles safety.
        // We need to write potentially large content safely.
        std::string escapedContent = escapeXmlChars(content);
        // Replace newlines in content with newline + appropriate indentation
        // for XML readability
        std::string line;
        std::stringstream ss(escapedContent);
        bool firstLine = true;
        while (std::getline(ss, line, '\n')) {
          if (!firstLine) {
            xmlFile << "\n" << indent(indentLevel + 2);
          }
          xmlFile << line;
          firstLine = false;
        }
        xmlFile << "\n"
                << indent(indentLevel + 1); // Indent before closing CDATA
      }

      xmlFile << "]]>\n";
      xmlFile << indent(indentLevel) << "</file>\n";
      mergedFiles++;
    } else {
      std::cout << indent(indentLevel)
                << "跳过文件(非代码/特殊文件): " << filename << std::endl;
      skippedFilesNonCode++;
    }
  }
}

// --- 处理逻辑函数 (mergeByDir modified) ---

// 新的函数签名，接收一个目录列表和输出文件路径
int mergeByDir(const std::vector<fs::path> &rootPaths, const fs::path &outputFile) {
  std::cout << "模式: 按目录扫描\n";

  // 直接使用传入的输出文件路径
  std::ofstream xmlFile(outputFile, std::ios::out | std::ios::binary);
  if (!xmlFile) {
    std::cerr << "错误: 无法创建输出文件: " << outputFile.string() << std::endl;
    return 1;
  }
  std::cout << "输出文件: " << outputFile.string() << std::endl;

  // 初始化统计变量
  int mergedFiles = 0;
  int skippedFilesNonCode = 0;
  int skippedFilesIgnored = 0;
  int skippedDirs = 0;

  // 写入XML头部和新的根节点 <projects>
  xmlFile << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
  xmlFile << "<projects>\n";

  // 遍历所有传入的根目录
  for (const auto &rootPath: rootPaths) {
    std::cout << "\n--- 开始处理根目录: " << rootPath.string() << " ---\n";

    // 验证每个根目录的有效性
    std::error_code ec_check;
    if (!fs::exists(rootPath, ec_check) || !fs::is_directory(rootPath, ec_check)) {
      std::cerr << "警告: 路径 '" << rootPath.string() << "' 不存在或不是一个目录，已跳过。\n";
      continue; // 跳过无效的目录
    }

    // 为每个根目录创建一个 <project> 节点
    std::string rootPathStr = rootPath.string();
    xmlFile << indent(1) << "<project path=\"" << escapeXmlAttribute(rootPathStr) << "\">\n";

    // 调用递归函数，注意缩进级别从2开始
    processDirectoryRecursive(
      rootPath, xmlFile, 2, // 缩进从 level 2 开始
      rootPath, mergedFiles, skippedFilesNonCode, skippedFilesIgnored, skippedDirs);

    xmlFile << indent(1) << "</project>\n";
    std::cout << "--- 完成处理根目录: " << rootPath.string() << " ---\n";
  }

  // 写入关闭的根标签
  xmlFile << "</projects>\n";
  xmlFile.close();

  if (!xmlFile) {
    std::cerr << "错误: 写入或关闭输出文件时出错: " << outputFile.string()
              << std::endl;
    return 1;
  }

  // 输出统计信息
  std::cout << "\n==== 目录扫描处理完成 ====\n";
  std::cout << "合并的文件数: " << mergedFiles << std::endl;
  std::cout << "跳过的文件数 (非代码/特殊): " << skippedFilesNonCode << std::endl;
  std::cout << "跳过的忽略目录数: " << skippedDirs << std::endl;
  std::cout << "跳过的忽略/错误/非文件条目数: " << skippedFilesIgnored << std::endl;
  std::cout << "输出文件: " << outputFile.string() << std::endl;

  return 0;
}

// --- mergeByRef function remains unchanged ---
// It generates a flat XML structure based on a list file, which is different
// from the directory scan goal. If mergeByRef also needs the hierarchical
// output based on the *paths* listed, it would require a significant redesign
// (e.g., building an in-memory tree from the paths first). Keeping it as is for
// now.
int mergeByRef(const fs::path &refFilePath) {
  // ... (original mergeByRef code remains here) ...
  std::cout << "模式: 按引用文件\n";
  std::cout << "引用文件: " << refFilePath.string() << std::endl;

  std::error_code ec_check;
  if (!fs::exists(refFilePath, ec_check) ||
      !fs::is_regular_file(refFilePath, ec_check)) {
    if (ec_check) {
      std::cerr << "错误: 检查引用文件 '" << refFilePath.string()
                << "' 时出错: " << ec_check.message() << std::endl;
    } else {
      std::cerr << "错误: 引用文件 " << refFilePath.string()
                << " 不存在或不是一个文件" << std::endl;
    }
    return 1;
  }

  fs::path outputFile = refFilePath.parent_path() /
                        (refFilePath.filename().stem().string() + "_merge.xml");
  std::ofstream xmlFile(outputFile, std::ios::out | std::ios::binary);

  if (!xmlFile) {
    std::cerr << "错误: 无法创建输出文件: " << outputFile.string() << std::endl;
    return 1;
  }
  std::cout << "输出文件: " << outputFile.string() << std::endl;

  int totalLines = 0;
  int mergedFiles = 0;
  int skippedFilesNotFound = 0;
  int skippedFilesNotFile = 0;
  int skippedFilesReadError = 0;
  int skippedEmptyOrComment = 0;

  xmlFile << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
  std::string escapedRefPathStr =
    escapeXmlAttribute(refFilePath.string()); // Use new escaper
  xmlFile << "<files source_type=\"reference_file\" ref_file=\""
          << escapedRefPathStr
          << "\">\n"; // Root element remains <files> for ref mode

  std::ifstream refFile(refFilePath);
  if (!refFile) {
    std::cerr << "错误: 无法打开引用文件: " << refFilePath.string()
              << std::endl;
    xmlFile.close();
    return 1;
  }

  std::string line;
  // Need the writeXmlFileEntry logic or replicate it here
  auto writeFlatXmlFileEntry = [&](std::ofstream &xmlOut,
                                   const fs::path &filePath,
                                   const std::string &pathAttrValue) {
    std::string content = readFileContent(filePath);
    if (content.empty() && !fs::is_empty(filePath)) {
      std::cerr << "警告: 文件 '" << filePath.string()
                << "' 读取内容为空或失败，跳过写入XML。" << std::endl;
      return false;
    }
    std::string escapedPathAttr = escapeXmlAttribute(pathAttrValue);
    xmlOut << "  <file path=\"" << escapedPathAttr
           << "\">\n";         // Indent level 1 for flat list
    xmlOut << "    <![CDATA["; // Indent level 2
    std::string escapedContent = escapeXmlChars(content);
    // Simple content writing for flat structure
    xmlOut << escapedContent;
    xmlOut << "]]>\n";
    xmlOut << "  </file>\n";
    return true;
  };

  while (std::getline(refFile, line)) {
    totalLines++;
    if (!line.empty() && line.back() == '\r')
      line.pop_back();
    size_t first = line.find_first_not_of(" \t\n\r\f\v");
    if (std::string::npos == first) {
      skippedEmptyOrComment++;
      continue;
    }
    size_t last = line.find_last_not_of(" \t\n\r\f\v");
    line = line.substr(first, (last - first + 1));
    if (line.size() >= 3 && (unsigned char) line[0] == 0xEF &&
        (unsigned char) line[1] == 0xBB && (unsigned char) line[2] == 0xBF) {
      line.erase(0, 3);
    }
    if (line.empty() || line[0] == '#') {
      skippedEmptyOrComment++;
      continue;
    }

    fs::path targetFilePath = line;
    std::error_code ec;
    bool exists = fs::exists(targetFilePath, ec);
    if (ec || !exists) {
      skippedFilesNotFound++;
      if (!ec)
        std::cerr << "警告: 文件不存在 '" << line << "', 跳过。\n";
      else
        std::cerr << "警告: 检查路径 '" << line << "' 时出错: " << ec.message()
                  << ", 跳过。\n";
      continue;
    }
    bool isFile = fs::is_regular_file(targetFilePath, ec);
    if (ec || !isFile) {
      skippedFilesNotFile++;
      if (!ec)
        std::cerr << "警告: 路径不是一个常规文件 '" << line << "', 跳过。\n";
      else
        std::cerr << "警告: 检查文件类型 '" << line
                  << "' 时出错: " << ec.message() << ", 跳过。\n";
      continue;
    }

    std::cout << "处理文件: " << targetFilePath.string() << " (来自引用文件)\n";
    if (writeFlatXmlFileEntry(xmlFile, targetFilePath,
                              line)) { // Use the lambda/helper
      mergedFiles++;
    } else {
      skippedFilesReadError++;
    }
  }

  refFile.close();
  xmlFile << "</files>\n";
  xmlFile.close();

  if (!xmlFile) {
    std::cerr << "错误: 写入或关闭输出文件时出错: " << outputFile.string()
              << std::endl;
    return 1;
  }

  // Statistics output (remains the same)
  std::cout << "\n==== 引用文件处理完成 ====\n";
  std::cout << "引用文件总行数: " << totalLines << std::endl;
  std::cout << "跳过的空行/注释行: " << skippedEmptyOrComment << std::endl;
  int attemptedFiles = totalLines - skippedEmptyOrComment;
  std::cout << "尝试处理的文件路径数: " << attemptedFiles << std::endl;
  std::cout << "成功合并的文件数: " << mergedFiles << std::endl;
  std::cout << "跳过的文件数 (未找到): " << skippedFilesNotFound << std::endl;
  std::cout << "跳过的文件数 (非文件): " << skippedFilesNotFile << std::endl;
  std::cout << "跳过的文件数 (读取/写入错误): " << skippedFilesReadError
            << std::endl;
  std::cout << "输出文件: " << outputFile.string() << std::endl;

  return 0;
}

// --- 主函数 (modified for multi-directory support) ---
int main(int argc, char *argv[]) {
  fs::path inputPath;
  int result = 1;
  std::error_code ec;

  if (argc == 1) {
    std::string dirInput;
    std::cout << "请输入要扫描的目录路径: ";
    std::getline(std::cin, dirInput);
    if (dirInput.empty()) {
      std::cerr << "未输入任何路径，程序退出。" << std::endl;
      return 1;
    }
    try {
      inputPath = fs::absolute(dirInput).lexically_normal();
      // 兼容模式：单目录扫描，使用旧的输出文件命名规则
      std::vector<fs::path> inputDirs = {inputPath};
      fs::path outputFile = inputPath.parent_path() / (inputPath.filename().string() + "_merge.xml");
      result = mergeByDir(inputDirs, outputFile);
    } catch (const std::exception &e) {
      std::cerr << "错误: 处理输入路径时出错: " << e.what() << std::endl;
      return 1;
    }
  } else if (argc >= 2) { // 修改为处理所有 argc >= 2 的情况
    // 首先判断是否是按引用文件模式（因为它的格式是固定的：一个文件输入）
    fs::path firstArgPath = argv[1];
    std::error_code ec;
    bool is_file = fs::is_regular_file(fs::absolute(firstArgPath), ec);

    // 如果只有一个参数，且它是一个文件，则认为是引用文件模式
    if (argc == 2 && !ec && is_file) {
      result = mergeByRef(fs::absolute(firstArgPath).lexically_normal());
    }
      // 否则，全部当作目录扫描模式处理
    else {
      std::vector<fs::path> inputDirs;
      fs::path outputFile;

      if (argc == 2) { // 兼容旧的单目录模式
        fs::path rootPath = fs::absolute(argv[1]).lexically_normal();
        if (!fs::is_directory(rootPath, ec)) {
          std::cerr << "错误: 输入路径 '" << rootPath.string() << "' 不是一个有效的目录。" << std::endl;
          return 1;
        }
        inputDirs.push_back(rootPath);
        // 沿用旧的输出文件命名规则
        outputFile = rootPath.parent_path() / (rootPath.filename().string() + "_merge.xml");

      } else { // argc >= 3, 新的多目录模式
        // 最后一个参数是输出文件
        outputFile = fs::absolute(argv[argc - 1]).lexically_normal();

        // 检查输出文件的父目录是否存在
        fs::path outputParentPath = outputFile.parent_path();
        if (!outputParentPath.empty() && !fs::exists(outputParentPath)) {
          std::cerr << "错误: 输出目录 '" << outputParentPath.string() << "' 不存在。" << std::endl;
          return 1;
        }

        // 其余参数是输入目录
        for (int i = 1; i < argc - 1; ++i) {
          fs::path dirPath = fs::absolute(argv[i]).lexically_normal();
          if (!fs::is_directory(dirPath, ec)) {
            std::cerr << "错误: 输入路径 '" << dirPath.string() << "' 不是一个有效的目录。所有输入都必须是目录。" << std::endl;
            return 1;
          }
          inputDirs.push_back(dirPath);
        }
      }

      // 统一调用新的 mergeByDir 函数
      result = mergeByDir(inputDirs, outputFile);
    }

  } else { // argc < 1 的情况，实际上是 argc == 0，不太可能发生，但保持完整
    std::cerr << "用法 1 (单目录): " << argv[0] << " <目录路径>" << std::endl;
    std::cerr << "  -> 扫描单个目录，并在其父目录下生成'目录名_merge.xml'。\n" << std::endl;
    std::cerr << "用法 2 (多目录): " << argv[0] << " <目录1> <目录2> ... <输出文件.xml>" << std::endl;
    std::cerr << "  -> 扫描多个目录，并将所有结果合并到指定的输出文件中。\n" << std::endl;
    std::cerr << "用法 3 (引用文件): " << argv[0] << " <引用文件路径>" << std::endl;
    std::cerr << "  -> 读取引用文件中列出的文件路径进行合并。\n" << std::endl;
    return 1;
  }

  std::cout << "\n处理结束 (" << (result == 0 ? "成功" : "失败")
            << ")，按任意键退出..." << std::endl;
  while (_kbhit())
    _getch(); // Clear buffer
  _getch();   // Wait for key press
  return result;
}