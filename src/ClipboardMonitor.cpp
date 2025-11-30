#include "ClipboardMonitor.h"
#include <cinder/app/App.h>
//#include "cinder/app/RendererGl.h"
//#include "cinder/gl/gl.h"
#include <windows.h>
//#include <string>
//#include <vector>
//#include <algorithm>

//using namespace ci;
//using namespace ci::app;

//class ClipboardMonitor : public App
//{
//public:
//    void setup() override;
//    void update() override;
//    void draw() override;
//    void cleanup() override;
//
//private:
//    HWND hwnd;
//    WNDPROC originalWndProc;
//    std::vector<std::string> urlList;
//
//    void setupClipboardMonitoring();
//    void handleClipboardUpdate();
//    std::string getClipboardText();
//    bool isURL( const std::string &text );
//    bool isYouTubeOrInstagram( const std::string &url );
//
//    static LRESULT CALLBACK CustomWndProc( HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam );
//};

// Static pointer to access instance in window procedure
//static ClipboardMonitor *g_appInstance = nullptr;

//void ClipboardMonitor::setup()
//{
//    //g_appInstance = this;
//    setupClipboardMonitoring();
//
//    console() << "Clipboard monitor active. Copy YouTube or Instagram URLs." << std::endl;
//}

LRESULT CALLBACK CustomWndProc( HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam );

ClipboardMonitor::ClipboardMonitor()
{
    setup();
}

ClipboardMonitor::~ClipboardMonitor()
{
    cleanup();
}

void ClipboardMonitor::setup()
{
    // Get the HWND from Cinder's window
    mHWnd = ( HWND )ci::app::getWindow()->getNative();

    // Subclass the window to intercept messages
    mOriginalWndProc = ( WNDPROC )SetWindowLongPtr( mHWnd, GWLP_WNDPROC, ( LONG_PTR )CustomWndProc );

    // Register for clipboard notifications
    if( !AddClipboardFormatListener( mHWnd ) )
    {
        ci::app::console() << "Failed to add clipboard listener!" << std::endl;
    }
}

LRESULT CALLBACK CustomWndProc( HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam )
{
    if( msg == WM_CLIPBOARDUPDATE )
    {
        ClipboardMonitor::getInstance().handleClipboardUpdate();
    }

    // Call the original Cinder window procedure
    if( ClipboardMonitor::getInstance().getOriginalWndProc() )
    {
        return CallWindowProc( ClipboardMonitor::getInstance().getOriginalWndProc(), hwnd, msg, wParam, lParam );
    }

    return DefWindowProc( hwnd, msg, wParam, lParam );
}

void ClipboardMonitor::handleClipboardUpdate()
{
    std::string text = getClipboardText( mHWnd );

    if( text.empty() || !isURL( text ) )
    {
        return;
    }

    if( isYouTubeOrInstagram( text ) )
    {
        //mUrlList.push_back( text );
        mUrlSig.emit( text );
        ci::app::console() << "Added URL: " << text << std::endl;
        //ci::app::console() << "Total URLs: " << mUrlList.size() << std::endl;
    }
}

std::string ClipboardMonitor::getClipboardText( HWND hwnd )
{
    if( !OpenClipboard( hwnd ) )
    {
        return "";
    }

    std::string result;
    HANDLE hData = GetClipboardData( CF_UNICODETEXT );

    if( hData != nullptr )
    {
        wchar_t *pszText = static_cast<wchar_t *>( GlobalLock( hData ) );
        if( pszText != nullptr )
        {
            // Convert wide string to narrow string
            int size = WideCharToMultiByte( CP_UTF8, 0, pszText, -1, nullptr, 0, nullptr, nullptr );
            if( size > 0 )
            {
                std::vector<char> buffer( size );
                WideCharToMultiByte( CP_UTF8, 0, pszText, -1, buffer.data(), size, nullptr, nullptr );
                result = buffer.data();
            }
            GlobalUnlock( hData );
        }
    }

    CloseClipboard();
    return result;
}

bool ClipboardMonitor::isURL( const std::string &text )
{
    std::string lower = text;
    std::transform( lower.begin(), lower.end(), lower.begin(), ::tolower );

    return ( lower.find( "http://" ) == 0 || lower.find( "https://" ) == 0 || lower.find( "www." ) == 0 );
}

bool ClipboardMonitor::isYouTubeOrInstagram( const std::string &url )
{
    std::string lower = url;
    std::transform( lower.begin(), lower.end(), lower.begin(), ::tolower );

    return ( ( lower.find( "youtube.com" ) != std::string::npos ) || 
             ( lower.find( "youtu.be" ) != std::string::npos ) ||
             ( lower.find( "instagram.com" ) != std::string::npos ) );
}

void ClipboardMonitor::cleanup()
{
    // Restore original window procedure
    if( mHWnd && mOriginalWndProc )
    {
        SetWindowLongPtr( mHWnd, GWLP_WNDPROC, ( LONG_PTR )mOriginalWndProc );
    }

    // Unregister clipboard listener
    RemoveClipboardFormatListener( mHWnd );

    //g_appInstance = nullptr;
}
