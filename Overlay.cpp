#include "Interface/Overlay.h"
#include "Configurations/Globals.h"
#include "opus.h"

#define NOMINMAX
#include "Windows.h"
#include <iostream>
#include <fstream>
#include <WinUser.h>
#include <windows.h>
#include <TlHelp32.h>
#include <psapi.h>
#include <chrono>
#include <cmath>
#include <vector>
#include <algorithm>
#include <stdio.h>
#include <string>
#include <commdlg.h>
#include <shellapi.h>

struct Color {
    float r, g, b, a;
    static Color lerp(const Color& a, const Color& b, float t) {
        return { a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t };
    }
    ImVec4 toImVec4() const { return ImVec4(r, g, b, a); }
};

Color HSVtoRGB(float h, float s, float v, float a = 1.0f) {
    Color rgb = { 0, 0, 0, a };
    if (s <= 0.0f) { rgb.r = v; rgb.g = v; rgb.b = v; return rgb; }
    h = std::fmod(h, 360.0f) / 60.0f;
    int i = (int)h;
    float f = h - i;
    float p = v * (1.0f - s);
    float q = v * (1.0f - s * f);
    float t = v * (1.0f - s * (1.0f - f));
    switch (i) {
    case 0: rgb = { v, t, p, a }; break;
    case 1: rgb = { q, v, p, a }; break;
    case 2: rgb = { p, v, t, a }; break;
    case 3: rgb = { p, q, v, a }; break;
    case 4: rgb = { t, p, v, a }; break;
    case 5: rgb = { v, p, q, a }; break;
    }
    return rgb;
}

float calculate_eq_response(float freq, float lowCutoff, float highCutoff, float lowsGain, float midsGain, float highsGain, int sampleRate) {
    float lowBandResponse = lowsGain;
    if (freq > lowCutoff) lowBandResponse *= lowCutoff / freq;
    float highBandResponse = highsGain;
    if (freq < highCutoff) highBandResponse *= freq / highCutoff;
    float midBandResponse = midsGain;
    if (freq < lowCutoff) midBandResponse *= freq / lowCutoff;
    if (freq > highCutoff) midBandResponse *= highCutoff / freq;
    return lowBandResponse + midBandResponse + highBandResponse;
}

