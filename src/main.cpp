#include <iostream>
#include <vector>
#include <string>
#include <ctime>
#include <filesystem>
#include <regex>
#include <algorithm>
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <windows.h>
#include <commdlg.h>
#include <shlobj.h>
#include <map>
#include <thread>
#include <fstream>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "Downloader.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

namespace fs = std::filesystem;

struct AppConfig {
    std::string downloadDir;
    std::string cookiesBrowser;
    std::string cookiesPath;
};

void SaveConfig(const AppConfig& config) {
    std::ofstream out("config.txt");
    if (out.is_open()) {
        out << "downloadDir=" << config.downloadDir << "\n";
        out << "cookiesBrowser=" << config.cookiesBrowser << "\n";
        out << "cookiesPath=" << config.cookiesPath << "\n";
    }
}

AppConfig LoadConfig() {
    AppConfig config;
    std::ifstream in("config.txt");
    if (in.is_open()) {
        std::string line;
        while (std::getline(in, line)) {
            if (line.rfind("downloadDir=", 0) == 0) config.downloadDir = line.substr(12);
            else if (line.rfind("cookiesBrowser=", 0) == 0) config.cookiesBrowser = line.substr(15);
            else if (line.rfind("cookiesPath=", 0) == 0) config.cookiesPath = line.substr(12);
        }
    }
    return config;
}

// Native Win32 file picker filtered to text / cookie files
std::string OpenFilePicker() {
    OPENFILENAMEA ofn;
    char szFile[260] = {0};
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize  = sizeof(ofn);
    ofn.hwndOwner    = NULL;
    ofn.lpstrFile    = szFile;
    ofn.nMaxFile     = sizeof(szFile);
    ofn.lpstrFilter  = "Text Files (*.txt)\0*.txt\0All Files (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.Flags        = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

    if (GetOpenFileNameA(&ofn) == TRUE) {
        return std::string(szFile);
    }
    return "";
}

// Native Win32 folder picker dialog
std::string OpenFolderPicker() {
    char path[MAX_PATH] = {0};
    BROWSEINFOA bi      = {0};
    bi.lpszTitle        = "Select Output Folder";
    bi.ulFlags          = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;

    LPITEMIDLIST pidl = SHBrowseForFolderA(&bi);
    if (pidl != NULL) {
        SHGetPathFromIDListA(pidl, path);
        CoTaskMemFree(pidl);
        return std::string(path);
    }
    return "";
}

// Strip TikTok URL down to its base (removes query params / tracking)
std::string StripTikTokURL(const std::string& url) {
    std::regex baseRegex(R"(^https?:\/\/(?:vm\.|vt\.|www\.)?tiktok\.com\/[^\?]+)");
    std::smatch match;
    if (std::regex_search(url, match, baseRegex)) {
        return match[0].str();
    }
    return url;
}

// Human-readable file size (e.g. "12.4 MB")
std::string FormatFileSize(uintmax_t bytes) {
    if (bytes < 1024)                return std::to_string(bytes) + " B";
    if (bytes < 1024 * 1024)         return std::to_string(bytes / 1024) + " KB";
    if (bytes < 1024 * 1024 * 1024)  return std::to_string(bytes / (1024 * 1024)) + " MB";
    return std::to_string(bytes / (1024 * 1024 * 1024)) + " GB";
}

// Collect every .mp4 / .mkv / .webm in a folder (non-recursive, sorted newest first)
struct VideoEntry {
    std::string path;
    std::string name;
    std::string size;
    fs::file_time_type modTime;
};

std::vector<VideoEntry> ScanVideos(const std::string& dir) {
    std::vector<VideoEntry> entries;
    if (dir.empty() || !fs::exists(dir)) return entries;

    for (const auto& entry : fs::directory_iterator(dir)) {
        if (!entry.is_regular_file()) continue;
        auto ext = entry.path().extension().string();
        // Lower-case the extension before comparing
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (ext != ".mp4" && ext != ".mkv" && ext != ".webm") continue;

        VideoEntry ve;
        ve.path    = entry.path().string();
        ve.name    = entry.path().filename().string();
        ve.size    = FormatFileSize(entry.file_size());
        ve.modTime = entry.last_write_time();
        entries.push_back(ve);
    }

    // Newest first
    std::sort(entries.begin(), entries.end(), [](const VideoEntry& a, const VideoEntry& b) {
        return a.modTime > b.modTime;
    });
    return entries;
}
//  ImGui

