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

  // 读取文件头部来验证文件格式
  const size_t headerSize = 0x40; // 读取更大的头部以检查PE/COFF格式
  unsigned char header[headerSize];
  DWORD bytesRead;
  if (!ReadFile(hFile, header, headerSize, &bytesRead, NULL)) {
    std::cerr << "读取文件头部失败. Error: " << GetLastError() << std::endl;
    CloseHandle(hFile);
    return false;
  }

  std::cout << "文件头部特征: ";
  for (int i = 0; i < min(16, (int) bytesRead); i++) {
    printf("%02X ", header[i]);
  }
  std::cout << std::endl;

  // 检查是否为COFF/PE格式
  if (bytesRead >= 2 && header[0] == 0x4D && header[1] == 0x5A) { // MZ signature
    std::cout << "检测到PE文件格式(MZ头)" << std::endl;
  } else if (bytesRead >= 2 && (header[0] == 0x64 || header[0] == 0x4c) && header[1] == 0x01) {
    std::cout << "检测到COFF对象文件格式" << std::endl;
  } else if (bytesRead >= 8 && memcmp(header, "!<arch>\n", 8) == 0) {
    std::cout << "检测到标准库文件格式(!<arch>)" << std::endl;
  } else {
    std::cout << "未知的文件格式" << std::endl;
  }

  CloseHandle(hFile);

  HANDLE hProcess = GetCurrentProcess();

  // 尝试设置符号搜索路径为当前目录
  wchar_t searchPath[MAX_PATH];
  if (GetCurrentDirectoryW(MAX_PATH, searchPath)) {
    std::wcout << L"设置符号搜索路径: " << searchPath << std::endl;
  }

  if (!SymInitialize(hProcess, (PCSTR) searchPath, FALSE)) {
    DWORD error = GetLastError();
    std::cerr << "SymInitialize失败. 错误代码: " << error << std::endl;

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
    std::cerr << "初始化错误: " << errorMsg << std::endl;
    return false;
  }

  std::cout << "符号处理器初始化成功" << std::endl;

  // 设置符号加载选项
  DWORD newOptions = SYMOPT_DEBUG |           // 启用调试输出
                     SYMOPT_LOAD_ANYTHING |    // 尝试加载任何类型的文件作为符号
                     SYMOPT_UNDNAME |          // 取消修饰C++符号名
                     SYMOPT_AUTO_PUBLICS |     // 自动加载公共符号
                     SYMOPT_INCLUDE_32BIT_MODULES | // 包含32位模块
                     SYMOPT_CASE_INSENSITIVE | // 不区分大小写
                     SYMOPT_ALLOW_ABSOLUTE_SYMBOLS; // 允许绝对符号

  SymSetOptions(newOptions);
  std::cout << "符号加载选项设置为: 0x" << std::hex << newOptions << std::dec << std::endl;

  // 确保搜索路径包含当前目录
  wchar_t symPath[MAX_PATH * 2];
  if (SymGetSearchPathW(hProcess, symPath, MAX_PATH * 2)) {
    std::wcout << L"当前符号搜索路径: " << symPath << std::endl;
  }

  // 加载库文件
  std::wcout << L"正在尝试加载文件: " << libPath << std::endl;

  // 尝试直接加载DLL看是否能获取模块句柄
  HMODULE hModule = LoadLibraryW(libPath);
  if (hModule != NULL) {
    std::cout << "成功加载为DLL，模块句柄: 0x" << std::hex << (DWORD64) hModule << std::dec << std::endl;
    FreeLibrary(hModule);
  } else {
    std::cout << "无法作为DLL加载（这可能是正常的，如果这是一个.lib文件）" << std::endl;
  }
  // 获取映像大小
  DWORD64 moduleSize = fileInfo.nFileSizeLow + (static_cast<DWORD64>(fileInfo.nFileSizeHigh) << 32);

  DWORD64 baseAddr = SymLoadModuleExW(
    hProcess,
    nullptr,
    libPath,
    nullptr,
    0,
    moduleSize,  // 提供实际的文件大小
    nullptr,
    0
  );

  // 尝试刷新模块列表
  if (baseAddr != 0) {
    std::cout << "模块加载地址: 0x" << std::hex << baseAddr << std::dec << std::endl;
    if (!SymRefreshModuleList(hProcess)) {
      std::cout << "警告: 刷新模块列表失败, 错误码: " << GetLastError() << std::endl;
    } else {
      std::cout << "模块列表刷新成功" << std::endl;
    }
  }

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
    IMAGEHLP_MODULE64 moduleInfo = {sizeof(IMAGEHLP_MODULE64)};
    if (SymGetModuleInfo64(hProcess, baseAddr, &moduleInfo)) {
      std::cout << "符号类型: " << moduleInfo.SymType << std::endl;
      std::cout << "加载的映像名称: " << moduleInfo.LoadedImageName << std::endl;
    }

    switch (error) {
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

  // 获取模块详细信息
  IMAGEHLP_MODULE64 modInfo = {sizeof(IMAGEHLP_MODULE64)};
  if (SymGetModuleInfo64(hProcess, baseAddr, &modInfo)) {
    std::cout << "\n模块详细信息:" << std::endl;
    std::cout << "加载基址: 0x" << std::hex << modInfo.BaseOfImage << std::dec << std::endl;
    std::cout << "映像大小: " << modInfo.ImageSize << " 字节" << std::endl;
    std::cout << "时间戳: 0x" << std::hex << modInfo.TimeDateStamp << std::dec << std::endl;
    std::cout << "校验和: 0x" << std::hex << modInfo.CheckSum << std::dec << std::endl;
    std::cout << "符号类型: ";
    switch (modInfo.SymType) {
      case SymNone:
        std::cout << "无符号";
        break;
      case SymCoff:
        std::cout << "COFF格式";
        break;
      case SymCv:
        std::cout << "CodeView格式";
        break;
      case SymPdb:
        std::cout << "PDB格式";
        break;
      case SymExport:
        std::cout << "导出表";
        break;
      case SymDeferred:
        std::cout << "延迟加载";
        break;
      case SymSym:
        std::cout << "SYM格式";
        break;
      default:
        std::cout << "未知(" << modInfo.SymType << ")";
    }
    std::cout << std::endl;
  }

  std::cout << "\n开始枚举导出符号..." << std::endl;

  // 枚举符号
  if (!SymEnumSymbols(
    hProcess,
    baseAddr,
    "*!*",  // 匹配所有符号（包括命名空间/类）
    EnumSymProc,
    nullptr
  )) {
    DWORD error = GetLastError();
    std::cerr << "符号枚举失败. 错误代码: " << error << std::endl;

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
    std::cerr << "错误信息: " << errorMsg << std::endl;

    if (error == ERROR_NOT_FOUND) {
      std::cerr << "未找到任何符号" << std::endl;
    } else if (error == ERROR_INVALID_ADDRESS) {
      std::cerr << "无效的地址范围" << std::endl;
    }

    SymUnloadModule64(hProcess, baseAddr);
    SymCleanup(hProcess);
    return false;
  }

  // 卸载模块并清理
  SymUnloadModule64(hProcess, baseAddr);
  SymCleanup(hProcess);
  return true;
}

