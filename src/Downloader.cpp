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

// ---------------------------------------------------------------------------
// Win32 helpers — launch child processes with CREATE_NO_WINDOW so no console
// window ever flashes on screen.
// ---------------------------------------------------------------------------

// Fire-and-forget: run `cmd` invisibly through cmd.exe /C and return the
// process exit code (-1 on failure to launch).
int Downloader::runSilent(const std::string& cmd) {
    std::string cmdLine = "cmd.exe /C " + cmd;

    STARTUPINFOA        si{};
    PROCESS_INFORMATION pi{};
    si.cb = sizeof(si);
    si.dwFlags    = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    if (!CreateProcessA(nullptr, cmdLine.data(), nullptr, nullptr,
                        FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        return -1;
    }

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return static_cast<int>(exitCode);
}

// Run `cmd` invisibly through cmd.exe /C, pipe stdout+stderr back, and call
// `lineCallback` for every line of output.  Returns the process exit code.
int Downloader::runSilentWithOutput(const std::string& cmd,
                                    std::function<void(const std::string&)> lineCallback) {
    // Create an anonymous pipe for the child's stdout+stderr.
    SECURITY_ATTRIBUTES sa{};
    sa.nLength        = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE hReadPipe  = nullptr;
    HANDLE hWritePipe = nullptr;
    if (!CreatePipe(&hReadPipe, &hWritePipe, &sa, 0)) return -1;

    // The read end should NOT be inherited by the child.
    SetHandleInformation(hReadPipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA        si{};
    PROCESS_INFORMATION pi{};
    si.cb          = sizeof(si);
    si.dwFlags     = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput  = hWritePipe;
    si.hStdError   = hWritePipe;

    // A GUI-subsystem app has no console stdin, so hand the child NUL instead.
    // (Also stops yt-dlp from ever waiting on input.)
    HANDLE hNul = CreateFileA("NUL", GENERIC_READ,
                              FILE_SHARE_READ | FILE_SHARE_WRITE,
                              &sa, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    si.hStdInput   = hNul;

    // Wrap the command through cmd.exe /C so shell features (redirects, etc.)
    // keep working, while CREATE_NO_WINDOW suppresses any window.
    std::string cmdLine = "cmd.exe /C " + cmd;

    if (!CreateProcessA(nullptr, cmdLine.data(), nullptr, nullptr,
                        TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        if (hNul != INVALID_HANDLE_VALUE) CloseHandle(hNul);
        CloseHandle(hReadPipe);
        CloseHandle(hWritePipe);
        return -1;
    }

    // Close the write end in the parent — otherwise ReadFile will never see
    // EOF.
    CloseHandle(hWritePipe);
    if (hNul != INVALID_HANDLE_VALUE) CloseHandle(hNul);

    // Read stdout line-by-line.
    std::string accumulated;
    char buf[512];
    DWORD bytesRead = 0;
    while (ReadFile(hReadPipe, buf, sizeof(buf) - 1, &bytesRead, nullptr) && bytesRead > 0) {
        buf[bytesRead] = '\0';
        accumulated += buf;

        // Deliver complete lines to the callback.
        std::string::size_type pos;
        while ((pos = accumulated.find('\n')) != std::string::npos) {
            std::string line = accumulated.substr(0, pos);
            // Trim trailing \r if present.
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (lineCallback) lineCallback(line);
            accumulated.erase(0, pos + 1);
        }
    }
    // Flush any remaining partial line.
    if (!accumulated.empty()) {
        if (!accumulated.empty() && accumulated.back() == '\r') accumulated.pop_back();
        if (lineCallback) lineCallback(accumulated);
    }

    CloseHandle(hReadPipe);

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return static_cast<int>(exitCode);
}

// ---------------------------------------------------------------------------

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
    std::lock_guard<std::mutex> lock(pathMutex);
    if (pathChecked) return cachedYtDlpPath;

    // 1. Check current directory
    if (fs::exists("yt-dlp.exe")) {
        cachedYtDlpPath = "yt-dlp.exe";
        pathChecked = true;
        return cachedYtDlpPath;
    }

    // 2. Check next to application binary or parent directory (e.g. running from build folder)
    char exeBuf[MAX_PATH];
    if (GetModuleFileNameA(NULL, exeBuf, MAX_PATH) > 0) {
        fs::path exeDir = fs::path(exeBuf).parent_path();
        if (fs::exists(exeDir / "yt-dlp.exe")) {
            cachedYtDlpPath = (exeDir / "yt-dlp.exe").string();
            pathChecked = true;
            return cachedYtDlpPath;
        }
        if (fs::exists(exeDir.parent_path() / "yt-dlp.exe")) {
            cachedYtDlpPath = (exeDir.parent_path() / "yt-dlp.exe").string();
            pathChecked = true;
            return cachedYtDlpPath;
        }
    }

    // 3. Search system PATH using Win32 API (instant < 0.1ms, no cmd/python process spawned)
    char foundPath[MAX_PATH];
    DWORD len = SearchPathA(NULL, "yt-dlp.exe", NULL, MAX_PATH, foundPath, NULL);
    if (len > 0 && len < MAX_PATH) {
        cachedYtDlpPath = std::string(foundPath);
        pathChecked = true;
        return cachedYtDlpPath;
    }

    pathChecked = true;
    return "";
}

bool Downloader::checkYtDlp() {
    return !getYtDlpPath().empty();
}

void Downloader::updateYtDlp() {
    if (updating) return;

    updating = true;
    updateStatus = "Checking for updates...";

    updateThread = std::thread([this]() {
        std::string exePath = getYtDlpPath();
        if (exePath.empty()) {
            updateStatus = "yt-dlp not found.";
            updating = false;
            return;
        }

        // Run yt-dlp.exe --update-to stable
        std::string cmd = "\"" + exePath + "\" --update-to stable 2>&1";

        bool updated  = false;
        bool uptodate = false;

        runSilentWithOutput(cmd, [&](const std::string& line) {
            if (line.find("up to date") != std::string::npos) {
                uptodate = true;
            } else if (line.find("Updated yt-dlp to") != std::string::npos) {
                updated = true;
            }
        });

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
        // Use PowerShell for better reliability — run silently via CreateProcess
        std::string cmd = "powershell -Command \"[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12; Invoke-WebRequest -Uri 'https://github.com/yt-dlp/yt-dlp/releases/latest/download/yt-dlp.exe' -OutFile 'yt-dlp.exe'\"";
        int result = runSilent(cmd);

        downloading = false;
        if (result == 0 && fs::exists("yt-dlp.exe")) {
            {
                std::lock_guard<std::mutex> lock(pathMutex);
                cachedYtDlpPath = "yt-dlp.exe";
                pathChecked = true;
            }
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
    addLog("Executing command: " + cmd);
    cmd += " 2>&1"; // Merge stderr to stdout

    std::regex progressRegex(R"(\[download\]\s+(\d+\.?\d*)%)");

    int returnCode = runSilentWithOutput(cmd, [&](const std::string& line) {
        addLog(line);

        std::smatch match;
        if (std::regex_search(line, match, progressRegex)) {
            progress = std::stof(match[1]) / 100.0f;
            status = "Downloading... " + match[1].str() + "%";
            if (progressCallback) progressCallback(progress);
        }
    });

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