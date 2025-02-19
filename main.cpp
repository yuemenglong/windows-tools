#include <iostream>
#include <windows.h>
#include <dbghelp.h>
#include <conio.h>

#pragma comment(lib, "dbghelp.lib")

BOOL CALLBACK EnumSymProc(PSYMBOL_INFO pSymInfo, ULONG SymbolSize, PVOID UserContext) {
  if (pSymInfo->Flags & SYMFLAG_EXPORT) {
    std::cout << pSymInfo->Name << std::endl;
  }
  return TRUE;
}
bool PrintLibExports(const wchar_t *libPath) {
  // 检查文件是否存在
  DWORD fileAttr = GetFileAttributesW(libPath);
  if (fileAttr == INVALID_FILE_ATTRIBUTES) {
    std::cerr << "文件不存在或无法访问. Error: " << GetLastError() << std::endl;
    return false;
  }

  HANDLE hProcess = GetCurrentProcess();
  if (!SymInitialize(hProcess, nullptr, FALSE)) {
    std::cerr << "SymInitialize failed. Error: " << GetLastError() << std::endl;
    return false;
  }

  SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEBUG);

  // 加载库文件
  std::wcout << L"正在尝试加载文件: " << libPath << std::endl;
  DWORD64 baseAddr = SymLoadModuleExW(
    hProcess,
    nullptr,
    libPath,
    nullptr,
    0,
    0,
    nullptr,
    0
  );

  if (baseAddr == 0) {
    DWORD error = GetLastError();
    std::cerr << "加载库文件失败. Error code: " << error << std::endl;
    switch(error) {
      case ERROR_FILE_NOT_FOUND:
        std::cerr << "文件未找到" << std::endl;
        break;
      case ERROR_ACCESS_DENIED:
        std::cerr << "访问被拒绝" << std::endl;
        break;
      case ERROR_BAD_EXE_FORMAT:
        std::cerr << "不是有效的库文件格式" << std::endl;
        break;
      default:
        std::cerr << "未知错误" << std::endl;
    }
    SymCleanup(hProcess);
    return false;
  }

  // 枚举符号
  if (!SymEnumSymbols(
    hProcess,
    baseAddr,
    "*",  // 匹配所有符号
    EnumSymProc,
    nullptr
  )) {
    std::cerr << "SymEnumSymbols failed. Error: " << GetLastError() << std::endl;
    SymUnloadModule64(hProcess, baseAddr);
    SymCleanup(hProcess);
    return false;
  }

  // 卸载模块并清理
  SymUnloadModule64(hProcess, baseAddr);
  SymCleanup(hProcess);
  return true;
}

int main(int argc, char *argv[]) {
  if (argc != 2) {
    std::cout << "用法: " << argv[0] << " <库文件路径>" << std::endl;
    return 1;
  }

  // 将char*转换为wchar_t*
  int wlen = MultiByteToWideChar(CP_UTF8, 0, argv[1], -1, NULL, 0);
  if (wlen == 0) {
    std::cerr << "获取所需缓冲区大小失败. 错误代码: " << GetLastError() << std::endl;
    return 1;
  }

  wchar_t *wlibPath = new wchar_t[wlen];
  if (MultiByteToWideChar(CP_UTF8, 0, argv[1], -1, wlibPath, wlen) == 0) {
    std::cerr << "转换文件路径到宽字符串失败. 错误代码: " << GetLastError() << std::endl;
    delete[] wlibPath;
    return 1;
  }

  std::cout << "正在分析库文件: " << argv[1] << std::endl;
  std::wcout << L"完整路径: " << wlibPath << std::endl;
  std::cout << "导出的函数列表:" << std::endl;
  std::cout << "-------------------" << std::endl;

  bool success = PrintLibExports(wlibPath);

  delete[] wlibPath;
  std::cout << "\n按任意键退出..." << std::endl;
  _getch();
  return success ? 0 : 1;
}