void DrawCoolEQVisualization(float lowsGain, float midsGain, float highsGain) {
    static float animTime = 0.0f;
    animTime += ImGui::GetIO().DeltaTime * 0.5f;
    const float lowCutoff = 1.0f;
    const float highCutoff = 30000.0f;
    const int sampleRate = 58000;
    const int numPoints = 250;
    const float minFreq = 20.0f;
    const float maxFreq = 25000.0f;
    ImVec2 windowPos = ImGui::GetCursorScreenPos();
    ImVec2 canvasSize = ImVec2(ImGui::GetContentRegionAvail().x, 250);
    ImVec2 canvasEnd = ImVec2(windowPos.x + canvasSize.x, windowPos.y + canvasSize.y);
    ImGui::Text("EQ Visualization");
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(1, 1));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.1f, 0.1f, 0.1f, 1.0f));
    ImGui::BeginChild("EQCanvas", canvasSize, true);
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    std::vector<float> frequencies(numPoints);
    std::vector<float> responses(numPoints);
    float logMinFreq = std::log10(minFreq);
    float logMaxFreq = std::log10(maxFreq);
    for (int i = 0; i < numPoints; i++) {
        float t = (float)i / (numPoints - 1);
        float logFreq = logMinFreq + t * (logMaxFreq - logMinFreq);
        frequencies[i] = std::pow(10.0f, logFreq);
        responses[i] = calculate_eq_response(frequencies[i], lowCutoff, highCutoff, lowsGain, midsGain, highsGain, sampleRate);
    }
    float minResponse = responses[0];
    float maxResponse = responses[0];
    for (int i = 1; i < numPoints; i++) {
        if (responses[i] < minResponse) minResponse = responses[i];
        if (responses[i] > maxResponse) maxResponse = responses[i];
    }
    minResponse = std::fmax(0.0f, std::fmin(minResponse, 0.5f));
    maxResponse = std::fmax(15.0f, maxResponse * 1.1f);
    ImVec2 canvasTopLeft = ImGui::GetCursorScreenPos();
    ImVec2 canvasBottomRight = ImVec2(canvasTopLeft.x + canvasSize.x, canvasTopLeft.y + canvasSize.y);
    const float gridSize = 30.0f;
    for (float x = std::fmod(canvasTopLeft.x, gridSize); x < canvasBottomRight.x; x += gridSize) {
        drawList->AddLine(ImVec2(x, canvasTopLeft.y), ImVec2(x, canvasBottomRight.y), ImGui::ColorConvertFloat4ToU32(ImVec4(0.2f, 0.2f, 0.2f, 0.4f)));
    }
    for (float y = std::fmod(canvasTopLeft.y, gridSize); y < canvasBottomRight.y; y += gridSize) {
        drawList->AddLine(ImVec2(canvasTopLeft.x, y), ImVec2(canvasBottomRight.x, y), ImGui::ColorConvertFloat4ToU32(ImVec4(0.2f, 0.2f, 0.2f, 0.4f)));
    }
    const int numMarkers = 7;
    const float markerFreqs[] = { 20, 50, 100, 500, 1000, 5000, 20000 };
    const char* markerLabels[] = { "20Hz", "50Hz", "100Hz", "500Hz", "1kHz", "5kHz", "20kHz" };
    for (int i = 0; i < numMarkers; i++) {
        float t = (std::log10(markerFreqs[i]) - logMinFreq) / (logMaxFreq - logMinFreq);
        float x = canvasTopLeft.x + t * canvasSize.x;
        drawList->AddLine(ImVec2(x, canvasTopLeft.y), ImVec2(x, canvasBottomRight.y), ImGui::ColorConvertFloat4ToU32(ImVec4(0.3f, 0.3f, 0.3f, 0.6f)));
        ImVec2 textSize = ImGui::CalcTextSize(markerLabels[i]);
        drawList->AddText(ImVec2(x - textSize.x / 2, canvasBottomRight.y - textSize.y - 5), ImGui::ColorConvertFloat4ToU32(ImVec4(0.8f, 0.8f, 0.8f, 1.0f)), markerLabels[i]);
    }
    const int numLevels = 5;
    for (int i = 0; i <= numLevels; i++) {
        float level = minResponse + (maxResponse - minResponse) * i / numLevels;
        float y = canvasBottomRight.y - (level - minResponse) / (maxResponse - minResponse) * canvasSize.y;
        drawList->AddLine(ImVec2(canvasTopLeft.x, y), ImVec2(canvasBottomRight.x, y), ImGui::ColorConvertFloat4ToU32(ImVec4(0.3f, 0.3f, 0.3f, 0.6f)));
        char levelLabel[16];
        snprintf(levelLabel, sizeof(levelLabel), "%.1f", level);
        drawList->AddText(ImVec2(canvasTopLeft.x + 5, y - 10), ImGui::ColorConvertFloat4ToU32(ImVec4(0.8f, 0.8f, 0.8f, 1.0f)), levelLabel);
    }
    std::vector<ImVec2> points(numPoints);
    for (int i = 0; i < numPoints; i++) {
        float t = (float)i / (numPoints - 1);
        float x = canvasTopLeft.x + t * canvasSize.x;
        float y = canvasBottomRight.y - ((responses[i] - minResponse) / (maxResponse - minResponse)) * canvasSize.y;
        points[i] = ImVec2(x, y);
    }
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4 themeColor = ImVec4(0.8f, 0.8f, 0.8f, 1.0f);
    ImVec4 markerColor = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    for (int i = 0; i < numPoints - 1; i++) {
        ImVec2 p1 = points[i];
        ImVec2 p2 = points[i + 1];
        ImVec2 p3 = ImVec2(p2.x, canvasBottomRight.y);
        ImVec2 p4 = ImVec2(p1.x, canvasBottomRight.y);
        ImVec4 fillColor = themeColor;
        fillColor.w = 0.2f;
        drawList->AddQuadFilled(p1, p2, p3, p4, ImGui::ColorConvertFloat4ToU32(fillColor));
    }
    const float lineThickness = 3.0f;
    for (int i = 0; i < numPoints - 1; i++) {
        ImVec4 lineColor = themeColor;
        for (float glow = lineThickness * 3; glow >= lineThickness; glow -= 1.0f) {
            float alpha = 0.03f * (1.0f - (glow - lineThickness) / (lineThickness * 2));
            ImVec4 glowColor = lineColor;
            glowColor.w = alpha;
            drawList->AddLine(points[i], points[i + 1], ImGui::ColorConvertFloat4ToU32(glowColor), glow);
        }
        ImVec4 solidLineColor = lineColor;
        solidLineColor.w = 0.9f;
        drawList->AddLine(points[i], points[i + 1], ImGui::ColorConvertFloat4ToU32(solidLineColor), lineThickness);
    }
    float lowX = (std::log10(lowCutoff) - logMinFreq) / (logMaxFreq - logMinFreq) * canvasSize.x + canvasTopLeft.x;
    float highX = (std::log10(highCutoff) - logMinFreq) / (logMaxFreq - logMinFreq) * canvasSize.x + canvasTopLeft.x;
    {
        float yPos = canvasBottomRight.y - ((lowsGain - minResponse) / (maxResponse - minResponse)) * canvasSize.y;
        drawList->AddCircleFilled(ImVec2(lowX * 0.3f, yPos), 6.0f, ImGui::ColorConvertFloat4ToU32(markerColor));
        drawList->AddCircle(ImVec2(lowX * 0.3f, yPos), 8.0f, ImGui::ColorConvertFloat4ToU32(ImVec4(1, 1, 1, 0.5f)), 0, 2.0f);
    }
    {
        float midX = (lowX + highX) * 0.5f;
        float yPos = canvasBottomRight.y - ((midsGain - minResponse) / (maxResponse - minResponse)) * canvasSize.y;
        drawList->AddCircleFilled(ImVec2(midX, yPos), 6.0f, ImGui::ColorConvertFloat4ToU32(markerColor));
        drawList->AddCircle(ImVec2(midX, yPos), 8.0f, ImGui::ColorConvertFloat4ToU32(ImVec4(1, 1, 1, 0.5f)), 0, 2.0f);
    }
    {
        float yPos = canvasBottomRight.y - ((highsGain - minResponse) / (maxResponse - minResponse)) * canvasSize.y;
        drawList->AddCircleFilled(ImVec2(highX * 0.9f, yPos), 6.0f, ImGui::ColorConvertFloat4ToU32(markerColor));
        drawList->AddCircle(ImVec2(highX * 0.9f, yPos), 8.0f, ImGui::ColorConvertFloat4ToU32(ImVec4(1, 1, 1, 0.5f)), 0, 2.0f);
    }
    float pulseAlpha = (std::sin(animTime * 3.0f) + 1.0f) * 0.5f * 0.4f + 0.1f;
    ImVec4 borderColor = ImVec4(1.0f, 1.0f, 1.0f, pulseAlpha);
    drawList->AddRect(canvasTopLeft, canvasBottomRight, ImGui::ColorConvertFloat4ToU32(borderColor), 5.0f, ImDrawFlags_RoundCornersAll, 2.0f);
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
}

