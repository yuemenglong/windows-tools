#include <iostream>
#include <string>
#include <filesystem>
#include <fstream>
#include <vector>
#include <set>
#include <conio.h> // 用于getch()函数

namespace fs = std::filesystem;

bool isCodeFile(const std::string& extension) {
    static const std::set<std::string> codeExtensions = {
        ".c", ".cpp", ".h", ".hpp", ".cc", ".cxx", ".hxx", 
        ".java", ".py", ".js", ".ts", ".go", ".dart"
    };
    
    return codeExtensions.find(extension) != codeExtensions.end();
}

bool shouldIgnorePath(const fs::path& path) {
    static const std::set<std::string> ignoreDirs = {
        "venv", ".venv", "node_modules", ".git", "__pycache__", 
        "build", "dist", "bin", "obj", "target", ".idea", ".vs"
    };
    
    // 检查路径的每一部分是否在忽略列表中
    for (const auto& part : path) {
        if (ignoreDirs.find(part.string()) != ignoreDirs.end()) {
            return true;
        }
    }
    
    return false;
}

std::string readFileContent(const fs::path& filePath) {
    std::ifstream file(filePath, std::ios::in);  // 使用文本模式而非二进制模式
    if (!file) {
        std::cerr << "无法打开文件: " << filePath << std::endl;
        return "";
    }
    
    std::string content;
    std::string line;
    bool firstLine = true;
    
    while (std::getline(file, line)) {
        if (!firstLine) {
            content += '\n';  // 只在非第一行前添加换行符
        } else {
            firstLine = false;
        }
        content += line;  // 添加行内容
    }
    
    return content;
}

// 转义XML特殊字符并过滤不可见字符
std::string escapeXmlChars(const std::string& input) {
    std::string result;
    result.reserve(input.size() * 1.1); // 预留一些额外空间
    
    for (unsigned char c : input) {
        // 保留常用的控制字符（换行、回车、制表符）
        bool isVisibleOrAllowed = (c >= 32 && c != 127) || c == '\n' || c == '\r' || c == '\t';
        
        if (!isVisibleOrAllowed) {
            // 忽略不可见字符
            continue;
        } else if (c == '<') {
            result += "&lt;";
        } else if (c == '>') {
            result += "&gt;";
        } else if (c == '&') {
            result += "&amp;";
        } else if (c == '\"') {
            result += "&quot;";
        } else if (c == '\'') {
            result += "&apos;";
        } else {
            result += c;
        }
    }
    
    return result;
}

int main(int argc, char *argv[]) {
  /*参数只有一个，是一个目录的路径，我们叫根路径
   * 首先检查根路径是否存在且是一个路径
   * 然后深度遍历根路径中的每一个文件，并过滤只保留常见的代码文件
   * 例如.c/.cpp/.h/.hpp/.cc/.cxx/.hxx/.java/.py/.js/.ts/.go
   * 生成一个xml，每个节点都代表一个源码文件，格式为：
   * <file path="相对根路径的路径">
   * // 使用CDATA包裹源码文件的内容
   * </file>
   * 将生成的xml存入根路径下_merge.xml
   * */
  
  // 统计信息
  int totalFiles = 0;
  int mergedFiles = 0;
  int skippedFiles = 0;
  int skippedDirs = 0;
  std::vector<std::string> mergedFilePaths;
  std::vector<std::string> skippedFilePaths;
  std::vector<std::string> skippedDirPaths;
  
  // 检查命令行参数
  if (argc != 2) {
      std::cerr << "用法: " << argv[0] << " <目录路径>" << std::endl;
      std::cout << "按任意键退出..." << std::endl;
      _getch();
      return 1;
  }
  
  // 获取根路径
  fs::path rootPath = argv[1];
  
  // 检查路径是否存在且是目录
  if (!fs::exists(rootPath) || !fs::is_directory(rootPath)) {
      std::cerr << "错误: " << rootPath << " 不存在或不是一个目录" << std::endl;
      std::cout << "按任意键退出..." << std::endl;
      _getch();
      return 1;
  }
  
  std::cout << "开始处理目录: " << rootPath << std::endl;
  
  // 创建输出XML文件 - 修改为根目录+_merge.xml格式
  fs::path outputFile = rootPath.string() + "_merge.xml";
  std::ofstream xmlFile(outputFile);
  
  if (!xmlFile) {
      std::cerr << "无法创建输出文件: " << outputFile << std::endl;
      std::cout << "按任意键退出..." << std::endl;
      _getch();
      return 1;
  }
  
  // 写入XML头
  xmlFile << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
  xmlFile << "<files>\n";
  
  // 递归遍历目录
  try {
      for (const auto& entry : fs::recursive_directory_iterator(rootPath)) {
          if (fs::is_directory(entry)) {
              totalFiles++; // 统计目录
              // 检查是否应该忽略该目录
              if (shouldIgnorePath(entry.path())) {
                  skippedDirs++;
                  skippedDirPaths.push_back(entry.path().string());
                  std::cout << "跳过目录: " << entry.path().string() << std::endl;
                  continue;
              }
          } else if (fs::is_regular_file(entry)) {
              totalFiles++; // 统计文件
              // 检查是否应该忽略该路径
              if (shouldIgnorePath(entry.path())) {
                  skippedFiles++;
                  skippedFilePaths.push_back(entry.path().string());
                  std::cout << "跳过文件(位于忽略目录): " << entry.path().string() << std::endl;
                  continue;
              }
              
              std::string extension = entry.path().extension().string();
              
              // 检查是否是代码文件
              if (isCodeFile(extension)) {
                  // 获取相对路径
                  fs::path relativePath = fs::relative(entry.path(), rootPath);
                  std::string content = readFileContent(entry.path());
                  std::string escapedContent = escapeXmlChars(content);
                  
                  // 写入XML节点
                  xmlFile << "  <file path=\"" << relativePath.string() << "\">\n";
                  xmlFile << "    <![CDATA[" << escapedContent << "]]>\n";
                  xmlFile << "  </file>\n";
                  
                  mergedFiles++;
                  mergedFilePaths.push_back(entry.path().string());
                  std::cout << "处理文件: " << entry.path().string() << std::endl;
              } else {
                  // 不是代码文件，跳过
                  skippedFiles++;
                  skippedFilePaths.push_back(entry.path().string());
                  std::cout << "跳过文件(非代码文件): " << entry.path().string() << std::endl;
              }
          }
      }
  } catch (const fs::filesystem_error& e) {
      std::cerr << "文件系统错误: " << e.what() << std::endl;
      std::cout << "按任意键退出..." << std::endl;
      _getch();
      return 1;
  }
  
  // 写入XML尾
  xmlFile << "</files>\n";
  xmlFile.close();
  
  // 输出统计信息
  std::cout << "\n==== 处理完成 ====\n";
  std::cout << "扫描总文件/目录数: " << totalFiles << std::endl;
  std::cout << "合并的代码文件数: " << mergedFiles << std::endl;
  std::cout << "跳过的文件数: " << skippedFiles << std::endl;
  std::cout << "跳过的目录数: " << skippedDirs << std::endl;
  std::cout << "输出文件: " << outputFile << std::endl;
  
  // 等待用户按任意键退出
  std::cout << "\n按任意键退出..." << std::endl;
  _getch();
  
  return 0;
}