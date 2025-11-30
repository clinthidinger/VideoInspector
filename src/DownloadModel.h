#pragma once

#include <atomic>
#include <deque>
#include <future>
#include <string>
#include <vector>
#ifdef _WIN32
    #include <windows.h>
    #include <processthreadsapi.h>
#endif

class IDownloadModel
{
public:
    virtual ~IDownloadModel() = default;
    virtual const std::vector<std::string> &getUrlList() = 0;
    virtual void addUrl( const std::string &url ) = 0;
    virtual void deleteUrl( int index ) = 0;
    virtual void downloadAll() = 0;
    virtual void cancelDownload() = 0;
    virtual bool isDownloading() const = 0;
    virtual const std::string &getOutputDir() const = 0;
    virtual void setOutputDir( const std::string &dir ) = 0;
    virtual const std::string &getDownloaderPath() const = 0;
    virtual void setDownloaderPath( const std::string &path ) = 0;
    virtual const std::string &getProcessOutput() = 0;
};

class DownloadModel : public IDownloadModel
{
public:
    DownloadModel();
    ~DownloadModel();
    const std::vector<std::string> &getUrlList() override;
    void addUrl( const std::string &url ) override;
    void deleteUrl( int index ) override;
    void downloadAll() override;
    void cancelDownload() override;
    bool isDownloading() const override;
    const std::string &getOutputDir() const override;
    void setOutputDir( const std::string &dir ) override;
    const std::string &getDownloaderPath() const override;
    void setDownloaderPath( const std::string &path ) override;
    const std::string &getProcessOutput() override;

private:
    int download( const std::string &url, const std::string &additionalArgs = "" );
    std::string getOutputFilename( const std::string &url, const std::string &outputDir );

    std::atomic_bool mIsDownloading{ false };
    std::vector<std::string> mUrlList;

#ifdef _WIN32
    PROCESS_INFORMATION mProcessInfo;
    HANDLE hOutputRead = nullptr;
#else
    pid_t mPid = -1;
    int mPipefd[2];
#endif
    std::atomic_bool mIsRunning{ false };
    std::future<void> mFut;
    std::mutex mUrlListMutex;
    std::mutex mProcessOutputMutex;
    std::string mProcessOutput;
    std::string mOutputDir;
    std::string mDownloaderPath{ "yt-dlp.exe" };
};


inline const std::vector<std::string> &DownloadModel::getUrlList()
{
    std::scoped_lock<std::mutex> lk( mUrlListMutex );
    return mUrlList;
}

inline void DownloadModel::addUrl( const std::string &url )
{
    std::scoped_lock<std::mutex> lk( mUrlListMutex );
    mUrlList.push_back( url );
}

inline void DownloadModel::deleteUrl( int index )
{
    mUrlList.erase( mUrlList.begin() + index );
}

inline bool DownloadModel::isDownloading() const
{
    return mIsDownloading;
}

inline const std::string &DownloadModel::getOutputDir() const
{
    return mOutputDir;
}

inline void DownloadModel::setOutputDir( const std::string &dir )
{
    mOutputDir = dir;
}

inline const std::string &DownloadModel::getDownloaderPath() const
{
    return mDownloaderPath;
}

inline void DownloadModel::setDownloaderPath( const std::string &path )
{
    mDownloaderPath = path;
}

inline const std::string &DownloadModel::getProcessOutput()
{
    std::scoped_lock<std::mutex> lk( mProcessOutputMutex );
    return mProcessOutput;
}