static bool showEQVisualization = true;

void RenderEQControls() {
    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 2));
    ImGui::SliderFloat("Treble", &TrebleGain, 0.0f, 50.0f, "%.2f dB");
    ImGui::SliderFloat("Mids", &MidGain, 4.0f, 80.0f, "%.2f dB");
    ImGui::SliderFloat("Bass", &BassGain, 4.0f, 70.0f, "%.2f dB");
    ImGui::Checkbox("Show EQ Visualization", &showEQVisualization);
    if (showEQVisualization) {
        DrawCoolEQVisualization(BassGain, MidGain, TrebleGain);
    }
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(3);
}

void CreateConsole() {
    AllocConsole();
    FILE* f;
    freopen_s(&f, "CONOUT$", "w", stdout);
}

static float time_since_start = 0.0f;
static bool ui_visible = true;
static HWND hwnd_global = nullptr;
static int current_hotkey = 0;
static bool waiting_for_key = false;

void UpdateTime() {
    static auto start_time = std::chrono::high_resolution_clock::now();
    auto current_time = std::chrono::high_resolution_clock::now();
    time_since_start = std::chrono::duration<float>(current_time - start_time).count();
}

ImVec4 GetAnimatedColor() {
    return ImVec4(0.8f, 0.8f, 0.8f, 0.8f);
}

namespace Spoofing {
    bool ProcessIsolation = false;
    bool Hider = true;
}

std::wstring ConvertToWide(const char* str) {
    int len = MultiByteToWideChar(CP_UTF8, 0, str, -1, nullptr, 0);
    std::wstring wstr(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, str, -1, &wstr[0], len);
    return wstr;
}

void SaveConfiguration(const char* file_path) {
    std::ofstream ofs(file_path, std::ios::binary);
    if (ofs) {
        ofs.write(reinterpret_cast<const char*>(&Gain), sizeof(Gain));
        ofs.write(reinterpret_cast<const char*>(&ExpGain), sizeof(ExpGain));
        ofs.write(reinterpret_cast<const char*>(&Spoofing::ProcessIsolation), sizeof(Spoofing::ProcessIsolation));
        ofs.write(reinterpret_cast<const char*>(&Spoofing::Hider), sizeof(Spoofing::Hider));
        ofs.write(reinterpret_cast<const char*>(&BassGain), sizeof(BassGain));
        ofs.write(reinterpret_cast<const char*>(&MidGain), sizeof(MidGain));
        ofs.write(reinterpret_cast<const char*>(&TrebleGain), sizeof(TrebleGain));
        ofs.write(reinterpret_cast<const char*>(&Pan), sizeof(Pan));
        ofs.write(reinterpret_cast<const char*>(&enableAutoEQ), sizeof(enableAutoEQ));
        ofs.write(reinterpret_cast<const char*>(&enableAboveHead), sizeof(enableAboveHead));
        ofs.write(reinterpret_cast<const char*>(&enableInHead), sizeof(enableInHead));
        ofs.write(reinterpret_cast<const char*>(&Wideness), sizeof(Wideness));
        ofs.write(reinterpret_cast<const char*>(&showEQVisualization), sizeof(showEQVisualization));
        ofs.close();
    }
}

