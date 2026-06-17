#pragma once
#include <string>
#include "imgui.h"

namespace Theme {

// ── Default Color Palette (mutable — UpdateFromClientBg can modify) ──
inline ImVec4 Bg       {0.973f,0.973f,0.973f,1}; // #F8F8F8
inline ImVec4 BgAlt    {0.945f,0.945f,0.949f,1}; // #F1F1F2
inline ImVec4 Surface  {0.925f,0.925f,0.929f,1}; // #ECECED
inline ImVec4 SurfaceH {0.890f,0.890f,0.898f,1}; // #E3E3E5
inline ImVec4 Border   {0.780f,0.780f,0.784f,1};
inline ImVec4 Accent   {0.000f,0.471f,0.831f,1}; // #0078D4
inline ImVec4 AccentH  {0.129f,0.557f,0.878f,1};
inline ImVec4 Text     {0.102f,0.102f,0.102f,1}; // #1A1A1A
inline ImVec4 TextDim  {0.400f,0.400f,0.400f,1};
inline ImVec4 TextAcc  {0.000f,0.471f,0.831f,1};
inline ImVec4 Success  {0.067f,0.604f,0.290f,1};
inline ImVec4 Warning  {0.878f,0.522f,0.000f,1};
inline ImVec4 Error    {0.808f,0.129f,0.129f,1};
inline ImVec4 Folder   {0.835f,0.549f,0.000f,1};
inline ImVec4 FileClr  {0.400f,0.400f,0.400f,1};
inline ImVec4 ClrImage {0.012f,0.475f,0.812f,1};
inline ImVec4 ClrAudio {0.592f,0.012f,0.557f,1};
inline ImVec4 ClrVideo {0.812f,0.012f,0.012f,1};
inline ImVec4 ClrArchive{0.596f,0.396f,0.000f,1};
inline ImVec4 ClrCode  {0.012f,0.553f,0.235f,1};
inline ImVec4 ClrDoc   {0.012f,0.424f,0.620f,1};
inline ImVec4 TitleBar {0.824f,0.851f,0.890f,1}; // #D2D9E3 — light blue-gray

// ── Sizing ───────────────────────────────────────────────────────
inline constexpr float FrameRounding = 4, ChildRounding = 6, WindowRounding = 8;
inline constexpr float ScrollbarRounding = 4, GrabRounding = 4;
inline constexpr float TitleBarHeight = 34;

// ── Public API ───────────────────────────────────────────────────
void ApplyTheme();

// Recalculate all derived colours from a client-area background.
// Call after user changes the background colour in settings.
void UpdateFromClientBg(const ImVec4& clientBg);

// Segoe MDL2 icon helpers (returns UTF-8 encoded PUA characters)
const char* IconForFile(const std::string& ext);
const char* IconFolder();
ImVec4      ColorForFile(const std::string& ext);

float  Lerp(float a, float b, float t);
float  SmoothStep(float* current, float target, float speed = 0.12f);

} // namespace Theme
