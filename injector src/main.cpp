#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <iostream>
#include <vector>
#include <map>
#include <string>
#include <shlwapi.h>
#include <chrono>
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <sstream>
#include <iomanip>
#include <TlHelp32.h>
#include <Psapi.h>
#include <AclAPI.h>
#include <shlobj.h>
#include <shlwapi.h>

#pragma comment(lib, "Shlwapi.lib")
#pragma comment(lib, "Advapi32.lib")

// Constants
const int INJECTION_RETRY_DELAY_MS = 1000;
const int MAX_INJECTION_ATTEMPTS = 3;
const DWORD PROCESS_ACCESS_RIGHTS = PROCESS_CREATE_THREAD | 
                                   PROCESS_QUERY_INFORMATION | 
                                   PROCESS_VM_OPERATION | 
                                   PROCESS_VM_WRITE | 
                                   PROCESS_VM_READ;

bool EnableDebugPrivilege();

const std::vector<std::wstring> TARGET_NAMES = {
    L"discord.exe"
    L"Lightcord.exe"
    
};

struct InjectionContext {
    DWORD pid;
    std::string dllPath;
    int attempts;
    DWORD lastAttemptTime;
};

std::map<DWORD, InjectionContext> g_injectionContexts;

std::vector<DWORD> GetTargetProcessIds() {
    std::vector<DWORD> pids;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    
    if (snapshot == INVALID_HANDLE_VALUE) {
        return pids;
    }

    PROCESSENTRY32W entry = { 0 };
    entry.dwSize = sizeof(PROCESSENTRY32W);
    
    if (!Process32FirstW(snapshot, &entry)) {
        CloseHandle(snapshot);
        return pids;
    }

    do {
        for (const auto& name : TARGET_NAMES) {
            if (_wcsicmp(entry.szExeFile, name.c_str()) == 0) {
                pids.push_back(entry.th32ProcessID);
                break; // No need to check other names if this one matches
            }
        }
    } while (Process32NextW(snapshot, &entry));

    CloseHandle(snapshot);
    return pids;
}

bool IsProcessRunning(DWORD pid) {
    HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, pid);
    if (!process) return false;
    
    DWORD exitCode = 0;
    if (GetExitCodeProcess(process, &exitCode) && exitCode != STILL_ACTIVE) {
        CloseHandle(process);
        return false;
    }
    
    CloseHandle(process);
    return true;
}