bool SaveConfigurationWithDialog(HWND hwnd) {
    OPENFILENAMEW ofn;
    wchar_t szFile[260] = L"elemental.cfg";
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile) / sizeof(*szFile);
    ofn.lpstrFilter = L"Configuration Files (*.cfg)\0*.cfg\0All Files (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrFileTitle = nullptr;
    ofn.nMaxFileTitle = 0;
    ofn.lpstrInitialDir = nullptr;
    ofn.lpstrDefExt = L"cfg";
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT;
    if (GetSaveFileNameW(&ofn)) {
        SaveConfiguration(std::string(szFile, szFile + wcslen(szFile)).c_str());
        return true;
    }
    return false;
}

void LoadConfiguration(const char* file_path) {
    std::ifstream ifs(file_path, std::ios::binary);
    if (ifs) {
        ifs.read(reinterpret_cast<char*>(&Gain), sizeof(Gain));
        ifs.read(reinterpret_cast<char*>(&ExpGain), sizeof(ExpGain));
        ifs.read(reinterpret_cast<char*>(&Spoofing::ProcessIsolation), sizeof(Spoofing::ProcessIsolation));
        ifs.read(reinterpret_cast<char*>(&Spoofing::Hider), sizeof(Spoofing::Hider));
        ifs.read(reinterpret_cast<char*>(&BassGain), sizeof(BassGain));
        ifs.read(reinterpret_cast<char*>(&MidGain), sizeof(MidGain));
        ifs.read(reinterpret_cast<char*>(&TrebleGain), sizeof(TrebleGain));
        ifs.read(reinterpret_cast<char*>(&Pan), sizeof(Pan));
        ifs.read(reinterpret_cast<char*>(&enableAutoEQ), sizeof(enableAutoEQ));
        ifs.read(reinterpret_cast<char*>(&enableAboveHead), sizeof(enableAboveHead));
        ifs.read(reinterpret_cast<char*>(&enableInHead), sizeof(enableInHead));
        ifs.read(reinterpret_cast<char*>(&Wideness), sizeof(Wideness));
        ifs.read(reinterpret_cast<char*>(&showEQVisualization), sizeof(showEQVisualization));
        ifs.close();
    }
}

bool LoadConfigurationWithDialog(HWND hwnd) {
    OPENFILENAMEW ofn;
    wchar_t szFile[260] = L"";
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile) / sizeof(*szFile);
    ofn.lpstrFilter = L"Configuration Files (*.cfg)\0*.cfg\0All Files (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrFileTitle = nullptr;
    ofn.nMaxFileTitle = 0;
    ofn.lpstrInitialDir = nullptr;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;
    if (GetOpenFileNameW(&ofn)) {
        LoadConfiguration(std::string(szFile, szFile + wcslen(szFile)).c_str());
        return true;
    }
    return false;
}

void ToggleWindowVisibility(bool hider) {
    Spoofing::Hider = hider;
    SetWindowDisplayAffinity(hwnd_global, hider ? WDA_EXCLUDEFROMCAPTURE : WDA_NONE);
}

void RunElevatedCommand(const char* command) {
    ShellExecuteA(nullptr, "runas", "cmd.exe", command, nullptr, SW_HIDE);
}

void ClearTempFiles() {
    RunElevatedCommand("/c del /q /f /s %temp%\\* & for /d %%p in (%temp%\\*) do rd /s /q \"%%p\"");
}

void ClearRunHistory() {
    RunElevatedCommand("/c reg delete HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\RunMRU /f");
}

void ClearJournalTraces() {
    RunElevatedCommand("/c wevtutil cl System & wevtutil cl Application");
}

void ClearRegistryKeys() {
    RunElevatedCommand("/c reg delete HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\RecentDocs /f");
}

void ClearTaskManager() {
    RunElevatedCommand("/c reg delete HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\TaskManager /f");
}

void ClearPrefetchTraces() {
    RunElevatedCommand("/c del /q /f /s %SystemRoot%\\Prefetch\\* & for /d %%p in (%SystemRoot%\\Prefetch\\*) do rd /s /q \"%%p\"");
}

void ClearRecycleBin() {
    RunElevatedCommand("/c rd /s /q %SystemDrive%\\$Recycle.Bin");
}

void ClearBrowserCache() {
    RunElevatedCommand("/c del /q /f /s %LocalAppData%\\Google\\Chrome\\User Data\\Default\\Cache\\* & "
        "del /q /f /s %LocalAppData%\\Mozilla\\Firefox\\Profiles\\*\\cache\\* & "
        "del /q /f /s %AppData%\\Microsoft\\Edge\\User Data\\Default\\Cache\\*");
}

