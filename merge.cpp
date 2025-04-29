#include <algorithm> // 鐢ㄤ簬 std::transform
#include <conio.h>   // 鐢ㄤ簬getch()鍑芥暟
#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex> // 新增: 用于正则表达式
#include <set>
#include <sstream> // 鐢ㄤ簬瀛楃涓叉祦澶勭悊锛堝鏋滈渶瑕佹洿澶嶆潅鐨勮瑙ｆ瀽锛?#10;#include <stdexcept> // 鐢ㄤ簬 std::exception
#include <string>
#include <system_error> // 鐢ㄤ簬 std::error_code
#include <vector>

namespace fs = std::filesystem;

// --- 全局配置 ---

// 忽略的目录和文件模式
// 使用正则表达式匹配，更灵活
static const std::vector<std::regex> ignorePatterns = {
  std::regex("venv"),
  std::regex("\\.venv"),
  std::regex("node_modules"),
  std::regex("\\.git"),
  std::regex("__pycache__"),
  std::regex("build"),
  std::regex("dist"),
  std::regex("bin"),
  std::regex("obj"),
  std::regex("target"),
  std::regex("\\.idea"),
  std::regex("\\.vs"),
  std::regex("\\.vscode"),
  std::regex("cmake-build-debug"),
  std::regex("cmake-build-release"),
  std::regex("\\..+") // 新增: 匹配以.开头的任何目录/文件（.和..除外）
};

// 特殊文件名模式 - 使用正则表达式匹配
static const std::vector<std::regex> specialFilePatterns = {
  std::regex("CMakeLists\\.txt", std::regex::icase), // 不区分大小写
  std::regex("README\\.md", std::regex::icase),      // 不区分大小写
  std::regex("readme\\.txt", std::regex::icase)      // 不区分大小写
};

// --- 辅助函数 (基本不变) ---

bool isCodeFile(const std::string &extension) {
  static const std::set<std::string> codeExtensions = {
    ".c", ".cpp", ".h", ".hpp", ".cc", ".cxx",
    ".hxx", ".java", ".py", ".js", ".ts", ".go",
    ".dart", ".kt", ".kts", ".cs", ".gradle", ".properties",
    ".yml", ".yaml"};
  // 将扩展名转为小写进行比较，更健壮
  std::string lowerExtension = extension;
  std::transform(lowerExtension.begin(), lowerExtension.end(),
                 lowerExtension.begin(), [](unsigned char c) {
      return std::tolower(c);
    });                           // 使用 lambda 兼容性更好
  return codeExtensions.count(lowerExtension); // 使用 count 比 find 更简洁
}

// 这个函数主要用于目录扫描时的过滤
bool shouldIgnorePath(const fs::path &path) {
  try {
    // 忽略特殊目录 . 和 .. (虽然递归迭代器通常会跳过这些)
    if (path.filename() == "." || path.filename() == "..") {
      return true;
    }

    // 检查路径的每个部分是否匹配任何忽略模式
    for (const auto &part: path) {
      std::string partStr = part.string();
      for (const auto &pattern: ignorePatterns) {
        if (std::regex_match(partStr, pattern)) {
          return true;
        }
      }
    }
  } catch (const std::exception &e) {
    // 路径迭代可能因特殊字符等失败
    std::cerr << "警告: 检查路径时出错 '" << path.string() << "': " << e.what()
              << std::endl;
    return true; // 出错时倾向于忽略
  }
  return false;
}

// 检查文件是否匹配特殊文件模式
bool isSpecialFile(const std::string &filename) {
  for (const auto &pattern: specialFilePatterns) {
    if (std::regex_match(filename, pattern)) {
      return true;
    }
  }
  return false;
}

std::string readFileContent(const fs::path &filePath) {
  // 显式使用 std::ios::binary 来读取原始字节，避免文本模式下的行尾转换问题
  // 然后在 escapeXmlChars 中处理换行符 (\n, \r)
  std::ifstream file(filePath, std::ios::in | std::ios::binary);
  if (!file) {
    std::cerr << "错误: 无法打开文件: " << filePath.string() << std::endl;
    return "";
  }

  // 更高效地读取整个文件
  std::string content((std::istreambuf_iterator<char>(file)),
                      std::istreambuf_iterator<char>());

  if (file.bad()) {
    std::cerr << "错误: 读取文件时出错: " << filePath.string() << std::endl;
    // 即使读取出错，可能已经读了一部分，看是否需要返回部分内容
    // return ""; // 或者返回已读取的部分 content
    return ""; // 当前选择读取出错返回空
  }

  return content;
}

