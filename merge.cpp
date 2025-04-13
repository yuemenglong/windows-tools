#include <iostream>
#include <string>
#include <filesystem>
#include <fstream>
#include <vector>
#include <set>
#include <sstream> // 用于字符串流处理（如果需要更复杂的行解析）
#include <conio.h> // 用于getch()函数

namespace fs = std::filesystem;

// --- 辅助函数 (基本不变) ---

bool isCodeFile(const std::string& extension)
{
    static const std::set<std::string> codeExtensions = {
        ".c", ".cpp", ".h", ".hpp", ".cc", ".cxx", ".hxx",
        ".java", ".py", ".js", ".ts", ".go", ".dart", "kt", ".cs", // 添加了 .cs 示例
        // 根据需要添加更多扩展名
    };
    // 将扩展名转为小写进行比较，更健壮
    std::string lowerExtension = extension;
    std::transform(lowerExtension.begin(), lowerExtension.end(), lowerExtension.begin(), ::tolower);
    return codeExtensions.count(lowerExtension); // 使用 count 比 find 更简洁
}

// 这个函数主要用于目录扫描时的过滤
bool shouldIgnorePath(const fs::path& path)
{
    static const std::set<std::string> ignoreDirs = {
        "venv", ".venv", "node_modules", ".git", "__pycache__",
        "build", "dist", "bin", "obj", "target", ".idea", ".vs",
        ".vscode" // 添加 .vscode 示例
    };

    try
    {
        for (const auto& part : path)
        {
            if (ignoreDirs.count(part.string()))
            {
                return true;
            }
        }
    }
    catch (const std::exception& e)
    {
        // 路径迭代可能因特殊字符等失败
        std::cerr << "警告: 检查路径时出错 '" << path.string() << "': " << e.what() << std::endl;
        return true; // 出错时倾向于忽略
    }
    return false;
}