void SetupStyle() {
    ImGuiStyle& style    = ImGui::GetStyle();
    style.WindowRounding = 8.0f;
    style.FrameRounding  = 6.0f;
    style.GrabRounding   = 6.0f;
    style.WindowPadding  = ImVec2(20, 20);
    style.ItemSpacing    = ImVec2(12, 12);

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_WindowBg]       = ImVec4(0.08f, 0.08f, 0.10f, 1.00f);
    colors[ImGuiCol_Text]           = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
    colors[ImGuiCol_Button]         = ImVec4(0.15f, 0.15f, 0.20f, 1.00f);
    colors[ImGuiCol_ButtonHovered]  = ImVec4(0.25f, 0.25f, 0.35f, 1.00f);
    colors[ImGuiCol_FrameBg]        = ImVec4(0.12f, 0.12f, 0.15f, 1.00f);
    colors[ImGuiCol_Tab]            = ImVec4(0.12f, 0.12f, 0.15f, 1.00f);
    colors[ImGuiCol_TabHovered]     = ImVec4(0.25f, 0.25f, 0.35f, 1.00f);
    colors[ImGuiCol_TabActive]      = ImVec4(0.20f, 0.20f, 0.28f, 1.00f);
    colors[ImGuiCol_Header]         = ImVec4(0.15f, 0.15f, 0.20f, 1.00f);
    colors[ImGuiCol_HeaderHovered]  = ImVec4(0.22f, 0.22f, 0.30f, 1.00f);
    colors[ImGuiCol_HeaderActive]   = ImVec4(0.25f, 0.25f, 0.35f, 1.00f);
}

//  Tab renderers

void RenderDownloaderTab(ImGuiIO& io, Downloader& downloader,
                         char* url, char* filename, char* cookiesPath,
                         bool& autoTimestamp, std::string& lastDownloadedFile,
                         const std::string& cookiesBrowser) {
    // Dependency
    static bool ytDlpInstalled = downloader.checkYtDlp();

    if (!ytDlpInstalled) {
        ImGui::BeginChild("Setup", ImVec2(0, 120), true);
        ImGui::Text("Core dependencies missing.");
        if (downloader.isDownloading()) {
            ImGui::Text("Downloading yt-dlp...");
        } else {
            if (ImGui::Button("Setup Core Locally", ImVec2(-1, 50))) {
                downloader.downloadYtDlp(nullptr, [&](bool success) { if (success) ytDlpInstalled = true; });
            }
        }
        ImGui::EndChild();
        ImGui::Dummy(ImVec2(0, 12));
    }

    //URL
    ImGui::Text("Video URL:");
    ImGui::PushItemWidth(-1);
    if (ImGui::InputText("##url", url, 1024)) {
        std::string stripped = StripTikTokURL(url);
        if (stripped != std::string(url)) strncpy(url, stripped.c_str(), 1024);
    }
    ImGui::PopItemWidth();

    ImGui::Dummy(ImVec2(0, 8));

    // Filename
    ImGui::Text("Save Filename:");
    if (autoTimestamp && !downloader.isDownloading()) {
        // Generate a timestamp-based name when auto mode is on
        snprintf(filename, 256, "tiktok_%ld", (long)std::time(nullptr));
    }

    ImGui::Columns(2, "filecolumns", false);
    ImGui::SetColumnWidth(0, io.DisplaySize.x * 0.70f);
    ImGui::PushItemWidth(-1);
    ImGui::InputText("##filename", filename, 256);
    ImGui::PopItemWidth();

    ImGui::NextColumn();
    ImGui::Checkbox("Auto-Time", &autoTimestamp);
    ImGui::Columns(1);

    ImGui::Dummy(ImVec2(0, 8));

    // Cookies (optional)
    ImGui::Text("Account Cookies (Optional):");
    ImGui::PushItemWidth(io.DisplaySize.x * 0.72f);
    ImGui::InputText("##cookies", cookiesPath, 512);
    ImGui::PopItemWidth();
    ImGui::SameLine();
    if (ImGui::Button("Browse##cookies", ImVec2(-1, 0))) {
        std::string picked = OpenFilePicker();
        if (!picked.empty()) strncpy(cookiesPath, picked.c_str(), 512);
    }

    ImGui::Dummy(ImVec2(0, 25));

    // Cookie source hint — TikTok now requires valid browser cookies to pass its
    // JS challenge; let the user know if nothing is configured
    if (std::string(cookiesPath).empty() && cookiesBrowser.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.2f, 1.0f),
                           "Tip: Set a cookie source in Settings to fix TikTok blocks.");
        ImGui::Dummy(ImVec2(0, 6));
    }

    // Download button or Success combo
    if (downloader.isDownloading()) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.2f, 0.2f, 1.0f));
        ImGui::Button("Processing...", ImVec2(-1, 60));
        ImGui::PopStyleColor();
    } else {
        if (!lastDownloadedFile.empty() && fs::exists(lastDownloadedFile)) {
            float btnW = (ImGui::GetContentRegionAvail().x - 12.0f) * 0.5f;
            if (ImGui::Button("Download Next", ImVec2(btnW, 60))) {
                std::string finalName = filename;
                if (finalName.find(".mp4") == std::string::npos) finalName += ".mp4";
                lastDownloadedFile = (fs::path(downloader.getDownloadDir()) / finalName).string();
                downloader.startDownload(url, filename, cookiesPath, cookiesBrowser, nullptr, nullptr);
            }
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.5f, 0.2f, 1.0f));
            if (ImGui::Button("Open Last Video", ImVec2(btnW, 60))) {
                ShellExecuteA(NULL, "open", lastDownloadedFile.c_str(), NULL, NULL, SW_SHOWNORMAL);
            }
            ImGui::PopStyleColor();
        } else {
            if (ImGui::Button("Download Video", ImVec2(-1, 60))) {
                std::string finalName = filename;
                if (finalName.find(".mp4") == std::string::npos) finalName += ".mp4";
                lastDownloadedFile = (fs::path(downloader.getDownloadDir()) / finalName).string();
                downloader.startDownload(url, filename, cookiesPath, cookiesBrowser, nullptr, nullptr);
            }
        }
    }

    // Activity logs
    ImGui::Dummy(ImVec2(0, 15));
    if (ImGui::CollapsingHeader("Activity Logs")) {
        float availableY = ImGui::GetContentRegionAvail().y;
        float reservedHeight = 70.0f; // Space for the persistent footer
        
        float logHeight = availableY - reservedHeight;
        if (logHeight < 150.0f) logHeight = 150.0f; // Minimum height
        
        ImGui::BeginChild("LogRegion", ImVec2(0, logHeight), true);
        for (const auto& log : downloader.getLogs()) {
            ImGui::TextUnformatted(log.c_str());
        }
        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) ImGui::SetScrollHereY(1.0f);
        ImGui::EndChild();
    }
}

