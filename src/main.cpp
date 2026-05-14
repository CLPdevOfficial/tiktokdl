#include <iostream>
#include <vector>
#include <string>
#include <ctime>
#include <filesystem>
#include <regex>
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <windows.h>
#include <commdlg.h>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "Downloader.hpp"

namespace fs = std::filesystem;

// Native Win32 File Picker
std::string OpenFilePicker() {
    OPENFILENAMEA ofn;
    char szFile[260] = {0};
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = NULL;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile);
    ofn.lpstrFilter = "Text Files (*.txt)\0*.txt\0All Files (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrFileTitle = NULL;
    ofn.nMaxFileTitle = 0;
    ofn.lpstrInitialDir = NULL;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

    if (GetOpenFileNameA(&ofn) == TRUE) {
        return std::string(szFile);
    }
    return "";
}

// Strip for video id
std::string StripTikTokURL(std::string url) {
    std::regex baseRegex(R"(^https?:\/\/(?:vm\.|vt\.|www\.)?tiktok\.com\/[^\?]+)");
    std::smatch match;
    if (std::regex_search(url, match, baseRegex)) {
        return match[0].str();
    }
    return url;
}

void SetupStyle() {
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 8.0f;
    style.FrameRounding = 6.0f;
    style.GrabRounding = 6.0f;
    style.WindowPadding = ImVec2(20, 20);
    style.ItemSpacing = ImVec2(12, 12);
    
    ImVec4* colors = style.Colors;
    colors[ImGuiCol_WindowBg] = ImVec4(0.08f, 0.08f, 0.10f, 1.00f);
    colors[ImGuiCol_Text] = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
    colors[ImGuiCol_Button] = ImVec4(0.15f, 0.15f, 0.20f, 1.00f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.25f, 0.25f, 0.35f, 1.00f);
    colors[ImGuiCol_FrameBg] = ImVec4(0.12f, 0.12f, 0.15f, 1.00f);
}

