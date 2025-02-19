#include <iostream>
#include <windows.h>
#include <dbghelp.h>
#include <string>

#pragma comment(lib, "dbghelp.lib")

// 符号回调函数
BOOL CALLBACK EnumSymProc(PSYMBOL_INFO pSymInfo, ULONG SymbolSize, PVOID UserContext) {
    if (pSymInfo->Flags & SYMFLAG_EXPORT) {
        std::cout << pSymInfo->Name << std::endl;
    }
    return TRUE;
}

void PrintLibExports(const std::wstring& libPath) {
    HANDLE hProcess = GetCurrentProcess();

    // 初始化符号处理器
    if (!SymInitialize(hProcess, nullptr, FALSE)) {
        std::cerr << "SymInitialize failed. Error: " << GetLastError() << std::endl;
        return;
    }

    // 设置符号选项
    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEBUG);

    // 加载库文件
    DWORD64 baseAddr = SymLoadModuleEx(
        hProcess,
        nullptr,
        libPath.c_str(),
        nullptr,
        0,
        0,
        nullptr,
        0
    );

    if (baseAddr == 0) {
        std::cerr << "Failed to load library. Error: " << GetLastError() << std::endl;
        SymCleanup(hProcess);
        return;
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
    }

    // 卸载模块并清理
    SymUnloadModule64(hProcess, baseAddr);
    SymCleanup(hProcess);
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cout << "Usage: " << argv[0] << " <path_to_lib_file>" << std::endl;
        return 1;
    }

    // 将char*转换为wstring
    int wlen = MultiByteToWideChar(CP_UTF8, 0, argv[1], -1, NULL, 0);
    std::wstring wlibPath(wlen, 0);
    MultiByteToWideChar(CP_UTF8, 0, argv[1], -1, &wlibPath[0], wlen);
    
    std::cout << "Analyzing lib file: " << argv[1] << std::endl;
    std::cout << "Exported functions:" << std::endl;
    std::cout << "-------------------" << std::endl;
    
    PrintLibExports(wlibPath);
    
    return 0;
}
