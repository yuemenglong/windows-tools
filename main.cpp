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
  HANDLE hProcess = GetCurrentProcess();
  if (!SymInitialize(hProcess, nullptr, FALSE)) {
    std::cerr << "SymInitialize failed. Error: " << GetLastError() << std::endl;
    return false;
  }

  SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEBUG);

  // 加载库文件
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
    std::cerr << "Failed to load library. Error: " << GetLastError() << std::endl;
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
    std::cout << "Usage: " << argv[0] << " <path_to_lib_file>" << std::endl;
    return 1;
  }

  // 将char*转换为wchar_t*
  int wlen = MultiByteToWideChar(CP_UTF8, 0, argv[1], -1, NULL, 0);
  if (wlen == 0) {
    std::cerr << "Failed to get required buffer size. Error: " << GetLastError() << std::endl;
    return 1;
  }

  wchar_t *wlibPath = new wchar_t[wlen];
  if (MultiByteToWideChar(CP_UTF8, 0, argv[1], -1, wlibPath, wlen) == 0) {
    std::cerr << "Failed to convert path to wide string. Error: " << GetLastError() << std::endl;
    delete[] wlibPath;
    return 1;
  }

  std::cout << "Analyzing lib file: " << argv[1] << std::endl;
  std::cout << "Exported functions:" << std::endl;
  std::cout << "-------------------" << std::endl;

  bool success = PrintLibExports(wlibPath);

  delete[] wlibPath;
  std::cout << "\n按任意键退出..." << std::endl;
  _getch();
  return success ? 0 : 1;
}