void utilities::ui::start() {
    WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(nullptr), nullptr, nullptr, nullptr, nullptr, L"Elemental", nullptr };
    ::RegisterClassExW(&wc);
    hwnd_global = ::CreateWindowW(wc.lpszClassName, L"Elemental", WS_OVERLAPPEDWINDOW & ~WS_CAPTION, 100, 100, 458, 550, nullptr, nullptr, wc.hInstance, nullptr);
    if (!CreateDeviceD3D(hwnd_global)) {
        CleanupDeviceD3D();
        ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return;
    }
    ::ShowWindow(hwnd_global, SW_SHOWDEFAULT);
    ::UpdateWindow(hwnd_global);
    SetWindowLong(hwnd_global, GWL_EXSTYLE, GetWindowLong(hwnd_global, GWL_EXSTYLE) | WS_EX_TOOLWINDOW);
    SetWindowPos(hwnd_global, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    if (current_hotkey != 0) {
        RegisterHotKey(NULL, 1, 0, current_hotkey);
    }
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    io.ConfigDebugIsDebuggerPresent = false;
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowPadding = ImVec2(20, 20);
    style.FramePadding = ImVec2(6, 4);
    style.ItemSpacing = ImVec2(10, 6);
    style.ItemInnerSpacing = ImVec2(4, 4);
    style.TouchExtraPadding = ImVec2(0, 0);
    style.IndentSpacing = 20.0f;
    style.ScrollbarSize = 14.0f;
    style.GrabMinSize = 10.0f;
    style.WindowBorderSize = 1.0f;
    style.ChildBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;
    style.FrameBorderSize = 1.0f;
    style.TabBorderSize = 1.0f;
    style.WindowRounding = 8.0f;
    style.ChildRounding = 6.0f;
    style.FrameRounding = 4.0f;
    style.PopupRounding = 6.0f;
    style.ScrollbarRounding = 4.0f;
    style.GrabRounding = 4.0f;
    style.TabRounding = 4.0f;
    style.WindowTitleAlign = ImVec2(0.5f, 0.5f);
    style.ButtonTextAlign = ImVec2(0.5f, 0.5f);
    style.DisplaySafeAreaPadding = ImVec2(4, 4);
    ImVec4* colors = style.Colors;
    colors[ImGuiCol_Text] = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    colors[ImGuiCol_TextDisabled] = ImVec4(0.7f, 0.7f, 0.7f, 1.0f);
    colors[ImGuiCol_WindowBg] = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);
    colors[ImGuiCol_ChildBg] = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);
    colors[ImGuiCol_PopupBg] = ImVec4(0.1f, 0.1f, 0.1f, 0.94f);
    colors[ImGuiCol_Border] = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    colors[ImGuiCol_BorderShadow] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    colors[ImGuiCol_FrameBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.8f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.3f, 0.3f, 0.3f, 0.8f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.4f, 0.4f, 0.4f, 0.8f);
    colors[ImGuiCol_TitleBg] = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);
    colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.0f, 0.0f, 0.0f, 0.5f);
    colors[ImGuiCol_MenuBarBg] = ImVec4(0.2f, 0.2f, 0.2f, 1.0f);
    colors[ImGuiCol_ScrollbarBg] = ImVec4(0.2f, 0.2f, 0.2f, 0.53f);
    colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.6f, 0.6f, 0.6f, 1.0f);
    colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.7f, 0.7f, 0.7f, 1.0f);
    colors[ImGuiCol_CheckMark] = ImVec4(0.8f, 0.8f, 0.8f, 1.0f);
    colors[ImGuiCol_SliderGrab] = ImVec4(0.8f, 0.8f, 0.8f, 1.0f);
    colors[ImGuiCol_SliderGrabActive] = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    colors[ImGuiCol_Button] = ImVec4(0.3f, 0.3f, 0.3f, 0.8f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.4f, 0.4f, 0.4f, 0.8f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
    colors[ImGuiCol_Header] = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);
    colors[ImGuiCol_Separator] = ImVec4(0.3f, 0.3f, 0.3f, 0.5f);
    colors[ImGuiCol_SeparatorHovered] = ImVec4(0.4f, 0.4f, 0.4f, 0.78f);
    colors[ImGuiCol_SeparatorActive] = ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
    colors[ImGuiCol_ResizeGrip] = ImVec4(0.8f, 0.8f, 0.8f, 0.25f);
    colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.8f, 0.8f, 0.8f, 0.67f);
    colors[ImGuiCol_ResizeGripActive] = ImVec4(0.8f, 0.8f, 0.8f, 0.95f);
    colors[ImGuiCol_Tab] = ImVec4(0.0f, 0.0f, 0.0f, 0.86f);
    colors[ImGuiCol_TabHovered] = ImVec4(0.2f, 0.2f, 0.2f, 0.86f);
    colors[ImGuiCol_TabActive] = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);
    colors[ImGuiCol_TabUnfocused] = ImVec4(0.0f, 0.0f, 0.0f, 0.97f);
    colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);
    colors[ImGuiCol_PlotLines] = ImVec4(0.8f, 0.8f, 0.8f, 1.0f);
    colors[ImGuiCol_PlotLinesHovered] = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    colors[ImGuiCol_PlotHistogram] = ImVec4(0.8f, 0.8f, 0.8f, 1.0f);
    colors[ImGuiCol_PlotHistogramHovered] = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    colors[ImGuiCol_TextSelectedBg] = ImVec4(0.8f, 0.8f, 0.8f, 0.35f);
    colors[ImGuiCol_DragDropTarget] = ImVec4(0.8f, 0.8f, 0.8f, 0.9f);
    colors[ImGuiCol_NavHighlight] = ImVec4(0.8f, 0.8f, 0.8f, 1.0f);
    colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.0f, 1.0f, 1.0f, 0.7f);
    colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.8f, 0.8f, 0.8f, 0.2f);
    colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.8f, 0.8f, 0.8f, 0.35f);
    ImFontConfig font_config;
    font_config.PixelSnapH = true;
    font_config.OversampleH = 1;
    font_config.OversampleV = 1;
    io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", 20.0f, &font_config);
    ImVec4 clear_color = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);
    ImGui_ImplWin32_Init(hwnd_global);
    ImGui_ImplDX9_Init(g_pd3dDevice);
    float last_time = 0.0f;
    LoadConfiguration("config.bin");
    ToggleWindowVisibility(Spoofing::Hider);
    bool done = false;
    while (!done) {
        MSG msg;
        while (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
            if (msg.message == WM_QUIT) done = true;
            if (msg.message == WM_HOTKEY && msg.wParam == 1) {
                ui_visible = !ui_visible;
                ShowWindow(hwnd_global, ui_visible ? SW_SHOW : SW_HIDE);
                SaveConfiguration("config.bin");
            }
            if (waiting_for_key && msg.message == WM_KEYDOWN) {
                int new_hotkey = static_cast<int>(msg.wParam);
                UnregisterHotKey(NULL, 1);
                current_hotkey = new_hotkey;
                if (current_hotkey != 0) RegisterHotKey(NULL, 1, 0, current_hotkey);
                waiting_for_key = false;
            }
        }
        if (done) break;
        UpdateTime();
        float delta_time = time_since_start - last_time;
        last_time = time_since_start;
        ImGui_ImplDX9_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        RECT Rect;
        GetClientRect(hwnd_global, &Rect);
        ImVec2 window_size((float)(Rect.right - Rect.left), (float)(Rect.bottom - Rect.top));
        ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
        ImGui::SetNextWindowSize(window_size, ImGuiCond_Always);
        if (ui_visible) {
            ImGui::Begin("Sal Bel Hook", nullptr, ImGuiWindowFlags_NoCollapse);
            ImDrawList* draw_list = ImGui::GetWindowDrawList();
            draw_list->AddRectFilled(ImVec2(0, 0), window_size, ImGui::GetColorU32(ImVec4(0.0f, 0.0f, 0.0f, 1.0f)));
            ImVec4 tab_color = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
            float border_thickness = 1.0f;
            float inset = 5.0f;
            draw_list->AddRect(ImVec2(inset, inset), ImVec2(window_size.x - inset, window_size.y - inset), ImGui::GetColorU32(tab_color), 0.0f, 0, border_thickness);
            draw_list->AddRect(ImVec2(0, 0), window_size, ImGui::GetColorU32(ImVec4(0.0f, 0.0f, 0.0f, 1.0f)), 0.0f, 0, 1.0f);
            if (ImGui::BeginTabBar("MainTabBar")) {
                if (ImGui::BeginTabItem("Encoder")) {
                    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 2));
                    ImGui::Text("Audio Amplification");
                    ImGui::Text("Audio Amplification");
                    ImGui::SliderFloat("vUnits", &vUnits, 0.0f, 90.0f, "%.1f dB");
                    ImGui::SliderFloat("Gain", &Gain, 0.0f, 90.0f, "%.1f dB");
                    ImGui::SliderFloat("Rage Gain", &ExpGain, 0.0f, 90.0f, "%.1f dB");
                    ImGui::Separator();
                    ImGui::SliderFloat("Bitrate", &Bitrate, 8000.0f, 510000.0f, "%.2f");
                    ImGui::SliderFloat("EnergySL", &energySLStrength, 0.0f, 100.0f, "%.2f");
                    ImGui::SliderFloat("Lowering", &LoweringSlider, 0.0f, 20.0f, "%.2f");
                    ImGui::Separator();
                    if (ImGui::CollapsingHeader("Spoofing")) {
                        ImGui::Checkbox("HighPass ", &enableHighPass);
                        ImGui::Checkbox("Spoof dB", &enableClipping);
                    }
                    if (ImGui::CollapsingHeader("Equalizer")) {
                        RenderEQControls();
                        ImGui::Checkbox("Auto EQ ", &enableAutoEQ);
                        ImGui::Separator();
                    }
                    if (ImGui::CollapsingHeader("Panning")) {
                        ImGui::SliderFloat("Wideness", &Wideness, 100.0f, 400.0f, "%.0f");
                        ImGui::SliderFloat("Pan", &Pan, -1.00f, 1.00f, "%.2f");
                        ImGui::Checkbox("Above Head", &enableAboveHead);
                        ImGui::Checkbox("In Head", &enableInHead);
                    }
                    if (ImGui::CollapsingHeader("Pitch")) {
                        ImGui::SliderFloat("Pitch Octave", &PitchOctave, -24.0f, 24.0f, "%.1f");
                    }
                    if (ImGui::CollapsingHeader("Audio")) {
                        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.0f, 0.0f, 0.0f, 0.8f));
                        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.0f, 0.0f, 0.0f, 0.8f));
                        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.0f, 0.0f, 0.0f, 0.8f));
                        static int combo1_current = 0;
                        const char* combo1_items[] = { "5760", "960", "480" };
                        ImGui::Combo("Pacsize", &combo1_current, combo1_items, IM_ARRAYSIZE(combo1_items));
                        static int combo2_current = 0;
                        const char* combo2_items[] = { "4096", "2048", "1024", "512", "128" };
                        ImGui::Combo("Audio Payload", &combo2_current, combo2_items, IM_ARRAYSIZE(combo2_items));
                        static int combo6_current = 9;
                        const char* combo6_items[] = { "1", "2", "3", "4", "5", "6", "7", "8", "9", "10" };
                        ImGui::Combo("Complexity", &combo6_current, combo6_items, IM_ARRAYSIZE(combo6_items));
                        static int combo10_current = 0;
                        const char* combo10_items[] = { "24", "16" };
                        ImGui::Combo("LSB Depth", &combo10_current, combo10_items, IM_ARRAYSIZE(combo10_items));
                        static int combo3_current = 0;
                        const char* combo3_items[] = { "CBR", "VBR" };
                        ImGui::Combo("Encoding Method", &combo3_current, combo3_items, IM_ARRAYSIZE(combo3_items));
                        static int combo4_current = 0;
                        const char* combo4_items[] = { "Celt", "Silk", "Hybrid" };
                        ImGui::Combo("Audio Mode", &combo4_current, combo4_items, IM_ARRAYSIZE(combo4_items));
                        static int combo5_current = 0;
                        const char* combo5_items[] = { "Music", "VOIP" };
                        ImGui::Combo("Audio Application", &combo5_current, combo5_items, IM_ARRAYSIZE(combo5_items));
                        static int combo12_current = 0;
                        const char* combo12_items[] = { "-1", "+1" };
                        ImGui::Combo("Voice Ratio", &combo12_current, combo12_items, IM_ARRAYSIZE(combo12_items));
                        static int combo11_current = 0;
                        const char* combo11_items[] = { "Full Band", "Medium Band", "Narrow Band", "Super Wide Band", "Wide Band" };
                        ImGui::Combo("Bandwidth", &combo11_current, combo11_items, IM_ARRAYSIZE(combo11_items));
                        ImGui::PopStyleColor(3);
                        ImGui::Separator();
                    }
                    ImGui::PopStyleVar();
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Decoder")) {
                    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 2));
                    ImGui::Separator();
                    ImGui::Checkbox("Client User Detection", &dbcheck);
                    //ImGui::SliderFloat("dB Multiplier", &g_dbMultiplier, 1.0f, 20.0f, "%.1f");
                    ImGui::Separator();
                    ImGui::PopStyleVar();
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Misc")) {
                    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 2));
                    ImGui::Separator();
                    if (ImGui::CollapsingHeader("Clean Traces")) {
                        if (ImGui::Button("Clear Temp")) {
                            ClearTempFiles();
                        }
                        if (ImGui::Button("Clear Run History")) {
                            ClearRunHistory();
                        }
                        if (ImGui::Button("Clear Journal Traces")) {
                            ClearJournalTraces();
                        }
                        if (ImGui::Button("Clear Registry Keys")) {
                            ClearRegistryKeys();
                        }
                        if (ImGui::Button("Clear Task Manager")) {
                            ClearTaskManager();
                        }
                        if (ImGui::Button("Clear Prefetch Traces")) {
                            ClearPrefetchTraces();
                        }
                        if (ImGui::Button("Clear Recycle Bin")) {
                            ClearRecycleBin();
                        }
                        if (ImGui::Button("Clear Browser Cache")) {
                            ClearBrowserCache();
                        }
                    }
                    ImGui::Separator();
                    if (ImGui::Button("Join Discord")) {
                        ShellExecuteA(nullptr, "open", "https://discord.gg/wAgNm9eNnS", nullptr, nullptr, SW_SHOWNORMAL);
                    }
                    if (ImGui::Button("Download Modules")) {
                        ShellExecuteA(nullptr, "open", "https://cdn.discordapp.com/attachments/1351720157252292739/1377547049439989770/discord_voice.rar?ex=68395c2a&is=68380aaa&hm=bb4656d8926a4c33d4cc233a8cf10a4605ccdee7b0f4943958d7afd496eb682e&", nullptr, nullptr, SW_SHOWNORMAL);
                    }
                    if (ImGui::Button("Download Injecter")) {
                        ShellExecuteA(nullptr, "open", "https://www.youtube.com", nullptr, nullptr, SW_SHOWNORMAL);
                    }
                   
                    ImGui::Separator();
                    ImGui::PopStyleVar();
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Settings")) {
                    ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[0]);
                    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 2));
                    bool hider_changed = ImGui::Checkbox("Hide Window", &Spoofing::Hider);
                    if (hider_changed) {
                        ToggleWindowVisibility(Spoofing::Hider);
                        SaveConfiguration("config.bin");
                    }
                    ImGui::Text("Hide UI Hotkey:");
                    ImGui::SameLine();
                    std::string hotkey_label = (waiting_for_key ? "Press a key..." : (current_hotkey == 0 ? "None" : std::string(1, static_cast<char>(current_hotkey))));
                    if (ImGui::Button(hotkey_label.c_str(), ImVec2(100, 0))) {
                        waiting_for_key = true;
                    }
 
                    ImGui::Separator();
                    if (ImGui::Button("Save Config")) {
                        SaveConfigurationWithDialog(hwnd_global);
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Load Config")) {
                        LoadConfigurationWithDialog(hwnd_global);
                    }
                    ImGui::Separator();
                    ImGui::Text("Hook Runtime: %.2f seconds", time_since_start);
                    ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
                    ImGui::Text("Ascend Was Here");
                    ImGui::PopStyleVar();
                    ImGui::PopFont();
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }
            ImGui::End();
        }
        ImGui::EndFrame();
        g_pd3dDevice->SetRenderState(D3DRS_ZENABLE, FALSE);
        g_pd3dDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
        g_pd3dDevice->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
        D3DCOLOR clear_col_dx = D3DCOLOR_RGBA((int)(clear_color.x * clear_color.w * 255.0f), (int)(clear_color.y * clear_color.w * 255.0f), (int)(clear_color.z * clear_color.w * 255.0f), (int)(clear_color.w * 255.0f));
        g_pd3dDevice->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, clear_col_dx, 1.0f, 0);
        if (g_pd3dDevice->BeginScene() >= 0) {
            ImGui::Render();
            ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
            g_pd3dDevice->EndScene();
        }
        HRESULT result = g_pd3dDevice->Present(nullptr, nullptr, nullptr, nullptr);
        if (result == D3DERR_DEVICELOST && !g_DeviceLost) {
            g_DeviceLost = true;
            ImGui_ImplDX9_InvalidateDeviceObjects();
            ResetDevice();
            if (g_pd3dDevice->TestCooperativeLevel() != D3D_OK) {
                UnregisterHotKey(NULL, 1);
                ImGui_ImplDX9_Shutdown();
                ImGui_ImplWin32_Shutdown();
                ImGui::DestroyContext();
                CleanupDeviceD3D();
                ::DestroyWindow(hwnd_global);
                ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
                done = true;
            }
            else g_DeviceLost = false;
        }
        else if (result < 0) done = true;
    }
    if (!done) {
        UnregisterHotKey(NULL, 1);
        ImGui_ImplDX9_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        CleanupDeviceD3D();
        ::DestroyWindow(hwnd_global);
        ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
    }
}