std::string escapeXmlChars(const std::string &input) {
  std::string result;
  result.reserve(input.size() * 1.1); // 稍微预留多一点空间以减少重分配

  for (size_t i = 0; i < input.size(); ++i) {
    unsigned char c = input[i];

    // 检查是否是 XML 1.0 不允许的 C0 控制字符 (除了 TAB, LF, CR)
    // 允许: #x9 (TAB), #xA (LF), #xD (CR), #x20-#xD7FF, #xE000-#xFFFD,
    // #x10000-#x10FFFF 不允许: #x0-#x8, #xB, #xC, #xE-#x1F, #x7F (DEL)
    // (注意：这个检查对于多字节 UTF-8
    // 字符可能不完全充分，但能过滤掉主要的非法单字节控制符)
    if ((c <= 0x08) || (c == 0x0B) || (c == 0x0C) || (c >= 0x0E && c <= 0x1F) ||
        (c == 0x7F)) {
      // 选择忽略这些非法字符
      continue;
    }

    switch (c) {
      case '<':
        result += "<";
        break;
      case '>':
        result += ">";
        break; // '>' 在 CDATA 中通常不必转义，但为了安全可以转义
      case '&':
        result += "&";
        break;
        // CDATA 内部 '"' 和 ''' 不需要转义，但如果这段代码也用于属性值，则需要
        // case '\"': result += """; break;
        // case '\'': result += "'"; break;

        // 最重要的：处理 CDATA 结束标记 `]]>`
        // 检查是否即将形成 "]]>"
      case ']':
        if (i + 2 < input.size() && input[i + 1] == ']' && input[i + 2] == '>') {
          // 遇到 "]]>"，将其替换为 "]]>" (或其他方式如 "]]" " >")
          result += "]]>";
          i += 2; // 跳过已经处理的 ']' 和 '>'
        } else {
          result += c; // 不是 "]]>" 的一部分，直接添加 ']'
        }
        break;

      default:
        result += c; // 其他字符直接添加
        break;
    }
  }
  return result;
}

// --- 新增的共享函数 ---

/**
 * @brief 将单个文件的内容写入打开的 XML 文件流
 * @param xmlFile 输出 XML 文件流 (必须已打开且为二进制模式)
 * @param filePath 要读取内容的文件路径
 * @param pathAttributeValue 在 XML 的 path 属性中使用的值
 * (通常是相对路径或引用文件中的路径)
 * @return true 如果成功读取并写入, false 如果文件读取失败
 */
bool writeXmlFileEntry(std::ofstream &xmlFile, const fs::path &filePath,
                       const std::string &pathAttributeValue) {
  std::string content = readFileContent(filePath);
  // readFileContent 内部会报告错误，这里再次检查
  if (content.empty() && !fs::is_empty(filePath)) {
    // 如果文件非空但读取内容为空，说明读取失败（或者文件确实只包含非法字符）
    std::cerr << "警告: 文件 '" << filePath.string()
              << "' 读取内容为空或失败，跳过写入XML。" << std::endl;
    return false; // 明确返回失败
  }

  // 对 pathAttributeValue 进行 XML 属性值转义 (&, <, >, ", ')
  std::string escapedPath = pathAttributeValue;
  std::string tempPath;
  tempPath.reserve(escapedPath.size());
  for (char c: escapedPath) {
    switch (c) {
      case '&':
        tempPath += "&";
        break;
      case '<':
        tempPath += "<";
        break;
      case '>':
        tempPath += ">";
        break; // > 在属性中通常不需转义，但转义更安全
      case '\"':
        tempPath += "\"";
        break;
      case '\'':
        tempPath += "'";
        break;
        // XML 属性值不允许直接包含换行、制表符等，如果路径中可能出现，需要处理
      case '\n':
        tempPath += "\n";
        break;
      case '\r':
        tempPath += "\r";
        break;
      case '\t':
        tempPath += "	";
        break;
      default:
        tempPath += c;
        break;
    }
  }
  escapedPath = tempPath;

  // 写入 XML 节点
  xmlFile << "  <file path=\"" << escapedPath << "\">\n"; // 使用 \n
  xmlFile << "    <![CDATA[";

  // !!! 注意：escapeXmlChars 现在应该处理 CDATA 中的 "]]>"
  std::string escapedContent = escapeXmlChars(content);
  xmlFile << escapedContent;

  xmlFile << "]]>\n";       // 使用 \n
  xmlFile << "  </file>\n"; // 使用 \n

  return true;
}

