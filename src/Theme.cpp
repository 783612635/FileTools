#include "Theme.h"
#include "imgui_internal.h"
#include <algorithm>
#include <cmath>

namespace Theme {

void ApplyTheme()
{
    ImGuiStyle& s = ImGui::GetStyle();
    s.Colors[ImGuiCol_WindowBg]=Bg; s.Colors[ImGuiCol_ChildBg]=BgAlt;
    s.Colors[ImGuiCol_PopupBg]=ImVec4(1,1,1,1); s.Colors[ImGuiCol_Border]=Border;
    s.Colors[ImGuiCol_BorderShadow]=ImVec4(0,0,0,0);
    s.Colors[ImGuiCol_FrameBg]=ImVec4(0.96f,0.96f,0.96f,1);
    s.Colors[ImGuiCol_FrameBgHovered]=Surface; s.Colors[ImGuiCol_FrameBgActive]=SurfaceH;
    s.Colors[ImGuiCol_TitleBg]=Bg; s.Colors[ImGuiCol_TitleBgActive]=Surface;
    s.Colors[ImGuiCol_TitleBgCollapsed]=Bg; s.Colors[ImGuiCol_MenuBarBg]=ImVec4(0.98f,0.98f,0.98f,1);
    s.Colors[ImGuiCol_ScrollbarBg]=Bg; s.Colors[ImGuiCol_ScrollbarGrab]=ImVec4(0.80f,0.80f,0.80f,1);
    s.Colors[ImGuiCol_ScrollbarGrabHovered]=ImVec4(0.65f,0.65f,0.65f,1);
    s.Colors[ImGuiCol_ScrollbarGrabActive]=ImVec4(0.50f,0.50f,0.50f,1);
    s.Colors[ImGuiCol_CheckMark]=Accent; s.Colors[ImGuiCol_SliderGrab]=Accent;
    s.Colors[ImGuiCol_SliderGrabActive]=AccentH; s.Colors[ImGuiCol_Button]=Accent;
    s.Colors[ImGuiCol_ButtonHovered]=AccentH; s.Colors[ImGuiCol_ButtonActive]=ImVec4(0,0.35f,0.65f,1);
    s.Colors[ImGuiCol_Header]=Surface; s.Colors[ImGuiCol_HeaderHovered]=SurfaceH;
    s.Colors[ImGuiCol_HeaderActive]=Border; s.Colors[ImGuiCol_Separator]=Border;
    s.Colors[ImGuiCol_SeparatorHovered]=Accent; s.Colors[ImGuiCol_SeparatorActive]=AccentH;
    s.Colors[ImGuiCol_ResizeGrip]=Border; s.Colors[ImGuiCol_ResizeGripHovered]=Accent;
    s.Colors[ImGuiCol_ResizeGripActive]=AccentH; s.Colors[ImGuiCol_Tab]=Surface;
    s.Colors[ImGuiCol_TabHovered]=SurfaceH; s.Colors[ImGuiCol_TabActive]=Accent;
    s.Colors[ImGuiCol_TabUnfocused]=Surface; s.Colors[ImGuiCol_TabUnfocusedActive]=Border;
    s.Colors[ImGuiCol_DockingPreview]=ImVec4(Accent.x,Accent.y,Accent.z,0.4f);
    s.Colors[ImGuiCol_DockingEmptyBg]=Bg; s.Colors[ImGuiCol_PlotLines]=Accent;
    s.Colors[ImGuiCol_PlotLinesHovered]=AccentH; s.Colors[ImGuiCol_PlotHistogram]=Accent;
    s.Colors[ImGuiCol_PlotHistogramHovered]=AccentH; s.Colors[ImGuiCol_TableHeaderBg]=Surface;
    s.Colors[ImGuiCol_TableBorderStrong]=Border; s.Colors[ImGuiCol_TableBorderLight]=ImVec4(0.92f,0.92f,0.92f,1);
    s.Colors[ImGuiCol_TableRowBg]=ImVec4(1,1,1,1); s.Colors[ImGuiCol_TableRowBgAlt]=ImVec4(0.97f,0.97f,0.97f,1);
    s.Colors[ImGuiCol_Text]=Text; s.Colors[ImGuiCol_TextDisabled]=TextDim;
    s.Colors[ImGuiCol_TextSelectedBg]=ImVec4(Accent.x,Accent.y,Accent.z,0.25f);
    s.Colors[ImGuiCol_NavHighlight]=Accent; s.Colors[ImGuiCol_NavWindowingHighlight]=Accent;
    s.Colors[ImGuiCol_NavWindowingDimBg]=ImVec4(0,0,0,0.3f);
    s.Colors[ImGuiCol_ModalWindowDimBg]=ImVec4(0,0,0,0.3f);
    s.FrameRounding=FrameRounding; s.ChildRounding=ChildRounding;
    s.WindowRounding=WindowRounding; s.ScrollbarRounding=ScrollbarRounding;
    s.GrabRounding=GrabRounding; s.WindowBorderSize=0; s.ChildBorderSize=1;
    s.FrameBorderSize=0; s.PopupBorderSize=1; s.TabBorderSize=0;
    s.WindowPadding=ImVec2(10,10); s.FramePadding=ImVec2(10,6);
    s.CellPadding=ImVec2(8,4); s.ItemSpacing=ImVec2(8,6);
    s.ItemInnerSpacing=ImVec2(6,4); s.IndentSpacing=20;
    s.ScrollbarSize=12; s.GrabMinSize=10; s.Alpha=1; s.DisabledAlpha=0.4f;
    s.AntiAliasedLines=true; s.AntiAliasedLinesUseTex=true; s.AntiAliasedFill=true;
}

// Segoe MDL2 Assets icons (monochrome, PUA U+E000-U+F8FF, UTF-8 encoded)

const char* IconForFile(const std::string& ext)
{
    if (ext==".png"||ext==".jpg"||ext==".jpeg"||ext==".gif"||ext==".bmp"||ext==".ico"||ext==".webp"||ext==".svg")
        return "\xEE\x9C\xA2";  // Picture
    if (ext==".mp3"||ext==".wav"||ext==".flac"||ext==".aac"||ext==".ogg"||ext==".wma"||ext==".m4a")
        return "\xEE\xA3\x96";  // Music
    if (ext==".mp4"||ext==".avi"||ext==".mkv"||ext==".mov"||ext==".wmv"||ext==".webm")
        return "\xEE\x9C\x94";  // Video
    if (ext==".zip"||ext==".rar"||ext==".7z"||ext==".tar"||ext==".gz"||ext==".bz2"||ext==".xz")
        return "\xEF\x80\x92";  // Zip
    if (ext==".exe"||ext==".dll"||ext==".msi"||ext==".bat"||ext==".cmd")
        return "\xEE\x9D\xB9";  // App
    if (ext==".txt"||ext==".md"||ext==".csv"||ext==".log"||ext==".ini"||ext==".cfg")
        return "\xEE\xA2\xA5";  // Document
    if (ext==".pdf")
        return "\xEE\xA5\x98";  // PDF
    if (ext==".doc"||ext==".docx"||ext==".rtf")
        return "\xEE\xA2\xA5";  // Document
    if (ext==".xls"||ext==".xlsx")
        return "\xEE\xA7\xB9";  // Excel
    if (ext==".ppt"||ext==".pptx")
        return "\xEE\xA4\x82";  // PPT
    if (ext==".cpp"||ext==".h"||ext==".c"||ext==".hpp"||ext==".cs"||ext==".py"||ext==".js"||ext==".ts"||ext==".java"||ext==".go"||ext==".rs")
        return "\xEE\xA5\x83";  // Code
    return "\xEE\xA2\xA5";  // Default: Document
}

const char* IconFolder() { return "\xEE\xA2\xB7"; } // Folder icon

ImVec4 ColorForFile(const std::string& ext)
{
    if (ext==".png"||ext==".jpg"||ext==".jpeg"||ext==".gif"||ext==".bmp"||ext==".ico"||ext==".webp"||ext==".svg")
        return ClrImage;
    if (ext==".mp3"||ext==".wav"||ext==".flac"||ext==".aac"||ext==".ogg"||ext==".wma"||ext==".m4a")
        return ClrAudio;
    if (ext==".mp4"||ext==".avi"||ext==".mkv"||ext==".mov"||ext==".wmv"||ext==".webm")
        return ClrVideo;
    if (ext==".zip"||ext==".rar"||ext==".7z"||ext==".tar"||ext==".gz"||ext==".bz2")
        return ClrArchive;
    if (ext==".cpp"||ext==".h"||ext==".c"||ext==".hpp"||ext==".cs"||ext==".py"||ext==".js"||ext==".ts"||ext==".java"||ext==".go"||ext==".rs")
        return ClrCode;
    if (ext==".pdf"||ext==".doc"||ext==".docx"||ext==".xls"||ext==".xlsx"||ext==".ppt"||ext==".pptx"||ext==".txt"||ext==".md")
        return ClrDoc;
    return FileClr;
}

float Lerp(float a, float b, float t) { return a+(b-a)*t; }
float SmoothStep(float* c, float t, float s) {
    *c = Lerp(*c, t, 1.f - expf(-s * ImGui::GetIO().DeltaTime * 60.f));
    return *c;
}

void UpdateFromClientBg(const ImVec4& clientBg)
{
    float lum = 0.299f*clientBg.x + 0.587f*clientBg.y + 0.114f*clientBg.z;
    bool dark = (lum < 0.5f);

    // Client area itself
    Bg = clientBg;

    // Derived surfaces: slightly darker/lighter offsets from Bg
    float offset1 = dark ? 0.06f : -0.03f;
    float offset2 = dark ? 0.10f : -0.05f;
    float offset3 = dark ? 0.15f : -0.08f;

    BgAlt    = ImVec4(std::clamp(Bg.x+offset1,0.f,1.f), std::clamp(Bg.y+offset1,0.f,1.f), std::clamp(Bg.z+offset1,0.f,1.f), 1);
    Surface  = ImVec4(std::clamp(Bg.x+offset2,0.f,1.f), std::clamp(Bg.y+offset2,0.f,1.f), std::clamp(Bg.z+offset2,0.f,1.f), 1);
    SurfaceH = ImVec4(std::clamp(Bg.x+offset3,0.f,1.f), std::clamp(Bg.y+offset3,0.f,1.f), std::clamp(Bg.z+offset3,0.f,1.f), 1);

    // Border
    Border = ImVec4(std::clamp(Bg.x+(dark?0.20f:-0.18f),0.f,1.f),
                    std::clamp(Bg.y+(dark?0.20f:-0.18f),0.f,1.f),
                    std::clamp(Bg.z+(dark?0.20f:-0.18f),0.f,1.f), 1);

    // Text: light on dark bg, dark on light bg
    Text    = dark ? ImVec4(0.92f,0.92f,0.92f,1) : ImVec4(0.10f,0.10f,0.10f,1);
    TextDim = dark ? ImVec4(0.55f,0.55f,0.55f,1) : ImVec4(0.40f,0.40f,0.40f,1);

    // Apply to ImGui style
    ImGuiStyle& s = ImGui::GetStyle();
    s.Colors[ImGuiCol_WindowBg]       = Bg;
    s.Colors[ImGuiCol_ChildBg]        = BgAlt;
    s.Colors[ImGuiCol_PopupBg]        = dark ? ImVec4(0.18f,0.18f,0.18f,1) : ImVec4(1,1,1,1);
    s.Colors[ImGuiCol_Border]         = Border;
    s.Colors[ImGuiCol_FrameBg]        = Surface;
    s.Colors[ImGuiCol_FrameBgHovered] = SurfaceH;
    s.Colors[ImGuiCol_FrameBgActive]  = SurfaceH;
    s.Colors[ImGuiCol_TitleBg]        = Bg;
    s.Colors[ImGuiCol_TitleBgActive]  = Surface;
    s.Colors[ImGuiCol_TitleBgCollapsed] = Bg;
    s.Colors[ImGuiCol_MenuBarBg]      = BgAlt;
    s.Colors[ImGuiCol_ScrollbarBg]    = Bg;
    s.Colors[ImGuiCol_ScrollbarGrab]  = Border;
    s.Colors[ImGuiCol_Header]         = Surface;
    s.Colors[ImGuiCol_HeaderHovered]  = SurfaceH;
    s.Colors[ImGuiCol_HeaderActive]   = Border;
    s.Colors[ImGuiCol_Separator]      = Border;
    s.Colors[ImGuiCol_Tab]            = Surface;
    s.Colors[ImGuiCol_TabHovered]     = SurfaceH;
    s.Colors[ImGuiCol_TabActive]      = Accent;
    s.Colors[ImGuiCol_TabUnfocused]   = Surface;
    s.Colors[ImGuiCol_TabUnfocusedActive] = Border;
    s.Colors[ImGuiCol_DockingEmptyBg] = Bg;
    s.Colors[ImGuiCol_TableHeaderBg]  = Surface;
    s.Colors[ImGuiCol_TableBorderStrong] = Border;
    s.Colors[ImGuiCol_TableBorderLight]  = dark ? ImVec4(0.25f,0.25f,0.25f,1) : ImVec4(0.92f,0.92f,0.92f,1);
    s.Colors[ImGuiCol_TableRowBg]     = dark ? ImVec4(0.15f,0.15f,0.15f,1) : ImVec4(1,1,1,1);
    s.Colors[ImGuiCol_TableRowBgAlt]  = dark ? ImVec4(0.12f,0.12f,0.12f,1) : ImVec4(0.97f,0.97f,0.97f,1);
    s.Colors[ImGuiCol_Text]           = Text;
    s.Colors[ImGuiCol_TextDisabled]   = TextDim;
}

} // namespace Theme
