#pragma once

#ifndef OVERLAY_H
#define OVERLAY_H

#include "imgui/imgui_impl_win32.h"
#include "imgui/imgui_impl_dx9.h"
#include "imgui/imgui.h"

#include "d3d9.h"
#include "tchar.h"

static LPDIRECT3D9              g_pD3D = nullptr;
static LPDIRECT3DDEVICE9        g_pd3dDevice = nullptr;
static bool                     g_DeviceLost = false;
static UINT                     g_ResizeWidth = 0, g_ResizeHeight = 0;
static D3DPRESENT_PARAMETERS    g_d3dpp = {};
constexpr int MAX_DB_SAMPLES = 120; // thats the max number of data points to show in graph

struct AudioLevelData {
    float db;
    float lufs;
    bool isAbnormal;
    bool isClipped;      // Add this field if not already present
    float isHardClipped;  // Add this field if not already present

    // Constructor to initialize all fields
    AudioLevelData(float db, float lufs, bool isAbnormal, bool isClipped, bool isHardClipped)
        : db(db), lufs(lufs), isAbnormal(isAbnormal), isClipped(isClipped), isHardClipped(isHardClipped) {
    }
};
bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void ResetDevice();
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace utilities::ui {
	void start();
}

#endif