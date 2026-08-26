#include "Downloader.hpp"
#include <iostream>
#include <fstream>
#include <filesystem>
#include <cstdio>
#include <array>
#include <regex>
#include <shlobj.h>
#include <windows.h>

namespace fs = std::filesystem;

Downloader::Downloader() {
    status = "Idle";
    downloadDir = resolveDefaultDownloadDir();
}

Downloader::~Downloader() {
    if (workerThread.joinable()) {
        workerThread.detach();
    }
    if (updateThread.joinable()) {
        updateThread.detach();
    }
}

void Downloader::addLog(const std::string& log) {
    logs.push_back(log);
    if (logs.size() > 500) logs.erase(logs.begin());
}

// Returns Videos\ttdownloads as the default output folder, creating it if needed
std::string Downloader::resolveDefaultDownloadDir() {
    char path[MAX_PATH];
    if (SHGetFolderPathA(NULL, CSIDL_MYVIDEO, NULL, 0, path) == S_OK) {
        fs::path outPath = fs::path(path) / "ttdownloads";
        if (!fs::exists(outPath)) {
            fs::create_directories(outPath);
        }
        return outPath.string();
    }
    return ".";
}

std::string Downloader::getYtDlpPath() {
    if (!cachedYtDlpPath.empty() && fs::exists(cachedYtDlpPath)) return cachedYtDlpPath;

    // Check current directory for executable
    if (fs::exists("yt-dlp.exe")) {
        cachedYtDlpPath = "yt-dlp.exe";
        return cachedYtDlpPath;
    }

    // Check if yt-dlp is installed in PATH
    int res = std::system("yt-dlp --version > nul 2>&1");
    if (res == 0) {
        cachedYtDlpPath = "yt-dlp"; // Just use the command instead of the direct executable
        return cachedYtDlpPath;
    }

    return "";
}

bool Downloader::checkYtDlp() {
    return !getYtDlpPath().empty();
}

void Downloader::updateYtDlp() {
    if (updating) return;
    std::string exePath = getYtDlpPath();
    if (exePath.empty()) {
        updateStatus = "yt-dlp not found.";
        return;
    }
    
    updating = true;
    updateStatus = "Checking for updates...";
    
    updateThread = std::thread([this, exePath]() {
        // Run yt-dlp.exe --update-to stable
        std::string cmd = exePath + " --update-to stable 2>&1";
        std::array<char, 512> buffer;
        FILE* pipe = _popen(cmd.c_str(), "r");
        if (!pipe) {
            updateStatus = "Update launch failed.";
            updating = false;
            return;
        }

        bool updated = false;
        bool uptodate = false;
        
        while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
            std::string line(buffer.data());
            if (line.find("up to date") != std::string::npos) {
                uptodate = true;
            } else if (line.find("Updated yt-dlp to") != std::string::npos) {
                updated = true;
            }
        }
        
        _pclose(pipe);
        
        if (updated) {
            updateStatus = "On the latest release";
        } else if (uptodate) {
            updateStatus = "On the latest release";
        } else {
            updateStatus = "Update check finished";
        }
        updating = false;
    });
    updateThread.detach();
}

void Downloader::downloadYtDlp(std::function<void(float)> progressCallback, std::function<void(bool)> finishCallback) {
    if (downloading) return;
    downloading = true;
    status = "Downloading yt-dlp...";
    addLog("Initiating yt-dlp download via PowerShell...");

    workerThread = std::thread([this, progressCallback, finishCallback]() {
        // Use PowerShell for better reliability
        std::string cmd = "powershell -Command \"[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12; Invoke-WebRequest -Uri 'https://github.com/yt-dlp/yt-dlp/releases/latest/download/yt-dlp.exe' -OutFile 'yt-dlp.exe'\"";
        int result = std::system(cmd.c_str());
        
        downloading = false;
        if (result == 0 && fs::exists("yt-dlp.exe")) {
            cachedYtDlpPath = "yt-dlp.exe";
            status = "yt-dlp installed locally.";
            addLog("yt-dlp.exe downloaded successfully to local directory.");
            if (finishCallback) finishCallback(true);
        } else {
            status = "Failed to download yt-dlp.";
            addLog("ERROR: PowerShell download failed with code " + std::to_string(result));
            if (finishCallback) finishCallback(false);
        }
    });
    workerThread.detach();
}