void RenderMediaPlayerTab(ImGuiIO& io, const std::string& downloadDir, std::string& lastDownloadedFile, bool forceRefresh = false) {
    // Refresh button + scan
    static std::vector<VideoEntry> videos;
    static std::string scannedDir;
    static int selectedIdx = -1;

    // Re-scan whenever the dir changes or the user requests a refresh
    bool dirChanged = (scannedDir != downloadDir);
    if (dirChanged || forceRefresh) {
        videos     = ScanVideos(downloadDir);
        scannedDir = downloadDir;
        if (dirChanged || forceRefresh) selectedIdx = -1; // Only reset selection if forced
    }

    // Header row
    ImGui::Text("Media Library");
    ImGui::SameLine();
    ImGui::SetCursorPosX(io.DisplaySize.x - 120);
    if (ImGui::Button("Refresh##lib", ImVec2(100, 0))) {
        videos      = ScanVideos(downloadDir);
        scannedDir  = downloadDir;
        selectedIdx = -1;
    }

    ImGui::Dummy(ImVec2(0, 4));

    float panelHeight = io.DisplaySize.y - 260.0f; // Leave room for player + footer

    // File list
    float listWidth = io.DisplaySize.x * 0.45f - 30.0f;
    ImGui::BeginChild("VideoList", ImVec2(listWidth, panelHeight), true);

    if (videos.empty()) {
        ImGui::TextDisabled("No videos found in output folder.");
        ImGui::TextDisabled("%s", downloadDir.c_str());
    } else {
        for (int i = 0; i < (int)videos.size(); i++) {
            bool selected = (selectedIdx == i);
            // Truncate long names so they fit in the column
            std::string label = videos[i].name;
            if (label.size() > 36) label = label.substr(0, 33) + "...";

            ImGui::PushID(i);
            if (ImGui::Selectable(label.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns)) {
                selectedIdx = i;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s\n%s", videos[i].name.c_str(), videos[i].size.c_str());
            }
            ImGui::PopID();

            // File size hint on the right
            ImGui::SameLine(listWidth - 80.0f);
            ImGui::TextDisabled("%s", videos[i].size.c_str());
        }
    }
    ImGui::EndChild();

    // Preview / player panel
    ImGui::SameLine();
    ImGui::BeginChild("PlayerPanel", ImVec2(0, panelHeight), true);

    if (selectedIdx >= 0 && selectedIdx < (int)videos.size()) {
        const VideoEntry& v = videos[selectedIdx];
        
        static std::map<std::string, GLuint> thumbTextures;
        static std::map<std::string, int> thumbWidths;
        static std::map<std::string, int> thumbHeights;
        static std::string currentlyLoadingPath;
        
        std::string thumbPath = v.path + ".thumb.jpg";
        
        // If we haven't loaded it yet, check if file exists
        if (thumbTextures.find(v.path) == thumbTextures.end()) {
            if (fs::exists(thumbPath)) {
                int w = 0, h = 0;
                unsigned char* image_data = stbi_load(thumbPath.c_str(), &w, &h, NULL, 4);
                if (image_data != NULL) {
                    GLuint texture;
                    glGenTextures(1, &texture);
                    glBindTexture(GL_TEXTURE_2D, texture);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, image_data);
                    stbi_image_free(image_data);
                    thumbTextures[v.path] = texture;
                    thumbWidths[v.path] = w;
                    thumbHeights[v.path] = h;
                } else {
                    thumbTextures[v.path] = 0; // failed
                }
            } else if (currentlyLoadingPath != v.path) {
                currentlyLoadingPath = v.path;
                std::string cmdPath = v.path;
                std::thread([cmdPath, thumbPath]() {
                    // Extract a frame at 1 second using ffmpeg to a temporary file
                    std::string tempPath = thumbPath + ".tmp.jpg";
                    std::string cmd = ".\\ffmpeg.exe -v error -ss 00:00:01.00 -i \"" + cmdPath + "\" -frames:v 1 -q:v 2 \"" + tempPath + "\" -y";
                    int res = std::system(cmd.c_str());
                    if (res == 0 && fs::exists(tempPath)) {
                        try {
                            if (fs::exists(thumbPath)) fs::remove(thumbPath);
                            fs::rename(tempPath, thumbPath);
                        } catch(...) {}
                    }
                }).detach();
            }
        }

        float boxW = ImGui::GetContentRegionAvail().x;
        float boxH = 340.0f; // maximum preview height
        
        float drawW = boxW;
        float drawH = 200.0f; // default placeholder height
        
        GLuint tex = 0;
        if (thumbTextures.find(v.path) != thumbTextures.end()) {
            tex = thumbTextures[v.path];
        }

        if (tex != 0) {
            float imgW = (float)thumbWidths[v.path];
            float imgH = (float)thumbHeights[v.path];
            if (imgW > 0 && imgH > 0) {
                // Calculate scale to fit inside boxW x boxH
                float scale = std::min(boxW / imgW, boxH / imgH);
                drawW = imgW * scale;
                drawH = imgH * scale;
            }
        }

        ImVec2 cursorBase = ImGui::GetCursorScreenPos();
        // Center the image horizontally in the available space
        float offsetX = (boxW - drawW) * 0.5f;
        ImVec2 thumbPos = ImVec2(cursorBase.x + offsetX, cursorBase.y);

        if (tex != 0) {
            // Draw the loaded texture preserving aspect ratio
            ImGui::GetWindowDrawList()->AddImage(
                (void*)(intptr_t)tex,
                thumbPos,
                ImVec2(thumbPos.x + drawW, thumbPos.y + drawH)
            );
        } else {
            ImGui::GetWindowDrawList()->AddRectFilled(
                thumbPos,
                ImVec2(thumbPos.x + drawW, thumbPos.y + drawH),
                IM_COL32(20, 20, 28, 255),
                6.0f
            );
            const char* placeholderText = fs::exists(thumbPath) ? "[ Loading... ]" : "[ Generating Preview... ]";
            ImVec2 textSz = ImGui::CalcTextSize(placeholderText);
            ImGui::SetCursorScreenPos(ImVec2(
                thumbPos.x + (drawW  - textSz.x) * 0.5f,
                thumbPos.y + (drawH - textSz.y) * 0.5f
            ));
            ImGui::TextDisabled("%s", placeholderText);
        }

        // Advance the cursor past the thumbnail box
        ImGui::SetCursorScreenPos(ImVec2(cursorBase.x, cursorBase.y + drawH + 10.0f));

        // Metadata
        ImGui::TextWrapped("%s", v.name.c_str());
        ImGui::Dummy(ImVec2(0, 4));
        ImGui::TextDisabled("Size: %s", v.size.c_str());
        ImGui::TextDisabled("Path: %s", v.path.c_str());

        ImGui::Dummy(ImVec2(0, 12));

        // Action buttons
        float btnW = (ImGui::GetContentRegionAvail().x - 12.0f) * 0.5f;

        if (ImGui::Button("Play##open", ImVec2(btnW, 42))) {
            // Open with whatever the system default video player is
            ShellExecuteA(NULL, "open", v.path.c_str(), NULL, NULL, SW_SHOWNORMAL);
            lastDownloadedFile = v.path;
        }
        ImGui::SameLine();
        if (ImGui::Button("Show in Explorer##exp", ImVec2(btnW, 42))) {
            // /select highlights the file inside Explorer
            std::string arg = "/select,\"" + v.path + "\"";
            ShellExecuteA(NULL, "open", "explorer.exe", arg.c_str(), NULL, SW_SHOWNORMAL);
        }

        ImGui::Dummy(ImVec2(0, 8));

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.5f, 0.15f, 0.15f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.7f, 0.2f, 0.2f, 1.0f));
        if (ImGui::Button("Delete##del", ImVec2(-1, 36))) {
            // Ask for confirmation before permanently removing the file
            ImGui::OpenPopup("Confirm Delete");
        }
        ImGui::PopStyleColor(2);

        // Confirmation modal
        if (ImGui::BeginPopupModal("Confirm Delete", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Permanently delete:");
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "%s", v.name.c_str());
            ImGui::Spacing();
            if (ImGui::Button("Yes, Delete", ImVec2(120, 0))) {
                fs::remove(v.path);
                // Refresh list and clear selection
                videos      = ScanVideos(downloadDir);
                selectedIdx = -1;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0))) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

    } else {
        // Nothing selected — show a gentle prompt
        float h = ImGui::GetContentRegionAvail().y;
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + h * 0.4f);
        float w = ImGui::CalcTextSize("Select a video from the list").x;
        ImGui::SetCursorPosX((ImGui::GetContentRegionAvail().x - w) * 0.5f);
        ImGui::TextDisabled("Select a video from the list");
    }

    ImGui::EndChild();
}