// --- 处理逻辑函数 ---

/**
 * @brief 通过递归扫描目录来合并代码文件
 * @param rootPath 要扫描的根目录路径
 * @return 0 表示成功, 非 0 表示失败
 */
int mergeByDir(const fs::path &rootPath) {
  std::cout << "模式: 按目录扫描\n";
  std::cout << "根目录: " << rootPath.string() << std::endl;

  // 检查路径是否存在且是目录
  std::error_code ec_check;
  if (!fs::exists(rootPath, ec_check) ||
      !fs::is_directory(rootPath, ec_check)) {
    if (ec_check) {
      std::cerr << "错误: 检查路径 '" << rootPath.string()
                << "' 时出错: " << ec_check.message() << std::endl;
    } else {
      std::cerr << "错误: " << rootPath.string() << " 不存在或不是一个目录"
                << std::endl;
    }
    return 1;
  }

  // 创建输出 XML 文件 - 放在根目录旁边
  fs::path outputFile =
    rootPath.parent_path() / (rootPath.filename().string() + "_merge.xml");

  // ****************************************************************
  // * 修改：以二进制模式打开输出文件，防止自动行尾转换             *
  // ****************************************************************
  std::ofstream xmlFile(outputFile, std::ios::out | std::ios::binary);

  if (!xmlFile) {
    std::cerr << "错误: 无法创建输出文件: " << outputFile.string() << std::endl;
    return 1;
  }

  std::cout << "输出文件: " << outputFile.string() << std::endl;

  // 统计信息
  int totalScanned = 0;
  int mergedFiles = 0;
  int skippedFilesNonCode = 0;
  int skippedFilesIgnored = 0; // 包括忽略路径、读取失败、非文件等
  int skippedDirs = 0;

  // 写入 XML 头
  xmlFile << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"; // 使用 \n
  // 转义 rootPath 中的特殊字符用于 XML 属性
  std::string escapedRootPathStr;
  std::string rootPathStr = rootPath.string();
  escapedRootPathStr.reserve(rootPathStr.size());
  for (char c: rootPathStr) {
    switch (c) {
      case '&':
        escapedRootPathStr += "&";
        break;
      case '<':
        escapedRootPathStr += "<";
        break;
      case '>':
        escapedRootPathStr += ">";
        break;
      case '\"':
        escapedRootPathStr += "\"";
        break;
      case '\'':
        escapedRootPathStr += "'";
        break;
      default:
        escapedRootPathStr += c;
        break;
    }
  }
  xmlFile << "<files source_type=\"directory\" root=\"" << escapedRootPathStr
          << "\">\n"; // 使用 \n

  // 递归遍历目录
  try {
    // 使用 C++17 的 recursive_directory_iterator
    auto it = fs::recursive_directory_iterator(
      rootPath,
      fs::directory_options::skip_permission_denied // 跳过权限不足的目录
    );
    auto end = fs::recursive_directory_iterator();

    while (it != end) {
      totalScanned++;
      std::error_code ec;
      const fs::directory_entry &entry = *it;
      const fs::path &currentPath = entry.path();

      // --- 检查是否忽略 ---
      bool is_ignored = shouldIgnorePath(currentPath);

      // 检查是否是目录，捕获可能发生的错误
      bool is_dir = entry.is_directory(ec);
      if (ec) {
        std::cerr << "警告: 检查条目类型时出错 '" << currentPath.string()
                  << "': " << ec.message() << ", 跳过。" << std::endl;
        skippedFilesIgnored++;
        // 出错时安全地递增迭代器
        try {
          it.increment(ec);
          if (ec)
            break;
        } catch (...) {
          break;
        }
        continue;
      }

      if (is_ignored) {
        if (is_dir) {
          skippedDirs++;
          std::cout << "跳过忽略目录及其内容: " << currentPath.string()
                    << std::endl;
          // 阻止进入此目录
          it.disable_recursion_pending();
        } else {
          skippedFilesIgnored++; // 文件或其他在忽略路径下的条目
          std::cout << "跳过忽略路径下的条目: " << currentPath.string()
                    << std::endl;
        }
        // 安全地递增迭代器
        try {
          it.increment(ec);
          if (ec)
            break;
        } catch (...) {
          break;
        }
        continue; // 继续下一个条目
      }

      // --- 处理非忽略条目 ---
      // 检查是否是常规文件
      bool is_file = entry.is_regular_file(ec);
      if (ec) {
        std::cerr << "警告: 检查文件类型时出错 '" << currentPath.string()
                  << "': " << ec.message() << ", 跳过。" << std::endl;
        skippedFilesIgnored++;
        // 出错时安全地递增迭代器
        try {
          it.increment(ec);
          if (ec)
            break;
        } catch (...) {
          break;
        }
        continue;
      }

      if (is_file) {
        std::string extension =
          currentPath.has_extension() ? currentPath.extension().string() : "";
        if (isCodeFile(extension) ||
            isSpecialFile(currentPath.filename().string())) {
          fs::path relativePath;
          try {
            // 计算相对路径
            relativePath =
              fs::relative(currentPath, rootPath).lexically_normal();
          } catch (const fs::filesystem_error &rel_err) {
            std::cerr << "警告: 计算相对路径时出错 for '"
                      << currentPath.string() << "': " << rel_err.what()
                      << ", 使用绝对路径作为备用。" << std::endl;
            relativePath = currentPath; // 备用方案，可能需要调整
          }

          std::cout << "处理文件: " << currentPath.string()
                    << " (相对路径: " << relativePath.string() << ")"
                    << std::endl;

          // 写入 XML 条目
          if (writeXmlFileEntry(xmlFile, currentPath, relativePath.string())) {
            mergedFiles++;
          } else {
            skippedFilesIgnored++; // 读取或写入失败
          }
        } else {
          skippedFilesNonCode++;
          std::cout << "跳过文件(非代码文件): " << currentPath.string()
                    << std::endl;
        }
      } else if (is_dir) {
        // 是目录（且未被忽略），迭代器会自动进入
        std::cout << "进入目录: " << currentPath.string() << std::endl;
      } else {
        // 其他类型（符号链接等）
        std::cout << "跳过条目(非文件/目录): " << currentPath.string()
                  << std::endl;
        skippedFilesIgnored++;
      }

      // --- 安全地递增迭代器到下一个条目 ---
      try {
        it.increment(ec); // 使用带错误码的版本递增
        if (ec) {
          std::cerr << "警告: 迭代目录时出错: " << ec.message() << " at/after '"
                    << currentPath.string() << "', 停止扫描。" << std::endl;
          break; // 退出循环
        }
      } catch (const std::exception &e) {
        std::cerr << "警告: 迭代目录时发生异常: " << e.what() << " at/after '"
                  << currentPath.string() << "', 停止扫描。" << std::endl;
        break; // 退出循环
      }
    } // end while loop
  } catch (const fs::filesystem_error &e) {
    std::cerr << "\n文件系统错误 (迭代器初始化或严重错误): " << e.what()
              << std::endl;
    if (!e.path1().empty())
      std::cerr << "路径1: " << e.path1().string() << std::endl;
    if (!e.path2().empty())
      std::cerr << "路径2: " << e.path2().string() << std::endl;
    xmlFile.close(); // 尝试关闭文件
    return 1;        // 返回错误码
  } catch (const std::exception &e) {
    std::cerr << "\n发生标准库错误: " << e.what() << std::endl;
    xmlFile.close(); // 尝试关闭文件
    return 1;        // 返回错误码
  }

  // 写入 XML 尾
  xmlFile << "</files>\n"; // 使用 \n
  xmlFile.close();

  if (!xmlFile) {
    // 检查关闭后的状态
    std::cerr << "错误: 写入或关闭输出文件时出错: " << outputFile.string()
              << std::endl;
    return 1; // 返回错误码
  }

  // 输出统计信息
  std::cout << "\n==== 目录扫描处理完成 ====\n";
  std::cout << "扫描总条目数: " << totalScanned << std::endl;
  std::cout << "合并的代码文件数: " << mergedFiles << std::endl;
  std::cout << "跳过的文件数 (非代码文件): " << skippedFilesNonCode
            << std::endl;
  std::cout << "跳过的条目数 (忽略/错误/非文件): " << skippedFilesIgnored
            << std::endl;
  std::cout << "跳过的忽略目录数: " << skippedDirs << std::endl;
  std::cout << "输出文件: " << outputFile.string() << std::endl;

  return 0;
}