void Downloader::startDownload(const std::string& url, const std::string& filename,
                               const std::string& cookiesPath, const std::string& cookiesBrowser,
                               std::function<void(float)> progressCallback,
                               std::function<void(bool, std::string)> finishCallback) {
    if (downloading) return;
    
    std::string exePath = getYtDlpPath();
    if (exePath.empty()) {
        status = "Error: yt-dlp not found.";
        addLog("ERROR: yt-dlp not found in PATH or local folder.");
        return;
    }

    downloading = true;
    progress = 0.0f;
    status = "Initializing...";
    clearLogs();
    addLog("Using yt-dlp path: " + exePath);

    std::string finalName = filename;
    if (finalName.empty()) {
        finalName = "tiktok_" + std::to_string(std::time(nullptr));
    }
    if (finalName.find(".mp4") == std::string::npos) {
        finalName += ".mp4";
    }

    fs::path savePath = fs::path(downloadDir) / finalName;
    addLog("Saving to: " + savePath.string());

    // Build the yt-dlp command
    std::string cmd = exePath + " ";

    // Impersonate a real Chrome TLS fingerprint via curl_cffi — this is the
    // primary fix for TikTok's JS challenge blocking desktop requests
    cmd += "--impersonate chrome ";

    // Prefer a Netscape cookies.txt file if the user supplied one
    if (!cookiesPath.empty() && fs::exists(cookiesPath)) {
        cmd += "--cookies \"" + cookiesPath + "\" ";
        addLog("Using cookies file: " + cookiesPath);
    }
    // Otherwise fall back to live extraction from the chosen browser
    else if (!cookiesBrowser.empty()) {
        cmd += "--cookies-from-browser " + cookiesBrowser + " ";
        addLog("Extracting live cookies from browser: " + cookiesBrowser);
    }

    cmd += "-v "; // Verbose output for the activity log
    cmd += "-o \"" + savePath.string() + "\" ";
    cmd += "\"" + url + "\"";

    workerThread = std::thread(&Downloader::runYtDlp, this, cmd, progressCallback, finishCallback);
    workerThread.detach();
}

void Downloader::runYtDlp(std::string cmd, std::function<void(float)> progressCallback, std::function<void(bool, std::string)> finishCallback) {
    std::array<char, 512> buffer;
    
    addLog("Executing command: " + cmd);
    cmd += " 2>&1"; // Merge stderr to stdout
    
    FILE* pipe = _popen(cmd.c_str(), "r");
    if (!pipe) {
        downloading = false;
        status = "Launch Failed";
        addLog("ERROR: Failed to open pipe for command execution.");
        if (finishCallback) finishCallback(false, "");
        return;
    }

    std::regex progressRegex(R"(\[download\]\s+(\d+\.?\d*)%)");
    std::smatch match;

    while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
        std::string line(buffer.data());
        // Remove trailing newline
        line.erase(line.find_last_not_of(" \n\r\t") + 1);
        
        addLog(line);

        if (std::regex_search(line, match, progressRegex)) {
            progress = std::stof(match[1]) / 100.0f;
            status = "Downloading... " + match[1].str() + "%";
            if (progressCallback) progressCallback(progress);
        }
    }

    int returnCode = _pclose(pipe);
    downloading = false;
    
    if (returnCode == 0) {
        status = "Success!";
        progress = 1.0f;
        addLog("Process finished successfully (code 0).");
        if (finishCallback) finishCallback(true, "");
    } else {
        status = "Failed (Code " + std::to_string(returnCode) + ")";
        addLog("ERROR: yt-dlp exited with non-zero code: " + std::to_string(returnCode));
        if (finishCallback) finishCallback(false, "");
    }
}