void RenderSettingsTab(ImGuiIO& io, Downloader& downloader,
                       char* downloadDirBuf, int bufSize,
                       std::string& cookiesBrowser) {
    // Output folder
    ImGui::Text("Output Folder");
    ImGui::Dummy(ImVec2(0, 4));

    // Keep the buffer in sync with the downloader's current dir
    if (std::string(downloadDirBuf) != downloader.getDownloadDir()) {
        strncpy(downloadDirBuf, downloader.getDownloadDir().c_str(), bufSize - 1);
        downloadDirBuf[bufSize - 1] = '\0';
    }

    ImGui::PushItemWidth(io.DisplaySize.x * 0.72f);
    if (ImGui::InputText("##outdir", downloadDirBuf, bufSize)) {
        // Apply typed path immediately if it actually exists
        if (fs::exists(downloadDirBuf)) {
            downloader.setDownloadDir(downloadDirBuf);
        }
    }
    ImGui::PopItemWidth();
    ImGui::SameLine();
    if (ImGui::Button("Browse##dir", ImVec2(-1, 0))) {
        std::string picked = OpenFolderPicker();
        if (!picked.empty()) {
            strncpy(downloadDirBuf, picked.c_str(), bufSize - 1);
            downloadDirBuf[bufSize - 1] = '\0';
            downloader.setDownloadDir(picked);
        }
    }

    ImGui::Dummy(ImVec2(0, 8));

    // Reset to Videos\ttdownloads
    if (ImGui::Button("Reset to Default (Videos\\ttdownloads)", ImVec2(320, 0))) {
        std::string def = Downloader::resolveDefaultDownloadDir();
        strncpy(downloadDirBuf, def.c_str(), bufSize - 1);
        downloadDirBuf[bufSize - 1] = '\0';
        downloader.setDownloadDir(def);
    }

    ImGui::Dummy(ImVec2(0, 4));
    if (!fs::exists(downloadDirBuf)) {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Warning: folder does not exist.");
    } else {
        ImGui::TextDisabled("Folder is valid.");
    }

    ImGui::Dummy(ImVec2(0, 24));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, 12));

    // Cookie source
    // TikTok's web extractor now requires valid session cookies to pass its
    // JS challenge. Without them, every download fails. The options are:
    //   1. Let yt-dlp pull cookies live from an installed browser (easiest)
    //   2. Export a cookies.txt file from the browser and point to it in the
    //      Downloader tab (more portable, works without the browser open)
    ImGui::Text("Cookie Source  (required for TikTok)");
    ImGui::Dummy(ImVec2(0, 4));

    // Map display names to the argument yt-dlp expects
    static const char* browserLabels[] = { "None (cookies.txt only)", "Chrome", "Firefox", "Edge", "Brave", "Opera" };
    static const char* browserArgs[]   = { "",        "chrome",   "firefox",  "edge",   "brave",  "opera"  };
    static int browserIdx = 0;

    // Sync combo index to the current cookiesBrowser string on first render
    static bool syncedOnce = false;
    if (!syncedOnce) {
        for (int i = 0; i < 6; i++) {
            if (cookiesBrowser == browserArgs[i]) { browserIdx = i; break; }
        }
        syncedOnce = true;
    }

    ImGui::PushItemWidth(260);
    if (ImGui::Combo("##browser", &browserIdx, browserLabels, 6)) {
        cookiesBrowser = browserArgs[browserIdx];
    }
    ImGui::PopItemWidth();

    ImGui::Dummy(ImVec2(0, 6));

    if (browserIdx == 0) {
        ImGui::TextDisabled("No browser selected — use a cookies.txt in the Downloader tab.");
    } else {
        ImGui::TextDisabled("yt-dlp will read live cookies from %s.", browserLabels[browserIdx]);
        ImGui::TextDisabled("The browser does not need to be running.");
        ImGui::Dummy(ImVec2(0, 4));
        // Warn that Chrome 127+ uses App-Bound Encryption which breaks DPAPI extraction
        if (browserIdx == 1) {
            ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.2f, 1.0f),
                               "Chrome 127+ may fail due to App-Bound Encryption.");
            ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.2f, 1.0f),
                               "Use Firefox or export cookies.txt if it does.");
        }
    }

    ImGui::Dummy(ImVec2(0, 20));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, 10));
    ImGui::TextDisabled("Supported formats: .mp4  .mkv  .webm");
    ImGui::TextDisabled("Media player uses your system default application.");
}

