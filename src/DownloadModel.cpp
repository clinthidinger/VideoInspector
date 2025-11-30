#if 1
#include "DownloadModel.h"
#include <cstdlib>
#include <array>
#include <filesystem>
#include <future>
#include <memory>
#include <stdexcept>

void DownloadModel::downloadAll()
{
    if( mIsDownloading )
    {
        return;
    }
    mFut = std::async(
        [this]()
        {
            bool isEmpty = false;
            {
                std::scoped_lock<std::mutex> lk( mUrlListMutex );
                isEmpty = mUrlList.empty();
            }
            while( !isEmpty )
            {
                std::string url;
                {
                    std::scoped_lock<std::mutex> lk( mUrlListMutex );
                    url = mUrlList.front();
                }
                //auto const fileName = getOutputFilename( mUrlList.back(), mOutputDir );
                //if( fileName.empty() || !std::filesystem::exists( fileName ) )
                {
                    download( mUrlList.back(), " -P \"" + mOutputDir + "\" -f best" );
                }
                {
                    std::scoped_lock<std::mutex> lk( mUrlListMutex );
                    mUrlList.pop_back();
                    isEmpty = mUrlList.empty();
                }
            }// end while 
        } );
}

//*
std::string DownloadModel::getOutputFilename( const std::string &url, const std::string &outputDir )
{
    //YtDlpProcess process;
    // Use --get-filename to get what the file would be named
    std::string args = "-P \"" + outputDir + "\" --get-filename -o \"%(title)s.%(ext)s\"";
    //YtDlpResult result = process.execute( url, args );
    const int exitCode = download( url, args );

    if( ( exitCode == 0 ) && !mProcessOutput.empty() )
    {
        // Trim whitespace/newlines
        std::string filename = mProcessOutput;
        filename.erase( filename.find_last_not_of( " \n\r\t" ) + 1 );
        return filename;
    }
    return "";
}
//*/

/*
std::string callYtDlpWithOutput( const std::string &url, const std::string &additionalArgs = "" )
{
    std::string command = "yt-dlp " + additionalArgs + " \"" + url + "\"";
    std::array<char, 128> buffer;
    std::string result;

#ifdef _WIN32
    std::unique_ptr<FILE, decltype( &_pclose )> pipe( _popen( command.c_str(), "r" ), _pclose );
#else
    std::unique_ptr<FILE, decltype( &pclose )> pipe( popen( command.c_str(), "r" ), pclose );
#endif

    if( !pipe )
    {
        throw std::runtime_error( "popen() failed!" );
    }

    while( fgets( buffer.data(), buffer.size(), pipe.get() ) != nullptr )
    {
        result += buffer.data();
    }

    return result;
}
*/

#include <filesystem>
#include <iostream>
#include <string>
#include <atomic>
#include <thread>
#include <memory>

#ifdef _WIN32
    #include <windows.h>
#else
    #include <signal.h>
    #include <unistd.h>
    #include <sys/wait.h>
#endif


DownloadModel::DownloadModel()
{
#ifdef _WIN32
    ZeroMemory( &mProcessInfo, sizeof( mProcessInfo ) );
#endif
    mOutputDir = std::filesystem::current_path().string();
}

DownloadModel::~DownloadModel()
{
    cancelDownload();
}

