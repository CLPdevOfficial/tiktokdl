#pragma once
#include <string>
#include <atomic>
#include <thread>
#include <vector>
#include <functional>

#include <mutex>

class Downloader {
public:
    Downloader();
    ~Downloader();

    bool checkYtDlp();
    std::string getYtDlpPath();
    void downloadYtDlp(std::function<void(float)> progressCallback, std::function<void(bool)> finishCallback);
    
    // cookiesPath = path to a Netscape cookies.txt file (optional)
    // cookiesBrowser = one of: "" (none), "chrome", "firefox", "edge" — pulls cookies live from the browser
    void startDownload(const std::string& url, const std::string& filename,
                       const std::string& cookiesPath, const std::string& cookiesBrowser,
                       std::function<void(float)> progressCallback,
                       std::function<void(bool, std::string)> finishCallback);

    bool isDownloading() const { return downloading; }
    float getProgress() const { return progress; }
    std::string getStatus() const { return status; }

    // Update methods
    void updateYtDlp();
    bool isUpdating() const { return updating; }
    std::string getUpdateStatus() const { return updateStatus; }

    // Download dir can be set at runtime from the settings panel
    std::string getDownloadDir() const { return downloadDir; }
    void setDownloadDir(const std::string& dir) { downloadDir = dir; }

    // Resolves Videos\ttdownloads as the default output folder
    static std::string resolveDefaultDownloadDir();
    
    const std::vector<std::string>& getLogs() const { return logs; }
    void clearLogs() { logs.clear(); }

private:
    std::atomic<bool> downloading{false};
    std::atomic<bool> updating{false};
    std::atomic<float> progress{0.0f};
    std::string status;
    std::string updateStatus = "Checking for updates...";
    std::string downloadDir;
    std::mutex pathMutex;
    bool pathChecked{false};
    std::string cachedYtDlpPath;
    std::vector<std::string> logs;
    std::thread workerThread;
    std::thread updateThread;

    void addLog(const std::string& log);
    void runYtDlp(std::string cmd, std::function<void(float)> progressCallback, std::function<void(bool, std::string)> finishCallback);

    // Win32 helpers — run child processes with CREATE_NO_WINDOW so no CMD
    // window flashes on screen.  runSilent() is fire-and-forget (returns exit
    // code); runSilentWithOutput() pipes stdout+stderr back through a
    // line-by-line callback and then returns the exit code.
    static int  runSilent(const std::string& cmd);
    static int  runSilentWithOutput(const std::string& cmd,
                                    std::function<void(const std::string&)> lineCallback);
};