bool InjectDLL(DWORD pid, const std::string& dllPath) {
    if (!IsProcessRunning(pid)) {
        return false;
    }

    HANDLE hProcess = OpenProcess(PROCESS_ACCESS_RIGHTS, FALSE, pid);
    if (!hProcess) {
        return false;
    }

    // Allocate memory for the DLL path
    size_t pathSize = dllPath.size() + 1;
    LPVOID remotePath = VirtualAllocEx(hProcess, NULL, pathSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remotePath) {
        CloseHandle(hProcess);
        return false;
    }

    // Write the DLL path to the allocated memory
    if (!WriteProcessMemory(hProcess, remotePath, dllPath.c_str(), pathSize, NULL)) {
        VirtualFreeEx(hProcess, remotePath, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return false;
    }

    // Get the address of LoadLibraryA in kernel32.dll
    HMODULE hKernel32 = GetModuleHandle(L"kernel32.dll");
    if (!hKernel32) {
        VirtualFreeEx(hProcess, remotePath, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return false;
    }

    FARPROC loadLibraryAddr = GetProcAddress(hKernel32, "LoadLibraryA");
    if (!loadLibraryAddr) {
        VirtualFreeEx(hProcess, remotePath, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return false;
    }

    // Create a remote thread to call LoadLibraryA with the DLL path
    HANDLE hThread = CreateRemoteThread(hProcess, NULL, 0, 
        (LPTHREAD_START_ROUTINE)loadLibraryAddr, remotePath, 0, NULL);
    
    if (!hThread) {
        VirtualFreeEx(hProcess, remotePath, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return false;
    }

    // Wait for the thread to complete
    WaitForSingleObject(hThread, 10000); // 10 second timeout

    // Clean up
    DWORD exitCode = 0;
    if (!GetExitCodeThread(hThread, &exitCode) || exitCode == 0) {
        CloseHandle(hThread);
        VirtualFreeEx(hProcess, remotePath, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return false;
    }

    CloseHandle(hThread);
    VirtualFreeEx(hProcess, remotePath, 0, MEM_RELEASE);
    CloseHandle(hProcess);
    return true;
}

std::string FindDLLByName(const std::string& dir, const std::string& dllName) {
    if (dir.empty() || dllName.empty()) {
        return "";
    }

    // Construct the full path
    std::string fullPath = dir;
    if (fullPath.back() != '\\' && fullPath.back() != '/') {
        fullPath += '\\';
    }
    fullPath += dllName;

    // Verify the file exists and is accessible
    DWORD attrs = GetFileAttributesA(fullPath.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_DIRECTORY)) {
        return "";
    }

    // Verify it's actually a DLL
    if (fullPath.size() <= 4 || 
        _stricmp(fullPath.c_str() + fullPath.size() - 4, ".dll") != 0) {
        return "";
    }

    return fullPath;
}

void HideConsole() {
    #ifdef _DEBUG
    // Keep console visible in debug builds
    return;
    #else
    HWND hwnd = GetConsoleWindow();
    if (hwnd != NULL) {
        ShowWindow(hwnd, SW_HIDE);
    }
    #endif
}
bool EnableDebugPrivilege() {
    HANDLE hToken;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) {
        return false;
    }

    TOKEN_PRIVILEGES tp;
    LUID luid;

    if (!LookupPrivilegeValue(NULL, SE_DEBUG_NAME, &luid)) {
        CloseHandle(hToken);
        return false;
    }

    tp.PrivilegeCount = 1;
    tp.Privileges[0].Luid = luid;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

    if (!AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(TOKEN_PRIVILEGES), NULL, NULL)) {
        CloseHandle(hToken);
        return false;
    }

    CloseHandle(hToken);
    return true;
}

bool DirectoryExists(const std::wstring& path) {
    DWORD dwAttrib = GetFileAttributesW(path.c_str());
    return (dwAttrib != INVALID_FILE_ATTRIBUTES && (dwAttrib & FILE_ATTRIBUTE_DIRECTORY));
}

bool FileExists(const std::wstring& path) {
    DWORD dwAttrib = GetFileAttributesW(path.c_str());
    return (dwAttrib != INVALID_FILE_ATTRIBUTES && !(dwAttrib & FILE_ATTRIBUTE_DIRECTORY));
}

bool CopyFileIfExists(const std::wstring& sourcePath, const std::wstring& targetPath) {
    // Check if source file exists
    DWORD sourceAttrs = GetFileAttributesW(sourcePath.c_str());
    if (sourceAttrs == INVALID_FILE_ATTRIBUTES) {
        DWORD error = GetLastError();
        wchar_t errorMsg[256];
        FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM, NULL, error, 0, errorMsg, 256, NULL);
        OutputDebugStringW((L"File not found: " + sourcePath + L" - " + errorMsg + L"\n").c_str());
        return false;
    }
    
    // Create target directory if it doesn't exist
    std::wstring targetDir = targetPath.substr(0, targetPath.find_last_of(L"\\"));
    if (!CreateDirectoryW(targetDir.c_str(), NULL)) {
        DWORD error = GetLastError();
        if (error != ERROR_ALREADY_EXISTS) {
            wchar_t errorMsg[256];
            FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM, NULL, error, 0, errorMsg, 256, NULL);
            OutputDebugStringW((L"Failed to create directory: " + targetDir + L" - " + errorMsg + L"\n").c_str());
            return false;
        }
    }
    
    // Copy the file, overwrite if exists
    if (!CopyFileW(sourcePath.c_str(), targetPath.c_str(), FALSE)) {
        DWORD error = GetLastError();
        wchar_t errorMsg[256];
        FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM, NULL, error, 0, errorMsg, 256, NULL);
        OutputDebugStringW((L"Failed to copy " + sourcePath + L" to " + targetPath + L" - " + errorMsg + L"\n").c_str());
        return false;
    }
    
    OutputDebugStringW((L"Successfully copied " + sourcePath + L" to " + targetPath + L"\n").c_str());
    return true;
}