//  Entry point

int main() {
    if (!glfwInit()) return -1;

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(940, 820, "TikTok Downloader", NULL, NULL);
    if (!window) {
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);
    gladLoadGL();

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();

    // Font size globally
    io.FontGlobalScale = 1.35f;

    SetupStyle();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    // State
    Downloader downloader;
    downloader.updateYtDlp(); // Auto-update check on startup

    AppConfig config = LoadConfig();
    if (!config.downloadDir.empty()) downloader.setDownloadDir(config.downloadDir);

    char url[1024]          = "";
    char filename[256]      = "";
    char cookiesPath[512]   = "";
    if (!config.cookiesPath.empty()) strncpy(cookiesPath, config.cookiesPath.c_str(), 511);

    char downloadDirBuf[512] = "";
    bool autoTimestamp      = true;
    std::string lastDownloadedFile = "";
    std::string cookiesBrowser = config.cookiesBrowser;

    // Prime the dir buffer from the downloader's resolved default
    strncpy(downloadDirBuf, downloader.getDownloadDir().c_str(), sizeof(downloadDirBuf) - 1);

    bool showLoading   = true;
    float loadingTimer = 0.0f;

    // Main loop
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("Root", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove);

        if (showLoading) {
            // loading screen
            loadingTimer += io.DeltaTime;
            ImVec2 windowSize = io.DisplaySize;

            ImGui::SetCursorPosY(windowSize.y * 0.35f);

            // Title
            ImGui::SetWindowFontScale(2.2f);
            float textWidth = ImGui::CalcTextSize("TikTok Downloader").x;
            ImGui::SetCursorPosX((windowSize.x - textWidth) * 0.5f);
            ImGui::Text("TikTok Downloader");
            ImGui::SetWindowFontScale(1.0f);

            ImGui::Dummy(ImVec2(0, 15));

            // Watermark
            float subTextWidth = ImGui::CalcTextSize("made by CLPdevOfficial").x;
            ImGui::SetCursorPosX((windowSize.x - subTextWidth) * 0.5f);
            ImGui::TextDisabled("made by CLPdevOfficial");

            // GitHub link at the bottom during loading
            ImGui::SetCursorPosY(windowSize.y - 70.0f);
            float githubWidth = ImGui::CalcTextSize("github.com/CLPdevOfficial").x;
            ImGui::SetCursorPosX((windowSize.x - githubWidth) * 0.5f);
            ImGui::TextDisabled("github.com/CLPdevOfficial");

            if (loadingTimer > 1.0f) showLoading = false;

        } else {
            // Main UI

            // Top bar: app title + author tag
            ImGui::SetWindowFontScale(1.1f);
            ImGui::Text("TikTok Downloader");
            ImGui::SameLine();
            
            // Show update status
            std::string upStatus = downloader.getUpdateStatus();
            float statusWidth = ImGui::CalcTextSize(upStatus.c_str()).x;
            ImGui::SetCursorPosX((io.DisplaySize.x - statusWidth) * 0.5f);
            if (downloader.isUpdating()) {
                ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "%s", upStatus.c_str());
            } else {
                ImGui::TextDisabled("%s", upStatus.c_str());
            }

            ImGui::SameLine();
            ImGui::SetCursorPosX(io.DisplaySize.x - 280);
            ImGui::TextDisabled("by CLPdevOfficial"); // Static text, no redirect
            ImGui::SetWindowFontScale(1.0f);

            ImGui::Separator();
            ImGui::Dummy(ImVec2(0, 8));

            // Tab bar
            if (ImGui::BeginTabBar("MainTabs")) {

                // Downloader tab
                if (ImGui::BeginTabItem("Downloader")) {
                    ImGui::Dummy(ImVec2(0, 8));
                    RenderDownloaderTab(io, downloader, url, filename, cookiesPath, autoTimestamp, lastDownloadedFile, cookiesBrowser);
                    ImGui::EndTabItem();
                }

                // Media Player tab
                static bool wasMediaPlayerTab = false;
                bool isMediaPlayerTab = ImGui::BeginTabItem("Media Player");
                if (isMediaPlayerTab) {
                    ImGui::Dummy(ImVec2(0, 8));
                    bool justEntered = !wasMediaPlayerTab;
                    RenderMediaPlayerTab(io, downloader.getDownloadDir(), lastDownloadedFile, justEntered);
                    ImGui::EndTabItem();
                }
                wasMediaPlayerTab = isMediaPlayerTab;

                // Settings tab
                if (ImGui::BeginTabItem("Settings")) {
                    ImGui::Dummy(ImVec2(0, 8));
                    RenderSettingsTab(io, downloader, downloadDirBuf, sizeof(downloadDirBuf), cookiesBrowser);
                    ImGui::EndTabItem();
                }

                ImGui::EndTabBar();
            }

            // Persistent footer
            ImGui::SetCursorPosY(io.DisplaySize.y - 50.0f);
            ImGui::Separator();
            float githubWidth = ImGui::CalcTextSize("github.com/CLPdevOfficial").x;
            ImGui::SetCursorPosX((io.DisplaySize.x - githubWidth) * 0.5f);
            if (ImGui::Selectable("github.com/CLPdevOfficial", false, 0,
                                   ImGui::CalcTextSize("github.com/CLPdevOfficial"))) {
                ShellExecuteA(NULL, "open", "https://github.com/CLPdevOfficial", NULL, NULL, SW_SHOWNORMAL);
            }
            if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        }

        ImGui::End();

        ImGui::Render();
        glClearColor(0.08f, 0.08f, 0.10f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }
    
    // Save configuration before closing
    AppConfig newConfig;
    newConfig.downloadDir = downloader.getDownloadDir();
    newConfig.cookiesBrowser = cookiesBrowser;
    newConfig.cookiesPath = std::string(cookiesPath);
    SaveConfig(newConfig);

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