/**
 * @brief 通过读取引用文件中的路径列表来合并文件
 * @param refFilePath 包含文件路径列表的引用文件的路径
 * @return 0 表示成功, 非 0 表示失败
 */
int mergeByRef(const fs::path &refFilePath) {
  std::cout << "模式: 按引用文件\n";
  std::cout << "引用文件: " << refFilePath.string() << std::endl;

  // 检查引用文件是否存在且是普通文件
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

  // 创建输出 XML 文件 - 放在引用文件旁边
  fs::path outputFile = refFilePath.parent_path() /
                        (refFilePath.filename().stem().string() + "_merge.xml");
  // 使用 stem 避免 .txt.xml

  // ****************************************************************
  // * 修改：以二进制模式打开输出文件，防止自动行尾转换             *
  // ****************************************************************
  std::ofstream xmlFile(outputFile, std::ios::out | std::ios::binary);

  if (!xmlFile) {
    std::cerr << "错误: 无法创建输出文件: " << outputFile.string() << std::endl;
    return 1;
  }
  std::cout << "输出文件: " << outputFile.string() << std::endl;

  // 统计信息
  int totalLines = 0;
  int mergedFiles = 0;
  int skippedFilesNotFound = 0;
  int skippedFilesNotFile = 0;
  int skippedFilesReadError = 0; // 包括读取内容失败和写入失败
  int skippedEmptyOrComment = 0;

  // 写入 XML 头
  xmlFile << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"; // 使用 \n
  // 转义 refFilePath 中的特殊字符用于 XML 属性
  std::string escapedRefPathStr;
  std::string refPathStr = refFilePath.string();
  escapedRefPathStr.reserve(refPathStr.size());
  for (char c: refPathStr) {
    switch (c) {
      case '&':
        escapedRefPathStr += "&";
        break;
      case '<':
        escapedRefPathStr += "<";
        break;
      case '>':
        escapedRefPathStr += ">";
        break;
      case '\"':
        escapedRefPathStr += "\"";
        break;
      case '\'':
        escapedRefPathStr += "'";
        break;
      default:
        escapedRefPathStr += c;
        break;
    }
  }
  xmlFile << "<files source_type=\"reference_file\" ref_file=\""
          << escapedRefPathStr << "\">\n"; // 使用 \n

  // 打开并读取引用文件
  std::ifstream refFile(refFilePath); // 默认文本模式读取引用文件是OK的
  if (!refFile) {
    std::cerr << "错误: 无法打开引用文件: " << refFilePath.string()
              << std::endl;
    xmlFile.close(); // 关闭已创建的输出文件
    // 考虑是否删除空的输出文件
    // fs::remove(outputFile);
    return 1;
  }

  std::string line;
  while (std::getline(refFile, line)) {
    totalLines++;

    // 移除可能的行尾 \r
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }

    // 移除前后空白 (包括可能的 BOM 导致的空白感)
    size_t first = line.find_first_not_of(" \t\n\r\f\v");
    if (std::string::npos == first) {
      // 整行都是空白
      skippedEmptyOrComment++;
      continue;
    }
    size_t last = line.find_last_not_of(" \t\n\r\f\v");
    line = line.substr(first, (last - first + 1));

    // 移除 UTF-8 BOM (EF BB BF) - 应该在去除空白前做，但这里也行
    if (line.size() >= 3 && (unsigned char) line[0] == 0xEF &&
        (unsigned char) line[1] == 0xBB && (unsigned char) line[2] == 0xBF) {
      line.erase(0, 3);
      // 再次检查是否变空
      if (line.empty()) {
        skippedEmptyOrComment++;
        continue;
      }
    }

    if (line.empty() || line[0] == '#') {
      // 跳过空行和注释行（以#开头）
      skippedEmptyOrComment++;
      continue;
    }

    fs::path targetFilePath = line; // 直接使用行内容作为路径

    // 检查路径是否存在且是文件
    std::error_code ec;
    bool exists = fs::exists(targetFilePath, ec);
    if (ec) {
      std::cerr << "警告: 检查路径 '" << line << "' 时出错: " << ec.message()
                << ", 跳过。" << std::endl;
      skippedFilesNotFound++; // 归为找不到
      continue;
    }
    if (!exists) {
      std::cerr << "警告: 文件不存在 '" << line << "', 跳过。" << std::endl;
      skippedFilesNotFound++;
      continue;
    }

    bool isFile = fs::is_regular_file(targetFilePath, ec);
    if (ec) {
      std::cerr << "警告: 检查文件类型 '" << line
                << "' 时出错: " << ec.message() << ", 跳过。" << std::endl;
      skippedFilesNotFile++; // 归为非文件
      continue;
    }
    if (!isFile) {
      std::cerr << "警告: 路径不是一个常规文件 '" << line << "', 跳过。"
                << std::endl;
      skippedFilesNotFile++;
      continue;
    }

    std::cout << "处理文件: " << targetFilePath.string() << " (来自引用文件)"
              << std::endl;

    // 使用行内原始路径(line)作为 XML 的 path 属性值
    if (writeXmlFileEntry(xmlFile, targetFilePath, line)) {
      mergedFiles++;
    } else {
      skippedFilesReadError++; // 写入失败（通常是读取内容失败）
    }
  } // end while loop

  refFile.close();

  // 写入 XML 尾
  xmlFile << "</files>\n"; // 使用 \n
  xmlFile.close();

  if (!xmlFile) {
    // 检查关闭后的状态
    std::cerr << "错误: 写入或关闭输出文件时出错: " << outputFile.string()
              << std::endl;
    return 1; // 返回错误码
  }

  // 输出统计信息
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
  // 验证统计
  if (attemptedFiles != mergedFiles + skippedFilesNotFound +
                        skippedFilesNotFile + skippedFilesReadError) {
    std::cout << "警告: 统计数字加总不匹配尝试处理的文件数，请检查日志。"
              << std::endl;
  }
  std::cout << "输出文件: " << outputFile.string() << std::endl;

  return 0;
}

