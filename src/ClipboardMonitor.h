#pragma once

#include <string>
#include <vector>
#include <windows.h>
#include <cinder/Signals.h>


// Ctrl+L then Ctrl+C.
class ClipboardMonitor
{
public:
    static ClipboardMonitor &getInstance()
    {
        static ClipboardMonitor instance;
        return instance;
    }
    
    // Note: Meant to be called internally.
    void handleClipboardUpdate();
    WNDPROC getOriginalWndProc() const;

    cinder::signals::Signal<void( const std::string &url )> &getUrlSig();

private:
    ClipboardMonitor();
    ~ClipboardMonitor();
    ClipboardMonitor( ClipboardMonitor &other ) = delete;
    ClipboardMonitor( ClipboardMonitor &&other ) = delete;
    ClipboardMonitor &operator=( ClipboardMonitor &other ) = delete;
    ClipboardMonitor &operator=( ClipboardMonitor &&other ) = delete;

    void setup();
    void cleanup();

    static std::string getClipboardText( HWND hwnd );
    static bool isURL( const std::string &text );
    static bool isYouTubeOrInstagram( const std::string &url );

    HWND mHWnd;
    WNDPROC mOriginalWndProc;
    //std::vector<std::string> mUrlList;
    cinder::signals::Signal<void( const std::string& )> mUrlSig;
};

inline WNDPROC ClipboardMonitor::getOriginalWndProc() const
{
    return mOriginalWndProc;
}

inline cinder::signals::Signal<void( const std::string &url )> &ClipboardMonitor::getUrlSig()
{
    return mUrlSig;
}