// 查找dumpbin.exe的路径
bool FindDumpbinPath(wchar_t *dumpbinPath, size_t pathSize) {
  // 首先检查当前目录
  if (GetCurrentDirectoryW(MAX_PATH, dumpbinPath)) {
    wcscat_s(dumpbinPath, pathSize, L"\\dumpbin.exe");
    if (GetFileAttributesW(dumpbinPath) != INVALID_FILE_ATTRIBUTES) {
      return true;
    }
  }

  // 然后检查常见的Visual Studio安装路径
  const wchar_t *vsLocations[] = {
    L"C:\\Program Files\\Microsoft Visual Studio\\2022\\Professional\\VC\\Tools\\MSVC",
    L"C:\\Program Files\\Microsoft Visual Studio\\2022\\Enterprise\\VC\\Tools\\MSVC",
    L"C:\\Program Files\\Microsoft Visual Studio\\2022\\Community\\VC\\Tools\\MSVC",
    L"C:\\Program Files (x86)\\Microsoft Visual Studio\\2022\\Professional\\VC\\Tools\\MSVC",
    L"C:\\Program Files (x86)\\Microsoft Visual Studio\\2022\\Enterprise\\VC\\Tools\\MSVC",
    L"C:\\Program Files (x86)\\Microsoft Visual Studio\\2022\\Community\\VC\\Tools\\MSVC"
  };

  WIN32_FIND_DATAW findData;
  for (const wchar_t *vsLocation: vsLocations) {
    wchar_t searchPath[MAX_PATH];
    wcscpy_s(searchPath, vsLocation);
    wcscat_s(searchPath, L"\\*");

    HANDLE hFind = FindFirstFileW(searchPath, &findData);
    if (hFind != INVALID_HANDLE_VALUE) {
      do {
        if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
          if (wcscmp(findData.cFileName, L".") != 0 && wcscmp(findData.cFileName, L"..") != 0) {
            // 构造完整的dumpbin.exe路径
            swprintf(dumpbinPath, pathSize, L"%s\\%s\\bin\\Hostx64\\x64\\dumpbin.exe",
                     vsLocation, findData.cFileName);
            if (GetFileAttributesW(dumpbinPath) != INVALID_FILE_ATTRIBUTES) {
              FindClose(hFind);
              return true;
            }
          }
        }
      } while (FindNextFileW(hFind, &findData));
      FindClose(hFind);
    }
  }

  return false;
}