int main() {
    if (!glfwInit()) return -1;

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(900, 800, "TikTok Downloader", NULL, NULL);
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

    Downloader downloader;
    char url[1024] = "";
    char filename[256] = "";
    char cookiesPath[512] = "";
    bool autoTimestamp = true;
    std::string lastDownloadedFile = "";
    std::string downloadDir = downloader.getDownloadDir();
    
    bool showLoading = true;
    float loadingTimer = 0.0f;

    bool ytDlpInstalled = downloader.checkYtDlp();

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("Root", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove);

        if (showLoading) {
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
            
            // GitHub at Bottom on Loading
            ImGui::SetCursorPosY(windowSize.y - 70.0f);
            float githubWidth = ImGui::CalcTextSize("github.com/CLPdevOfficial").x;
            ImGui::SetCursorPosX((windowSize.x - githubWidth) * 0.5f);
            ImGui::TextDisabled("github.com/CLPdevOfficial");

            if (loadingTimer > 3.0f) showLoading = false;
        } else {
            // Main UI
            ImGui::SetWindowFontScale(1.1f);
            ImGui::Text("TikTok Downloader");
            ImGui::SameLine();
            ImGui::SetCursorPosX(io.DisplaySize.x - 280);
            ImGui::TextDisabled("by CLPdevOfficial"); // Static text, no redirect
            ImGui::SetWindowFontScale(1.0f);

            ImGui::Separator();
            ImGui::Dummy(ImVec2(0, 15));

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
            }

            // URL
            ImGui::Text("Video URL:");
            ImGui::PushItemWidth(-1);
            if (ImGui::InputText("##url", url, sizeof(url))) {
                std::string stripped = StripTikTokURL(url);
                if (stripped != url) strncpy(url, stripped.c_str(), sizeof(url));
            }
            ImGui::PopItemWidth();

            ImGui::Dummy(ImVec2(0, 8));

            // Filename for downloaded vid
            ImGui::Text("Save Filename:");
            if (autoTimestamp && !downloader.isDownloading()) {
                snprintf(filename, sizeof(filename), "tiktok_%ld", (long)std::time(nullptr));
            }
            
            ImGui::Columns(2, "filecolumns", false);
            ImGui::SetColumnWidth(0, io.DisplaySize.x * 0.7f);
            ImGui::PushItemWidth(-1);
            ImGui::InputText("##filename", filename, sizeof(filename));
            ImGui::PopItemWidth();
            
            ImGui::NextColumn();
            ImGui::Checkbox("Auto-Time", &autoTimestamp);
            ImGui::Columns(1);

            ImGui::Dummy(ImVec2(0, 8));

            // Cookies file
            ImGui::Text("Account Cookies (Optional):");
            ImGui::PushItemWidth(io.DisplaySize.x * 0.72f);
            ImGui::InputText("##cookies", cookiesPath, sizeof(cookiesPath));
            ImGui::PopItemWidth();
            ImGui::SameLine();
            if (ImGui::Button("Browse", ImVec2(-1, 0))) {
                std::string picked = OpenFilePicker();
                if (!picked.empty()) strncpy(cookiesPath, picked.c_str(), sizeof(cookiesPath));
            }

            ImGui::Dummy(ImVec2(0, 25));

            // Download button
            if (downloader.isDownloading()) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.2f, 0.2f, 1.0f));
                ImGui::Button("Processing...", ImVec2(-1, 60));
                ImGui::PopStyleColor();
            } else {
                if (ImGui::Button("Download Video", ImVec2(-1, 60))) {
                    std::string finalName = filename;
                    if (finalName.find(".mp4") == std::string::npos) finalName += ".mp4";
                    lastDownloadedFile = (fs::path(downloadDir) / finalName).string();
                    downloader.startDownload(url, filename, cookiesPath, nullptr, nullptr);
                }
            }

            // Statue
            // ImGui::Dummy(ImVec2(0, 15));
            // std::string status = downloader.getStatus();
            // float statusWidth = ImGui::CalcTextSize(status.c_str()).x;
            // ImGui::SetCursorPosX((io.DisplaySize.x - statusWidth) * 0.5f);
            // ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "%s", status.c_str());

            // Logs
            ImGui::Dummy(ImVec2(0, 15));
            if (ImGui::CollapsingHeader("Activity Logs")) {
                ImGui::BeginChild("LogRegion", ImVec2(0, 150), true);
                for (const auto& log : downloader.getLogs()) {
                    ImGui::TextUnformatted(log.c_str());
                }
                if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) ImGui::SetScrollHereY(1.0f);
                ImGui::EndChild();
            }

            // Success / Open
            if (!lastDownloadedFile.empty() && fs::exists(lastDownloadedFile) && !downloader.isDownloading()) {
                ImGui::Dummy(ImVec2(0, 25));
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.5f, 0.2f, 1.0f));
                if (ImGui::Button("Open Video in Player", ImVec2(-1, 55))) {
                    ShellExecuteA(NULL, "open", lastDownloadedFile.c_str(), NULL, NULL, SW_SHOWNORMAL);
                }
                ImGui::PopStyleColor();
                
                float successWidth = ImGui::CalcTextSize("Saved to TikTok_Downloads").x;
                ImGui::SetCursorPosX((io.DisplaySize.x - successWidth) * 0.5f);
                ImGui::TextDisabled("Location: TikTok_Downloads");
            }

            // Watermark
            ImGui::SetCursorPosY(io.DisplaySize.y - 50.0f);
            ImGui::Separator();
            float githubWidth = ImGui::CalcTextSize("github.com/CLPdevOfficial").x;
            ImGui::SetCursorPosX((io.DisplaySize.x - githubWidth) * 0.5f);
            if (ImGui::Selectable("github.com/CLPdevOfficial", false, 0, ImGui::CalcTextSize("github.com/CLPdevOfficial"))) {
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

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
