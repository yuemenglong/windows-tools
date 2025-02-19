#include <windows.h>
#include <dbghelp.h>
#include <psapi.h>
#include <Shlwapi.h>
#include <iostream>
#include <conio.h>
#include <cstring>
#include <algorithm>

#pragma comment(lib, "dbghelp.lib")
#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "Shlwapi.lib")

BOOL CALLBACK EnumSymProc(PSYMBOL_INFO pSymInfo, ULONG SymbolSize, PVOID UserContext) {
  if (pSymInfo->Flags & SYMFLAG_EXPORT) {
    std::cout << pSymInfo->Name << std::endl;
  }
  return TRUE;
}

bool PrintLibExports(const wchar_t *libPath) {
  // 检查文件是否存在
  WIN32_FILE_ATTRIBUTE_DATA fileInfo;
  if (!GetFileAttributesExW(libPath, GetFileExInfoStandard, &fileInfo)) {
    std::cerr << "文件不存在或无法访问. Error: " << GetLastError() << std::endl;
    return false;
  }

  // 尝试打开文件
  HANDLE hFile = CreateFileW(libPath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
  if (hFile == INVALID_HANDLE_VALUE) {
    std::cerr << "无法打开文件. Error: " << GetLastError() << std::endl;
    return false;
  }

  // 读取文件头
  char fileHeader[8];
  DWORD bytesRead;
  if (!ReadFile(hFile, fileHeader, sizeof(fileHeader), &bytesRead, NULL) || bytesRead != sizeof(fileHeader)) {
    std::cerr << "读取文件头失败. Error: " << GetLastError() << std::endl;
    CloseHandle(hFile);
    return false;
  }

  // 检查是否为 .lib 文件
  if (memcmp(fileHeader, "!<arch>\n", 8) != 0) {
    std::cerr << "不是有效的 .lib 文件。" << std::endl;
    CloseHandle(hFile);
    return false;
  }

  std::cout << "导出的函数列表:" << std::endl;
  std::cout << "-------------------" << std::endl;

  // 循环读取 .lib 文件的每个成员
  while (true) {
    IMAGE_ARCHIVE_MEMBER_HEADER memberHeader;
    if (!ReadFile(hFile, &memberHeader, sizeof(memberHeader), &bytesRead, NULL) || bytesRead != sizeof(memberHeader)) {
      break; // 文件结束或读取错误
    }

    // 检查成员头部的结束标记
    if (memcmp(memberHeader.EndHeader, "`\n", 2) != 0) {
      std::cerr << "无效的成员头部。" << std::endl;
      break;
    }

    // 提取成员名
    char memberName[17] = {0};
    memcpy(memberName, memberHeader.Name, 16);

    // 提取成员大小
    char sizeStr[11] = {0};
    memcpy(sizeStr, memberHeader.Size, 10);
    long memberSize = atol(sizeStr);

    // 如果是第一个链接器成员，跳过
    if (strcmp(memberName, "/               ") == 0) {
      SetFilePointer(hFile, memberSize, NULL, FILE_CURRENT);
      continue;
    }

    // 如果是第二个链接器成员,则包含导出符号
    if (strcmp(memberName, "/               ") == 0) {
      //跳过
      SetFilePointer(hFile, memberSize, NULL, FILE_CURRENT);
      continue;
    }

    // 如果是长文件名成员，跳过
    if (strcmp(memberName, "//              ") == 0) {
      SetFilePointer(hFile, memberSize, NULL, FILE_CURRENT);
      continue;
    }

    // 假设这是一个 COFF 对象文件，尝试解析
    std::cout << "成员: " << memberName << std::endl;

    // 读取 COFF 文件头
    IMAGE_FILE_HEADER coffHeader;
    SetFilePointer(hFile, -(LONG) sizeof(memberHeader), NULL, FILE_CURRENT); // 回到成员头部

    //先读成员头，再读COFF头
    DWORD dwPos = SetFilePointer(hFile, sizeof(IMAGE_ARCHIVE_MEMBER_HEADER), NULL, FILE_CURRENT);
    if (!ReadFile(hFile, &coffHeader, sizeof(coffHeader), &bytesRead, NULL) || bytesRead != sizeof(coffHeader)) {
      std::cerr << "读取 COFF 文件头失败. Error: " << GetLastError() << std::endl;
      SetFilePointer(hFile, memberSize, NULL, FILE_CURRENT); // 跳过成员内容
      continue;
    }

    // 检查机器类型
    if (coffHeader.Machine != IMAGE_FILE_MACHINE_AMD64 && coffHeader.Machine != IMAGE_FILE_MACHINE_I386) {
      std::cerr << "不支持的机器类型: 0x" << std::hex << coffHeader.Machine << std::dec << std::endl;
      SetFilePointer(hFile, memberSize, NULL, FILE_CURRENT); // 跳过成员内容
      continue;
    }

    // 定位符号表
    DWORD symbolOffset = coffHeader.PointerToSymbolTable;
    DWORD numberOfSymbols = coffHeader.NumberOfSymbols;

    if (symbolOffset == 0 || numberOfSymbols == 0) {
      std::cout << "  没有符号表。" << std::endl;
      SetFilePointer(hFile, memberSize, NULL, FILE_CURRENT); // 跳过成员内容
      continue;
    }

    // 读取符号表
    std::cout << "  符号表偏移: 0x" << std::hex << symbolOffset << std::dec << std::endl;
    std::cout << "  符号数量: " << numberOfSymbols << std::endl;

    SetFilePointer(hFile, dwPos + symbolOffset, NULL, FILE_BEGIN); //回溯到文件开始位置，加上偏移

    for (DWORD i = 0; i < numberOfSymbols; ++i) {
      IMAGE_SYMBOL symbol;
      if (!ReadFile(hFile, &symbol, sizeof(symbol), &bytesRead, NULL) || bytesRead != sizeof(symbol)) {
        std::cerr << "读取符号失败. Error: " << GetLastError() << std::endl;
        break;
      }

      // 检查是否为导出符号
      if (symbol.StorageClass == IMAGE_SYM_CLASS_EXTERNAL) {
        char symbolName[9] = {0}; // 符号名最多 8 字节 + 1 字节 '\0'
        if (symbol.N.Name.Short != 0) { //E1E0
          // 短名称
          memcpy(symbolName, symbol.N.ShortName, 8);
        } else {
          // 从字符串表中读取长名称
          DWORD stringOffset = symbol.N.Name.Long;

          // 检查字符串表是否存在及偏移是否有效
          if (coffHeader.PointerToSymbolTable == 0 || numberOfSymbols == 0) {
            std::cerr << "  没有符号表或字符串表。" << std::endl;
            break;
          }
          DWORD stringTableOffset = dwPos + coffHeader.PointerToSymbolTable + (numberOfSymbols * sizeof(IMAGE_SYMBOL));

          if (stringTableOffset + stringOffset >= fileInfo.nFileSizeLow + ((static_cast<ULONGLONG>(fileInfo.nFileSizeHigh) << 32))) {
            std::cerr << "字符串表偏移越界" << std::endl;
            break;
          }

          if (SetFilePointer(hFile, stringTableOffset + stringOffset, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER) {
            std::cerr << "跳转读取字符串表失败. Error: " << GetLastError() << std::endl;
            break;
          }

          // 循环读取字符串，直到遇到空字符
          int i = 0;
          char c;
          do {
            if (!ReadFile(hFile, &c, 1, &bytesRead, NULL) || bytesRead == 0) {
              std::cerr << "读取字符串表失败. Error: " << GetLastError() << std::endl;
              break;
            }
            if (i < sizeof(symbolName) - 1) {
              symbolName[i++] = c;
            }
          } while (c != '\0');
          symbolName[i - 1] = '\0'; // 确保以空字符结尾
        }
        std::cout << "  导出符号: " << symbolName << std::endl;
      }
    }

    SetFilePointer(hFile, dwPos + memberSize, NULL, FILE_BEGIN); // 跳过成员内容,回到文件起始位置
  }

  CloseHandle(hFile);
  return true;
}

#include <sstream>

int main(int argc, char *argv[]) {
  if (argc != 2) {
    std::cout << "用法: " << argv[0] << " <库文件路径>" << std::endl;
    return 1;
  }

  // 构造 dumpbin 命令
  std::stringstream command;
  command << "dumpbin /exports \"" << argv[1] << "\"";

  // 执行命令并获取输出
  FILE *pipe = _popen(command.str().c_str(), "r");
  if (!pipe) {
    std::cerr << "无法执行 dumpbin 命令。" << std::endl;
    return 1;
  }

  std::cout << "正在分析库文件: " << argv[1] << std::endl;
  std::cout << "导出的函数列表:" << std::endl;
  std::cout << "-------------------" << std::endl;

  char buffer[128];
  bool foundExports = false;
  while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
    // 查找导出函数
    if (strstr(buffer, "ordinal hint") != nullptr) {
      foundExports = true;
      continue; //跳过"ordinal hint"这一行
    }
    if (foundExports) {
      // 提取函数名，通常在多个空格之后
      char *name = strchr(buffer, ' ');
      if (name) {
        while (*name == ' ') name++; //跳过空格
        if (*name) {
          //移除换行符
          char *end = strchr(name, '\r');
          if (end) *end = 0;
          end = strchr(name, '\n');
          if (end) *end = 0;

          std::cout << name << std::endl;
        }
      }
    }
  }
  int exitCode = _pclose(pipe);
  if (exitCode != 0 && !foundExports) {
    std::cerr << "dumpbin 命令执行失败或未找到导出函数" << std::endl;
  }


  std::cout << "\n按任意键退出..." << std::endl;
  _getch();
  return 0;
}