// 使用dumpbin处理.lib文件
bool ProcessLibFile(const wchar_t *libPath) {
  // 查找dumpbin.exe
  wchar_t dumpbinPath[MAX_PATH];
  if (!FindDumpbinPath(dumpbinPath, MAX_PATH)) {
    std::cerr << "错误: 无法找到dumpbin.exe。请确保Visual Studio已正确安装。" << std::endl;
    std::cerr << "请在以下位置检查dumpbin.exe是否存在：" << std::endl;
    std::cerr << "1. 当前目录" << std::endl;
    std::cerr << "2. Visual Studio 2022的安装目录下的MSVC工具链目录" << std::endl;
    return false;
  }

  // 构建dumpbin命令
  wchar_t cmdLine[MAX_PATH * 3];
  swprintf(cmdLine, MAX_PATH * 3, L"\"%s\" /exports \"%s\"", dumpbinPath, libPath);

  std::wcout << L"执行命令: " << cmdLine << std::endl;

  // 创建管道以捕获输出
  SECURITY_ATTRIBUTES sa;
  sa.nLength = sizeof(SECURITY_ATTRIBUTES);
  sa.bInheritHandle = TRUE;
  sa.lpSecurityDescriptor = NULL;

  HANDLE hReadPipe, hWritePipe;
  if (!CreatePipe(&hReadPipe, &hWritePipe, &sa, 0)) {
    std::cerr << "创建管道失败. 错误码: " << GetLastError() << std::endl;
    return false;
  }

  // 设置进程启动信息
  STARTUPINFOW si = {sizeof(STARTUPINFOW)};
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdOutput = hWritePipe;
  si.hStdError = hWritePipe;
  si.hStdInput = NULL;

  PROCESS_INFORMATION pi;
  bool success = CreateProcessW(
    NULL,
    cmdLine,
    NULL,
    NULL,
    TRUE,
    CREATE_NO_WINDOW,
    NULL,
    NULL,
    &si,
    &pi
  );

  if (!success) {
    std::cerr << "运行dumpbin失败. 错误码: " << GetLastError() << std::endl;
    CloseHandle(hReadPipe);
    CloseHandle(hWritePipe);
    return false;
  }

  // 关闭写入端，防止死锁
  CloseHandle(hWritePipe);

  // 读取输出
  char buffer[4096];
  DWORD bytesRead;
  std::string output;

  while (ReadFile(hReadPipe, buffer, sizeof(buffer) - 1, &bytesRead, NULL) && bytesRead > 0) {
    buffer[bytesRead] = '\0';
    output += buffer;
  }

  // 等待进程完成
  WaitForSingleObject(pi.hProcess, INFINITE);

  // 清理
  CloseHandle(pi.hProcess);
  CloseHandle(pi.hThread);
  CloseHandle(hReadPipe);

  // 输出结果
  std::cout << output;
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

  bool success;
  if (PathMatchSpecW(wlibPath, L"*.lib")) {
    // 处理.lib文件
    success = ProcessLibFile(wlibPath);
  } else {
    // 处理.dll文件
    success = PrintLibExports(wlibPath);
  }

  delete[] wlibPath;
  std::cout << "\n按任意键退出..." << std::endl;
  _getch();
  return success ? 0 : 1;
}