bool TerminateTargetProcesses() {
    bool terminated = false;
    
    for (const auto& targetName : TARGET_NAMES) {
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot == INVALID_HANDLE_VALUE) {
            continue;
        }
        
        PROCESSENTRY32W pe;
        pe.dwSize = sizeof(PROCESSENTRY32W);
        
        if (Process32FirstW(snapshot, &pe)) {
            do {
                if (_wcsicmp(pe.szExeFile, targetName.c_str()) == 0) {
                    HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, pe.th32ProcessID);
                    if (hProcess != NULL) {
                        if (TerminateProcess(hProcess, 0)) {
                            OutputDebugStringW((L"Terminated process: " + targetName + L" (PID: " + std::to_wstring(pe.th32ProcessID) + L")\n").c_str());
                            terminated = true;
                        } else {
                            DWORD error = GetLastError();
                            wchar_t errorMsg[256];
                            FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM, NULL, error, 0, errorMsg, 256, NULL);
                            OutputDebugStringW((L"Failed to terminate process: " + targetName + L" - " + errorMsg + L"\n").c_str());
                        }
                        CloseHandle(hProcess);
                    }
                }
            } while (Process32NextW(snapshot, &pe));
        }
        
        CloseHandle(snapshot);
    }
    
    // Give some time for processes to terminate
    if (terminated) {
        Sleep(1000);
    }
    
    return terminated;
}

int main() {
    // Enable debug privilege for process access
    EnableDebugPrivilege();
    
    // Try to replace Discord voice node from Modules folder if it exists
    // ReplaceDiscordVoiceNode();
    
    // Hide console window if not in debug mode
    #ifndef _DEBUG
    HideConsole();
    #endif

    // Get DLL path
    const std::string dllName = "Hook Sal Bel.dll";
    char exeDir[MAX_PATH] = { 0 };
    if (!GetModuleFileNameA(NULL, exeDir, MAX_PATH)) {
        return 1;
    }
    
    PathRemoveFileSpecA(exeDir);
    std::string dllPath = FindDLLByName(exeDir, dllName);
    
    if (dllPath.empty()) {
        return 1;
    }

    // Main loop
    while (true) {
        try {
            // Get current target processes
            std::vector<DWORD> currentPIDs = GetTargetProcessIds();
            
            // Remove dead processes from tracking
            for (auto it = g_injectionContexts.begin(); it != g_injectionContexts.end();) {
                if (!IsProcessRunning(it->first)) {
                    it = g_injectionContexts.erase(it);
                } else {
                    ++it;
                }
            }

            // Process each target process
            for (DWORD pid : currentPIDs) {
                auto& context = g_injectionContexts[pid];
                
                // Initialize context if new
                if (context.pid == 0) {
                    context.pid = pid;
                    context.dllPath = dllPath;
                    context.attempts = 0;
                    context.lastAttemptTime = 0;
                }

                // Check if we need to attempt injection
                DWORD currentTime = GetTickCount();
                if (context.attempts < MAX_INJECTION_ATTEMPTS && 
                    (currentTime - context.lastAttemptTime) > INJECTION_RETRY_DELAY_MS) {
                    
                    if (InjectDLL(pid, dllPath)) {
                        context.attempts = MAX_INJECTION_ATTEMPTS; // Mark as succeeded
                    } else {
                        context.attempts++;
                        context.lastAttemptTime = currentTime;
                    }
                }
            }
            Sleep(10); // Check every second
            
        } catch (...) {
            Sleep(10); // Wait longer on error
        }
    }
    
    return 0;
}

int APIENTRY WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    main();
    return 0;
}