// --- 主函数 ---

int main(int argc, char *argv[]) {
  fs::path inputPath;
  int result = 1; // 默认失败
  std::error_code ec;

  if (argc == 1) {
    // 无参数，提示用户输入目录路径
    std::string dirInput;
    std::cout << "请输入要扫描的目录路径: ";
    std::getline(std::cin, dirInput);
    if (dirInput.empty()) {
      std::cerr << "未输入任何路径，程序退出。" << std::endl;
      std::cout << "\n按任意键退出..." << std::endl;
      while (_kbhit())
        _getch();
      _getch();
      return 1;
    }
    try {
      inputPath = fs::absolute(dirInput).lexically_normal();
    } catch (const std::exception &e) {
      std::cerr << "错误: 处理输入路径时出错: " << e.what() << std::endl;
      while (_kbhit())
        _getch();
      _getch();
      return 1;
    }
    result = mergeByDir(inputPath);
  } else if (argc == 2) {
    try {
      inputPath = argv[1];
      inputPath = fs::absolute(inputPath).lexically_normal();
    } catch (const std::exception &e) {
      std::cerr << "错误: 处理输入路径时出错: " << e.what() << std::endl;
      while (_kbhit())
        _getch();
      _getch();
      return 1;
    }
    if (fs::is_directory(inputPath, ec)) {
      result = mergeByDir(inputPath);
    } else if (fs::is_regular_file(inputPath, ec)) {
      result = mergeByRef(inputPath);
    } else {
      if (ec) {
        std::cerr << "错误: 无法访问或确定路径类型 '" << inputPath.string()
                  << "': " << ec.message() << std::endl;
      } else {
        if (fs::exists(inputPath)) {
          std::cerr << "错误: 输入路径 '" << inputPath.string()
                    << "' 存在但不是一个目录或常规文件。" << std::endl;
        } else {
          std::cerr << "错误: 输入路径 '" << inputPath.string() << "' 不存在。"
                    << std::endl;
        }
      }
      result = 1;
    }
    if (ec && result != 0) {
      std::cerr << "错误: 检查输入路径 '" << inputPath.string()
                << "' 类型时出错: " << ec.message() << std::endl;
      result = 1;
    }
  } else {
    std::cerr << "用法: " << argv[0] << " <目录路径 | 引用文件路径>"
              << std::endl;
    std::cerr << "  <目录路径>: 扫描该目录下所有符合条件的代码文件。"
              << std::endl;
    std::cerr << "  <引用文件路径>: 读取该文本文件中列出的文件路径进行合并 "
                 "(一行一个路径, '#'开头或空行为注释/忽略)。"
              << std::endl;
    std::cout << "\n按任意键退出..." << std::endl;
    while (_kbhit())
      _getch();
    _getch();
    return 1;
  }

  std::cout << "\n处理结束 (" << (result == 0 ? "成功" : "失败")
            << ")，按任意键退出..." << std::endl;
  while (_kbhit())
    _getch();
  _getch();
  return result;
}