int DownloadModel::download( const std::string &url, const std::string &additionalArgs )
{
    //std::string command = "yt-dlp " + additionalArgs + " \"" + url + "\"";
    std::string command = mDownloaderPath + " " + additionalArgs + " \"" + url + "\"";
    //std::string output;

#ifdef _WIN32
    HANDLE hOutputWrite = nullptr;
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof( SECURITY_ATTRIBUTES );
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = NULL;

    if( !CreatePipe( &hOutputRead, &hOutputWrite, &sa, 0 ) )
    {
        throw std::runtime_error( "CreatePipe failed" );
    }
    SetHandleInformation( hOutputRead, HANDLE_FLAG_INHERIT, 0 );

    STARTUPINFOA si;
    ZeroMemory( &si, sizeof( si ) );
    si.cb = sizeof( si );
    si.hStdOutput = hOutputWrite;
    si.hStdError = hOutputWrite;
    si.dwFlags |= STARTF_USESTDHANDLES;

    if( !CreateProcessA( NULL, const_cast<char *>( command.c_str() ), NULL, NULL, TRUE, 0, NULL, NULL, &si,
                            &mProcessInfo ) )
    {
        CloseHandle( hOutputWrite );
        CloseHandle( hOutputRead );
        throw std::runtime_error( "CreateProcess failed" );
    }

    mIsRunning = true;
    CloseHandle( hOutputWrite );

    char buffer[4096];
    DWORD bytesRead;
    while( mIsRunning 
           && ReadFile( hOutputRead, buffer, sizeof( buffer ) - 1, &bytesRead, NULL ) 
           && ( bytesRead > 0 ) )
    {
        buffer[bytesRead] = '\0';
        {
            std::scoped_lock<std::mutex> lk( mProcessOutputMutex );
            mProcessOutput += buffer;
        }
    }

    WaitForSingleObject( mProcessInfo.hProcess, INFINITE );
    DWORD exitCode = 0;
    GetExitCodeProcess( mProcessInfo.hProcess, &exitCode );
    CloseHandle( mProcessInfo.hProcess );
    CloseHandle( mProcessInfo.hThread );
    CloseHandle( hOutputRead );

    mIsRunning = false;

#else
    if( pipe( mPipefd ) == -1 )
    {
        throw std::runtime_error( "pipe() failed" );
    }

    mPid = fork();
    if( mPid == -1 )
    {
        close( mPipefd[0] );
        close( mPipefd[1] );
        throw std::runtime_error( "fork() failed" );
    }

    if( mPid == 0 )
    {
        // Child process
        close( mPipefd[0] );
        dup2( mPipefd[1], STDOUT_FILENO );
        dup2( mPipefd[1], STDERR_FILENO );
        close( mPipefd[1] );

        execl( "/bin/sh", "sh", "-c", command.c_str(), nullptr );
        _exit( 1 );
    }

    // Parent process
    mIsRunning = true;
    close( mPipefd[1] );

    char buffer[4096];
    ssize_t bytesRead = 0;
    while( isRunning && ( bytesRead = read( mPipefd[0], buffer, sizeof( buffer ) - 1 ) ) > 0 )
    {
        buffer[bytesRead] = '\0';
        {
            std::scoped_lock<std::mutex> lk( mProcessOutputMutex );
            mProcessOutput += buffer;
        }
    }

    close( mPipefd[0] );

    int exitCode = 0;
    if( mIsRunning )
    {
        int status = 0;
        waitpid( mPid, &status, 0 );

        
        if( WIFEXITED( status ) )
        {
            exitCode = WEXITSTATUS( status );
        }
        else if( WIFSIGNALED( status ) )
        {
            exitCode = -1;
        }
    }

    mIsRunning = false;
    mPid = -1;
#endif

    return static_cast<int>( exitCode );
}

void DownloadModel::cancelDownload()
{
    if( !mIsRunning )
    {
        return;
    }

#ifdef _WIN32
    if( mProcessInfo.hProcess )
    {
        TerminateProcess( mProcessInfo.hProcess, 1 );
        mIsRunning = false;
    }
#else
    if( mPid > 0 )
    {
        kill( mPid, SIGTERM );
        sleep( 1 );
        if( mIsRunning )
        {
            kill( mPid, SIGKILL );
        }
        int status = 0;
        waitpid( mPid, &status, 0 );
        mIsRunning = false;
    }
#endif
}


//// Usage example
//int main()
//{
//    YtDlpProcess process;
//    std::string url = "https://www.youtube.com/watch?v=dQw4w9WgXcQ";
//
//    // Run in a separate thread
//    std::string output;
//    std::thread downloadThread(
//        [&]()
//        {
//            try
//            {
//                output = process.execute( url, "-f best" );
//                std::cout << "Download complete!" << std::endl;
//            }
//            catch( const std::exception &e )
//            {
//                std::cerr << "Error: " << e.what() << std::endl;
//            }
//        } );
//
//    // Simulate user canceling after 5 seconds
//    std::this_thread::sleep_for( std::chrono::seconds( 5 ) );
//
//    if( process.running() )
//    {
//        std::cout << "Canceling download..." << std::endl;
//        process.cancel();
//    }
//
//    downloadThread.join();
//    std::cout << "Output: " << output << std::endl;
//
//    return 0;
//}

//Usage pattern:
//cppYtDlpProcess process;
//std::thread t( [&]() { process.execute( url ); } );
// Later...
//process.cancel(); // Cancel the download
//t.join();
//This gives you full control over the download lifecycle !RetryClaude can make mistakes.Please double -
//    check responses.Sonnet 4.5

#endif