bool CreateDeviceD3D(HWND hWnd) {
    if ((g_pD3D = Direct3DCreate9(D3D_SDK_VERSION)) == nullptr) return false;
    ZeroMemory(&g_d3dpp, sizeof(g_d3dpp));
    g_d3dpp.Windowed = TRUE;
    g_d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    g_d3dpp.BackBufferFormat = D3DFMT_UNKNOWN;
    g_d3dpp.EnableAutoDepthStencil = TRUE;
    g_d3dpp.AutoDepthStencilFormat = D3DFMT_D16;
    g_d3dpp.PresentationInterval = D3DPRESENT_INTERVAL_ONE;
    if (g_pD3D->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hWnd, D3DCREATE_HARDWARE_VERTEXPROCESSING, &g_d3dpp, &g_pd3dDevice) < 0) return false;
    return true;
}

void CleanupDeviceD3D() {
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
    if (g_pD3D) { g_pD3D->Release(); g_pD3D = nullptr; }
}

void ResetDevice() {
    ImGui_ImplDX9_InvalidateDeviceObjects();
    HRESULT hr = g_pd3dDevice->Reset(&g_d3dpp);
    if (hr == D3DERR_INVALIDCALL) IM_ASSERT(0);
    ImGui_ImplDX9_CreateDeviceObjects();
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam)) return true;
    switch (msg) {
    case WM_SIZE:
        if (wParam == SIZE_MINIMIZED) return 0;
        if (g_pd3dDevice != nullptr && wParam != SIZE_MINIMIZED) {
            g_d3dpp.BackBufferWidth = LOWORD(lParam);
            g_d3dpp.BackBufferHeight = HIWORD(lParam);
            ResetDevice();
        }
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) return 0;
        break;
    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;
    case WM_CREATE:
        break;
    }
    return ::DefWindowProc(hWnd, msg, wParam, lParam);
}