std::string readFileContent(const fs::path& filePath)
{
    // 显式使用 std::ios::binary 来读取原始字节，避免文本模式下的行尾转换问题
    // 然后在 escapeXmlChars 中处理换行符 (\n, \r)
    std::ifstream file(filePath, std::ios::in | std::ios::binary);
    if (!file)
    {
        std::cerr << "错误: 无法打开文件: " << filePath.string() << std::endl;
        return "";
    }

    // 更高效地读取整个文件
    std::string content((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());

    if (file.bad())
    {
        std::cerr << "错误: 读取文件时出错: " << filePath.string() << std::endl;
        return ""; // 读取出错返回空
    }

    return content;
}

std::string escapeXmlChars(const std::string& input)
{
    std::string result;
    result.reserve(input.size()); // 先预留原始大小，不够会自动扩展

    for (unsigned char c : input)
    {
        // 保留基本可见ASCII字符和允许的控制字符
        bool isVisibleOrAllowed = (c >= 32 && c <= 126) || c == '\n' || c == '\r' || c == '\t';

        // 检查是否是有效的 UTF-8 序列的一部分（这是一个简化检查，不完全严格）
        // XML 1.0 允许 #x9 | #xA | #xD | [#x20-#xD7FF] | [#xE000-#xFFFD] | [#x10000-#x10FFFF]
        // 此处简化为只过滤掉严格的 C0 控制字符（除了允许的）和 DEL (127)
        // 对于多字节 UTF-8 字符，其组成字节通常 > 127，这里没有过滤它们

        if (!isVisibleOrAllowed && c != 127)
        {
            // 可以选择替换为占位符，或者直接忽略
            // result += '?'; // 例如，替换为问号
            continue; // 当前选择忽略
        }
        else if (c == '<') result += "<";
        else if (c == '>') result += ">";
        else if (c == '&') result += "&";
            // CDATA 中 " 和 ' 不需要转义，但为了通用性，可以保留
            // else if (c == '\"') result += """;
            // else if (c == '\'') result += "'";
            // 最重要的：CDATA 结束标记 `]]>` 需要特殊处理，虽然概率小
            // 如果遇到 "]]>"，可以替换为 "]]>" 或拆开写 "]]" " >"
        else if (result.size() >= 2 && result.substr(result.size() - 2) == "]]" && c == '>')
        {
            result += ">"; // 转义 CDATA 结束符中的 '>'
        }
        else result += c;
    }
    return result;
}


// --- 新增的共享函数 ---

/**
 * @brief 将单个文件的内容写入打开的 XML 文件流
 * @param xmlFile 输出 XML 文件流 (必须已打开)
 * @param filePath 要读取内容的文件路径
 * @param pathAttributeValue 在 XML 的 path 属性中使用的值 (通常是相对路径或引用文件中的路径)
 * @return true 如果成功读取并写入, false 如果文件读取失败
 */
bool writeXmlFileEntry(std::ofstream& xmlFile, const fs::path& filePath, const std::string& pathAttributeValue)
{
    std::string content = readFileContent(filePath);
    // 即使 readFileContent 失败并打印错误，我们仍检查这里
    if (content.empty() && !fs::is_empty(filePath))
    {
        // 如果文件非空但读取内容为空，说明读取失败
        std::cerr << "警告: 文件 '" << filePath.string() << "' 读取内容为空或失败，跳过写入XML。" << std::endl;
        return false;
    }

    std::string escapedContent = escapeXmlChars(content);

    // 写入 XML 节点
    // 对 pathAttributeValue 本身也应该做最小的 XML 属性转义 (&, <, >, ", ')
    std::string escapedPath = pathAttributeValue;
    size_t pos = 0;
    while ((pos = escapedPath.find('&', pos)) != std::string::npos)
    {
        escapedPath.replace(pos, 1, "&");
        pos += 5;
    }
    pos = 0;
    while ((pos = escapedPath.find('<', pos)) != std::string::npos)
    {
        escapedPath.replace(pos, 1, "<");
        pos += 4;
    }
    pos = 0;
    while ((pos = escapedPath.find('>', pos)) != std::string::npos)
    {
        escapedPath.replace(pos, 1, ">");
        pos += 4;
    }
    pos = 0;
    while ((pos = escapedPath.find('\"', pos)) != std::string::npos)
    {
        escapedPath.replace(pos, 1, "\"");
        pos += 6;
    }
    pos = 0;
    while ((pos = escapedPath.find('\'', pos)) != std::string::npos)
    {
        escapedPath.replace(pos, 1, "'");
        pos += 6;
    }


    xmlFile << "  <file path=\"" << escapedPath << "\">\n";
    // 确保 CDATA 内容前后有换行，更易读，但不是必须
    xmlFile << "    <![CDATA[";
    // 写入时再检查一次 CDATA 结束符
    pos = 0;
    while ((pos = escapedContent.find("]]>", pos)) != std::string::npos)
    {
        escapedContent.replace(pos, 3, "]]>");
        pos += 6; // 移动到替换后的位置之后
    }
    xmlFile << escapedContent;
    xmlFile << "]]>\n";
    xmlFile << "  </file>\n";

    return true;
}

// --- 处理逻辑函数 ---

/**
 * @brief 通过递归扫描目录来合并代码文件
 * @param rootPath 要扫描的根目录路径
 * @return 0 表示成功, 非 0 表示失败
 */
int mergeByDir(const fs::path& rootPath)
{
    std::cout << "模式: 按目录扫描\n";
    std::cout << "根目录: " << rootPath.string() << std::endl;

    // 检查路径是否存在且是目录
    if (!fs::exists(rootPath) || !fs::is_directory(rootPath))
    {
        std::cerr << "错误: " << rootPath.string() << " 不存在或不是一个目录" << std::endl;
        return 1;
    }

    // 创建输出 XML 文件 - 放在根目录旁边
    fs::path outputFile = rootPath.parent_path() / (rootPath.filename().string() + "_merge.xml");
    std::ofstream xmlFile(outputFile);

    if (!xmlFile)
    {
        std::cerr << "错误: 无法创建输出文件: " << outputFile.string() << std::endl;
        return 1;
    }

    std::cout << "输出文件: " << outputFile.string() << std::endl;

    // 统计信息
    int totalScanned = 0;
    int mergedFiles = 0;
    int skippedFilesNonCode = 0;
    int skippedFilesIgnored = 0;
    int skippedDirs = 0;
    // std::vector<std::string> mergedFilePaths; // 如果需要详细列表可以取消注释
    // std::vector<std::string> skippedFilePaths;
    // std::vector<std::string> skippedDirPaths;

    // 写入 XML 头
    xmlFile << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    xmlFile << "<files source_type=\"directory\" root=\"" << rootPath.string() << "\">\n"; // 添加源类型和根路径信息

    // 递归遍历目录
    try
    {
        for (const auto& entry : fs::recursive_directory_iterator(
                 rootPath, fs::directory_options::skip_permission_denied))
        {
            totalScanned++;
            const auto& currentPath = entry.path();

            // 检查是否应该忽略整个路径（基于目录名）
            if (shouldIgnorePath(currentPath))
            {
                if (fs::is_directory(entry))
                {
                    skippedDirs++;
                    // skippedDirPaths.push_back(currentPath.string());
                    std::cout << "跳过忽略目录及其内容: " << currentPath.string() << std::endl;
                    // 告诉迭代器不要进入这个目录 (如果支持的话，C++17标准迭代器本身不直接支持跳过)
                    // 对于标准库，通常需要检查每个文件/子目录是否在忽略路径下
                    // 如果目录被忽略，其下的文件也会在文件检查时被 shouldIgnorePath 捕获
                    if (fs::is_directory(entry))
                    {
                        // 确保确实是目录再操作
                        entry.disable_recursion_pending(); // 尝试阻止进入下一层
                    }
                }
                else if (fs::is_regular_file(entry))
                {
                    skippedFilesIgnored++;
                    // skippedFilePaths.push_back(currentPath.string());
                    std::cout << "跳过文件(位于忽略目录): " << currentPath.string() << std::endl;
                }
                continue; // 跳过这个条目
            }

            // 处理文件
            if (fs::is_regular_file(entry))
            {
                std::string extension = currentPath.extension().string();

                // 检查是否是代码文件
                if (isCodeFile(extension))
                {
                    // 获取相对路径
                    fs::path relativePath = fs::relative(currentPath, rootPath);

                    std::cout << "处理文件: " << currentPath.string() << " (相对路径: " << relativePath.string() << ")" <<
                        std::endl;

                    if (writeXmlFileEntry(xmlFile, currentPath, relativePath.string()))
                    {
                        mergedFiles++;
                        // mergedFilePaths.push_back(currentPath.string());
                    }
                    else
                    {
                        skippedFilesIgnored++; // 读取失败也算作跳过
                        // skippedFilePaths.push_back(currentPath.string());
                    }
                }
                else
                {
                    // 不是代码文件，跳过
                    skippedFilesNonCode++;
                    // skippedFilePaths.push_back(currentPath.string());
                    std::cout << "跳过文件(非代码文件): " << currentPath.string() << std::endl;
                }
            }
            else if (fs::is_directory(entry))
            {
                std::cout << "进入目录: " << currentPath.string() << std::endl;
                // 目录本身不需要特殊处理，迭代器会自动进入
            }
            else
            {
                // 其他类型的文件（如符号链接等）
                std::cout << "跳过条目(非文件/目录): " << currentPath.string() << std::endl;
            }
        } // end for loop
    }
    catch (const fs::filesystem_error& e)
    {
        std::cerr << "\n文件系统错误: " << e.what() << std::endl;
        std::cerr << "路径1: " << e.path1().string() << std::endl;
        if (!e.path2().empty())
        {
            std::cerr << "路径2: " << e.path2().string() << std::endl;
        }
        xmlFile.close(); // 确保关闭文件
        return 1;
    } catch (const std::exception& e)
    {
        std::cerr << "\n发生错误: " << e.what() << std::endl;
        xmlFile.close();
        return 1;
    }

    // 写入 XML 尾
    xmlFile << "</files>\n";
    xmlFile.close();

    // 输出统计信息
    std::cout << "\n==== 目录扫描处理完成 ====\n";
    std::cout << "扫描总条目数: " << totalScanned << std::endl;
    std::cout << "合并的代码文件数: " << mergedFiles << std::endl;
    std::cout << "跳过的文件数 (非代码文件): " << skippedFilesNonCode << std::endl;
    std::cout << "跳过的文件数 (忽略路径或读取失败): " << skippedFilesIgnored << std::endl;
    std::cout << "跳过的忽略目录数: " << skippedDirs << std::endl;
    std::cout << "输出文件: " << outputFile.string() << std::endl;

    return 0;
}

/**
 * @brief 通过读取引用文件中的路径列表来合并文件
 * @param refFilePath 包含文件路径列表的引用文件的路径
 * @return 0 表示成功, 非 0 表示失败
 */
int mergeByRef(const fs::path& refFilePath)
{
    std::cout << "模式: 按引用文件\n";
    std::cout << "引用文件: " << refFilePath.string() << std::endl;

    // 检查引用文件是否存在且是普通文件
    if (!fs::exists(refFilePath) || !fs::is_regular_file(refFilePath))
    {
        std::cerr << "错误: 引用文件 " << refFilePath.string() << " 不存在或不是一个文件" << std::endl;
        return 1;
    }

    // 创建输出 XML 文件 - 放在引用文件旁边
    fs::path outputFile = refFilePath.parent_path() / (refFilePath.filename().string() + "_merge.xml");
    std::ofstream xmlFile(outputFile);

    if (!xmlFile)
    {
        std::cerr << "错误: 无法创建输出文件: " << outputFile.string() << std::endl;
        return 1;
    }
    std::cout << "输出文件: " << outputFile.string() << std::endl;

    // 统计信息
    int totalLines = 0;
    int mergedFiles = 0;
    int skippedFilesNotFound = 0;
    int skippedFilesNotFile = 0;
    int skippedFilesReadError = 0;

    // 写入 XML 头
    xmlFile << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    xmlFile << "<files source_type=\"reference_file\" ref_file=\"" << refFilePath.string() << "\">\n";
    // 添加源类型和引用文件路径

    // 打开并读取引用文件
    std::ifstream refFile(refFilePath);
    if (!refFile)
    {
        std::cerr << "错误: 无法打开引用文件: " << refFilePath.string() << std::endl;
        xmlFile.close(); // 关闭已创建的输出文件
        fs::remove(outputFile); // 删除空的输出文件
        return 1;
    }

    std::string line;
    while (std::getline(refFile, line))
    {
        totalLines++;
        // 移除可能的 BOM 和前后空白
        // 简单的空白移除
        line.erase(0, line.find_first_not_of(" \t\n\r\f\v"));
        line.erase(line.find_last_not_of(" \t\n\r\f\v") + 1);
        // 移除 UTF-8 BOM (EF BB BF)
        if (line.size() >= 3 && (unsigned char)line[0] == 0xEF && (unsigned char)line[1] == 0xBB && (unsigned char)
            line[2] == 0xBF)
        {
            line.erase(0, 3);
        }


        if (line.empty() || line[0] == '#')
        {
            // 跳过空行和注释行（以#开头）
            continue;
        }

        fs::path targetFilePath = line; // 直接使用行内容作为路径

        // 检查路径是否存在且是文件
        std::error_code ec;
        bool exists = fs::exists(targetFilePath, ec);
        if (ec)
        {
            std::cerr << "警告: 检查路径 '" << line << "' 时出错: " << ec.message() << ", 跳过。" << std::endl;
            skippedFilesNotFound++; // 或归为其他错误
            continue;
        }
        if (!exists)
        {
            std::cerr << "警告: 文件不存在 '" << line << "', 跳过。" << std::endl;
            skippedFilesNotFound++;
            continue;
        }

        bool isFile = fs::is_regular_file(targetFilePath, ec);
        if (ec)
        {
            std::cerr << "警告: 检查文件类型 '" << line << "' 时出错: " << ec.message() << ", 跳过。" << std::endl;
            skippedFilesNotFile++; // 或归为其他错误
            continue;
        }
        if (!isFile)
        {
            std::cerr << "警告: 路径不是一个常规文件 '" << line << "', 跳过。" << std::endl;
            skippedFilesNotFile++;
            continue;
        }

        std::cout << "处理文件: " << targetFilePath.string() << " (来自引用文件)" << std::endl;

        // 使用行内原始路径作为 XML 的 path 属性值
        if (writeXmlFileEntry(xmlFile, targetFilePath, line))
        {
            mergedFiles++;
        }
        else
        {
            skippedFilesReadError++; // 写入失败（通常是读取内容失败）
        }
    } // end while loop

    refFile.close();

    // 写入 XML 尾
    xmlFile << "</files>\n";
    xmlFile.close();

    // 输出统计信息
    std::cout << "\n==== 引用文件处理完成 ====\n";
    std::cout << "引用文件总行数: " << totalLines << std::endl;
    std::cout << "尝试合并的文件数 (来自有效行): " << (mergedFiles + skippedFilesNotFound + skippedFilesNotFile +
        skippedFilesReadError) << std::endl;
    std::cout << "成功合并的文件数: " << mergedFiles << std::endl;
    std::cout << "跳过的文件数 (未找到): " << skippedFilesNotFound << std::endl;
    std::cout << "跳过的文件数 (非文件): " << skippedFilesNotFile << std::endl;
    std::cout << "跳过的文件数 (读取错误): " << skippedFilesReadError << std::endl;
    std::cout << "输出文件: " << outputFile.string() << std::endl;

    return 0;
}


// --- 主函数 ---

int main(int argc, char* argv[])
{
    // 检查命令行参数
    if (argc != 2)
    {
        std::cerr << "用法: " << argv[0] << " <目录路径 | 引用文件路径>" << std::endl;
        std::cerr << "  <目录路径>: 扫描该目录下所有代码文件。" << std::endl;
        std::cerr << "  <引用文件路径>: 读取该文件中列出的文件路径进行合并。" << std::endl;
        std::cout << "\n按任意键退出..." << std::endl;
        _getch();
        return 1;
    }

    fs::path inputPath = argv[1];
    int result = 1; // 默认失败

    std::error_code ec;
    if (fs::is_directory(inputPath, ec))
    {
        result = mergeByDir(inputPath);
    }
    else if (fs::is_regular_file(inputPath, ec))
    {
        result = mergeByRef(inputPath);
    }
    else
    {
        if (ec)
        {
            // 检查路径时发生错误
            std::cerr << "错误: 无法访问路径 '" << inputPath.string() << "': " << ec.message() << std::endl;
        }
        else if (fs::exists(inputPath))
        {
            // 路径存在但不是目录也不是文件
            std::cerr << "错误: 输入路径 '" << inputPath.string() << "' 不是一个目录也不是一个常规文件。" << std::endl;
        }
        else
        {
            // 路径不存在
            std::cerr << "错误: 输入路径 '" << inputPath.string() << "' 不存在。" << std::endl;
        }
        result = 1; // 确保返回错误码
    }

    if (ec && result != 0)
    {
        // 如果之前的 is_directory/is_regular_file 检查本身就出错了
        std::cerr << "错误: 检查输入路径类型时出错: " << ec.message() << std::endl;
        result = 1; // 确保返回错误码
    }


    // 等待用户按任意键退出
    std::cout << "\n处理结束，按任意键退出..." << std::endl;
    _getch();

    return result; // 返回处理函数的结果
}
