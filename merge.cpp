#include <iostream>
#include <string>
#include <filesystem>
#include <fstream>
#include <vector>
#include <set>

namespace fs = std::filesystem;

bool isCodeFile(const std::string& extension) {
    static const std::set<std::string> codeExtensions = {
        ".c", ".cpp", ".h", ".hpp", ".cc", ".cxx", ".hxx", 
        ".java", ".py", ".js", ".ts", ".go"
    };
    
    return codeExtensions.find(extension) != codeExtensions.end();
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
  
  // 检查命令行参数
  if (argc != 2) {
      std::cerr << "用法: " << argv[0] << " <目录路径>" << std::endl;
      return 1;
  }
  
  // 获取根路径
  fs::path rootPath = argv[1];
  
  // 检查路径是否存在且是目录
  if (!fs::exists(rootPath) || !fs::is_directory(rootPath)) {
      std::cerr << "错误: " << rootPath << " 不存在或不是一个目录" << std::endl;
      return 1;
  }
  
  // 创建输出XML文件 - 修改为根目录+_merge.xml格式
  fs::path outputFile = rootPath.string() + "_merge.xml";
  std::ofstream xmlFile(outputFile);
  
  if (!xmlFile) {
      std::cerr << "无法创建输出文件: " << outputFile << std::endl;
      return 1;
  }
  
  // 写入XML头
  xmlFile << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
  xmlFile << "<files>\n";
  
  // 递归遍历目录
  try {
      for (const auto& entry : fs::recursive_directory_iterator(rootPath)) {
          if (fs::is_regular_file(entry)) {
              std::string extension = entry.path().extension().string();
              
              // 检查是否是代码文件
              if (isCodeFile(extension)) {
                  // 获取相对路径
                  fs::path relativePath = fs::relative(entry.path(), rootPath);
                  std::string content = readFileContent(entry.path());
                  
                  // 写入XML节点
                  xmlFile << "  <file path=\"" << relativePath.string() << "\">\n";
                  xmlFile << "    <![CDATA[" << content << "]]>\n";
                  xmlFile << "  </file>\n";
              }
          }
      }
  } catch (const fs::filesystem_error& e) {
      std::cerr << "文件系统错误: " << e.what() << std::endl;
      return 1;
  }
  
  // 写入XML尾
  xmlFile << "</files>\n";
  xmlFile.close();
  
  std::cout << "成功生成 " << outputFile << std::endl;
  return 0;
}