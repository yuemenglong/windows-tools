#include <iostream>
#include <windows.h>
#include <dbghelp.h>
#include <conio.h>
#include <cstring>

#pragma comment(lib, "dbghelp.lib")

BOOL CALLBACK EnumSymProc(PSYMBOL_INFO pSymInfo, ULONG SymbolSize, PVOID UserContext) {
  if (pSymInfo->Flags & SYMFLAG_EXPORT) {
    std::cout << pSymInfo->Name << std::endl;
  }
  return TRUE;
}
bool PrintLibExports(const wchar_t *libPath) {
  // 检查文件是否存在并获取文件信息
  WIN32_FILE_ATTRIBUTE_DATA fileInfo;
  if (!GetFileAttributesExW(libPath, GetFileExInfoStandard, &fileInfo)) {
    std::cerr << "文件不存在或无法访问. Error: " << GetLastError() << std::endl;
    return false;
  }

  // 显示文件信息
  std::wcout << L"文件大小: " <<
    (static_cast<ULONGLONG>(fileInfo.nFileSizeHigh) << 32) + fileInfo.nFileSizeLow <<
    L" 字节" << std::endl;
  
  // 尝试以二进制方式打开文件检查
  HANDLE hFile = CreateFileW(
    libPath,
    GENERIC_READ,
    FILE_SHARE_READ,
    NULL,
    OPEN_EXISTING,
    FILE_ATTRIBUTE_NORMAL,
    NULL
  );
  
  if (hFile == INVALID_HANDLE_VALUE) {
    std::cerr << "无法打开文件进行读取. Error: " << GetLastError() << std::endl;
    return false;
  }
  
  // 读取文件头部来验证是否为有效的库文件
  char header[4];
  DWORD bytesRead;
  if (!ReadFile(hFile, header, sizeof(header), &bytesRead, NULL)) {
    std::cerr << "读取文件头部失败. Error: " << GetLastError() << std::endl;
    CloseHandle(hFile);
    return false;
  }
  
  CloseHandle(hFile);
  
  // 检查文件头部是否为有效的库文件标识（通常以!<arch>开头）
  if (bytesRead == 4 && memcmp(header, "!<ar", 4) != 0) {
    std::cerr << "警告: 文件可能不是有效的库文件格式" << std::endl;
  }

  HANDLE hProcess = GetCurrentProcess();
  if (!SymInitialize(hProcess, nullptr, FALSE)) {
    std::cerr << "SymInitialize failed. Error: " << GetLastError() << std::endl;
    return false;
  }

  // 设置所有可能的调试选项
  DWORD oldOptions = SymGetOptions();
  DWORD newOptions = SYMOPT_DEBUG | SYMOPT_UNDNAME | SYMOPT_VERBOSE |
                    SYMOPT_LOAD_LINES | SYMOPT_OMAP_FIND_NEAREST |
                    SYMOPT_DEFERRED_LOADS | SYMOPT_DEBUG_END;
  SymSetOptions(newOptions);

  std::cout << "符号加载选项设置为: 0x" << std::hex << SymGetOptions() << std::dec << std::endl;

  // 加载库文件
  std::wcout << L"正在尝试加载文件: " << libPath << std::endl;

  // 尝试直接加载DLL看是否能获取模块句柄
  HMODULE hModule = LoadLibraryW(libPath);
  if (hModule != NULL) {
      std::cout << "成功加载为DLL，模块句柄: 0x" << std::hex << (DWORD64)hModule << std::dec << std::endl;
      FreeLibrary(hModule);
  } else {
      std::cout << "无法作为DLL加载（这可能是正常的，如果这是一个.lib文件）" << std::endl;
  }
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
    
    // 获取更详细的错误信息
    char errorMsg[256];
    FormatMessageA(
      FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
      NULL,
      error,
      MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
      errorMsg,
      sizeof(errorMsg),
      NULL
    );
    std::cerr << "系统错误信息: " << errorMsg << std::endl;
    
    // 检查文件访问权限
    DWORD attributes = GetFileAttributesW(libPath);
    if (attributes != INVALID_FILE_ATTRIBUTES) {
      std::cout << "文件属性: 0x" << std::hex << attributes << std::dec << std::endl;
      if (attributes & FILE_ATTRIBUTE_READONLY)
        std::cout << "文件为只读" << std::endl;
      if (attributes & FILE_ATTRIBUTE_SYSTEM)
        std::cout << "文件为系统文件" << std::endl;
    }

    // 尝试获取更多符号加载信息
    IMAGEHLP_MODULE64 moduleInfo = { sizeof(IMAGEHLP_MODULE64) };
    if (SymGetModuleInfo64(hProcess, baseAddr, &moduleInfo)) {
      std::cout << "符号类型: " << moduleInfo.SymType << std::endl;
      std::cout << "加载的映像名称: " << moduleInfo.LoadedImageName << std::endl;
    }

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
      case ERROR_INVALID_HANDLE:
        std::cerr << "无效的句柄" << std::endl;
        break;
      case ERROR_NOT_ENOUGH_MEMORY:
        std::cerr << "内存不足" << std::endl;
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
