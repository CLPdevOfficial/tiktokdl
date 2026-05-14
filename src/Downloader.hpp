#pragma once
#include <string>
#include <atomic>
#include <thread>
#include <vector>
#include <functional>

class Downloader {
public:
    Downloader();
    ~Downloader();

    bool checkYtDlp();
    std::string getYtDlpPath();
    void downloadYtDlp(std::function<void(float)> progressCallback, std::function<void(bool)> finishCallback);
    
    void startDownload(const std::string& url, const std::string& filename, const std::string& cookiesPath,
                       std::function<void(float)> progressCallback, std::function<void(bool, std::string)> finishCallback);

    bool isDownloading() const { return downloading; }
    float getProgress() const { return progress; }
    std::string getStatus() const { return status; }
    std::string getDownloadDir();
    
    const std::vector<std::string>& getLogs() const { return logs; }
    void clearLogs() { logs.clear(); }

private:
    std::atomic<bool> downloading{false};
    std::atomic<float> progress{0.0f};
    std::string status;
    std::string cachedYtDlpPath;
    std::vector<std::string> logs;
    std::thread workerThread;

    void addLog(const std::string& log);
    void runYtDlp(std::string cmd, std::function<void(float)> progressCallback, std::function<void(bool, std::string)> finishCallback);
};
