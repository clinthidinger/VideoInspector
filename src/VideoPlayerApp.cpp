#include <filesystem>
#include <functional>
#include <future>
#include <queue>
#include <chrono>
#include <ctime>
#include <cinder/app/App.h>
#include <cinder/app/RendererGl.h>
#include <cinder/Capture.h>
#include <cinder/CinderImGui.h>
#include <cinder/gl/gl.h>
#include <cinder/Log.h>
#include <cinder/Utilities.h>
#include <imgui/imgui_internal.h>
#include <spdlog/spdlog.h>
//#include <ci_nanovg_gl.hpp>
#ifdef CINDER_LINUX
#ifdef linux
#undef linux
#endif
#include <cinder/qtime/QuickTimeGl.h>
using MovieRef = ci::qtime::MovieGlRef;
#else
#ifdef CINDER_MSW
#include "AxMovie.h"
using MovieRef = AxMovieRef;
#endif
#endif
#include "ControllerManager.h"
#include "fonts/RobotoRegular.h"
#include "fonts/FontAwesome-tweak.h"
#include "graphics/ViewportTransform.h"
#include "../blocks/AX-MediaPlayer/src/AX-MediaWriter.h"
#include "ClipboardMonitor.h"
#include "DownloaderDialog.h"
#include "DownloadModel.h"
#include "GalleryView.h"


#ifdef CINDER_MSW

#include <windows.h>
#include <shobjidl.h>   // For IShellItem
#include <shlwapi.h>
#include <shlobj.h>     // For SHCreateItemFromParsingName
#include <comdef.h>
#include <iostream>

#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "ole32.lib")

#endif 

// TODO:
// Download needs refresh.  May not at 60 hz.
// switching between gallery and regular needs to highlight last selection
// switching to regular should not restart video. seek to time
// make gallery fit size/dim of display better
// refresh dir list after download completes
// +/- keys to make gallery square bigger or smaller.
// hot keys in gallery to go up,down, left,right
// Buttons
// Energy saver
// Move list
// file drop
// camera feed and multi-viewport.  three transforms.
// brightness/contrast?
// gallery view
// Drag and drop url, or detect clipboard has copied URL.
// Gallery:
// open and  close buttons, icons for parent or folders, transform.  Double click.  Text with ImGUI.  Caching strategy.
// Intel realsense camera

class VideoPlayerApp : public ci::app::App
{
public:
    static void prepareSettings( Settings *settings );

    void setup() override;
    void setupIcon();
    void draw() override;
    void resize() override;
    void update() override;
    void keyDown( ci::app::KeyEvent event ) override;
    void mouseWheel( ci::app::MouseEvent event ) override;
    void mouseDown( ci::app::MouseEvent event ) override;
    void mouseDrag( ci::app::MouseEvent event ) override;
    void mouseMove( ci::app::MouseEvent event ) override;
    void mouseUp( ci::app::MouseEvent event ) override;
    void fileDrop( ci::app::FileDropEvent event ) override;
    
private:
    void invalidate();
    void updateGui();
    void addZoom( float wheelIncrements );
    void resetPanZoom();
    bool isZoomed() const;
    bool isTransformedAreaOverThresh( float thresh ) const;
    void fit();
    void fit( const ci::Area &area );
    void loadMovie( const std::string &movieFilePath );
    void loadMovie( int index );
    void prevVideo();
    void nextVideo();
    void prevFrame();
    void nextFrame();
    void seekToFrame( int64_t frameNumber );
    void reset();
    bool loadMoviesInDir();
    void setupCapture();
    bool isInCameraFrame( const ci::ivec2 &pos ) const;
    bool isInVideoFrame( const ci::ivec2 &pos ) const;
    void startCameraRecording();
    void stopCameraRecording();
    std::string generateRecordingFilename() const;

    void makeThumbnails( const std::string &dir );
    void openGallery();
    void checkControllerInput( float dt );

    enum class FileMode { File, Directory };
    FileMode mFileMode{ FileMode::File };
    std::string mPath;
    std::vector<std::string> mVideoFilePaths;
    int mSelectedVideoIndex{ -1 };
    MovieRef mMovie;
    int64_t mFrameNumber{ 0 };
    int64_t mTotalFrameCount{ 0 };
    int mGotoFrame{ 0 };
    int mSliderFrame{ 0 };
    bool mIsSliding{ false };
    float mRate{ 1.0f };
    bool mDoesLoop{ false };
    bool mDoesRepeat{ false };
    int mLoopStartFrame{ 0 };
    int mLoopEndFrame{ 0 };
    bool mEnableCamera{ false };
    ViewportTransform mViewportTransform;
    ViewportTransform mCamFrameTransform;
    bool mWasMouseDownInCamFrame{ false };
    std::chrono::system_clock::time_point mLastMouseDownTime;

    ci::CaptureRef mCapture;
    ci::gl::TextureRef mCamFrameTex;
    int mCameraDeviceCount{ 0 };
    
    bool mIsRecording{ false };
    AX::Video::MediaWriterRef mCameraWriter;
    std::string mRecordingFilePath;

    std::future<void> mThumbnailFut;
    GalleryView mGalleryView;
    ControllerManager mController;
    float mPrevElapsedSecs{ 0.0f };
    std::string mLastControllerAction;
    float mActionDisplayTimer{ 0.0f };

    void setControllerAction( const std::string &action ) { mLastControllerAction = action; mActionDisplayTimer = 3.0f; invalidate(); }
    bool mScrollToSelected{ false };
    bool mDoShowDownloaderDlg{ false };
    DownloadModel mDownloadModel;

#ifndef CINDER_MSW
    std::atomic_bool mIsSeeking{ false };
#endif
    ci::signals::Connection mSeekFinishConn;
#ifdef CINDER_MSW
    ci::signals::Connection mIsReadyConn;
    ci::signals::Signal<void()> mSignalIsReady;
#endif
    ci::signals::Signal<void()> mSignalIsSeekFinished;

    std::queue<std::function<void()>> mSeekFinishedActions;

    ImFont *mFont{ nullptr };
    ImFont *mFontAwesomeTweaked{ nullptr };
    static constexpr const int DefaultFontSize{ 20 };
    int mFontSize{ DefaultFontSize };
    static constexpr int DefaultRefreshCount{ 60 * 3 };
    int mRefreshCount{ DefaultRefreshCount };

    static constexpr int DefaultWindowWidth{ 1280 };
    static constexpr int DefaultWindowHeight{ 720 };
    static constexpr int DoubleClickMS{ 300 };
};

void VideoPlayerApp::prepareSettings( Settings *settings )
{
#if defined ( CINDER_MAC )
    if( app::getWindowContentScale() > 1.0f )
    {
        settings->setHighDensityDisplayEnabled( true );
    }
    std::cout << "Mac window content scale: " << app::getWindowContentScale() << "\n";
#endif
    settings->setWindowSize( DefaultWindowWidth, DefaultWindowHeight );
#ifdef _DEBUG
    settings->setConsoleWindowEnabled();
#endif
}

void VideoPlayerApp::setup()
{
    getWindow()->setTitle( "Video Biter" );
    setupIcon();

    mViewportTransform.reset();
    mLastMouseDownTime = std::chrono::system_clock::now();
    ImGui::Initialize();
    auto args = getCommandLineArgs();
    if( args.size() > 1 )
    {
        mVideoFilePaths = { args[1] };
        loadMovie( mVideoFilePaths.front() );
        if( mMovie && ( args.size() > 2 ) )
        {
            int jumpFrame = std::stoi( args[2] );
            if( jumpFrame > mTotalFrameCount )
            {
                jumpFrame %= mTotalFrameCount;
            }
            seekToFrame( jumpFrame );
        }
    }
    mCameraDeviceCount = ci::Capture::getDevices().size();

    ImFontConfig fontConfig;
    fontConfig.FontDataOwnedByAtlas = false;
    mFont = ImGui::GetIO().Fonts->AddFontFromMemoryTTF( const_cast<unsigned char *>( RobotoRegular ), RobotoRegularLength, mFontSize, &fontConfig );
    ImFontConfig fontAwsConfig;
    fontAwsConfig.FontDataOwnedByAtlas = false;
    mFontAwesomeTweaked = ImGui::GetIO().Fonts->AddFontFromMemoryTTF( const_cast< unsigned char * >( FontAwesome ), FontAwesomeLength, mFontSize, &fontAwsConfig );

    // Set up gallery selection callback
    mGalleryView.setSelectionCallback( [this]( const std::string& videoPath ) {
        loadMovie( videoPath );
        resetPanZoom();
        if( mMovie ) mMovie->play();

        // Sync the directory list and scroll to the selected video
        mFileMode = FileMode::Directory;
        mPath = std::filesystem::path( videoPath ).parent_path().string();
        if( loadMoviesInDir() )
        {
            auto filename = std::filesystem::path( videoPath ).filename().string();
            auto it = std::find( mVideoFilePaths.begin(), mVideoFilePaths.end(), filename );
            if( it != mVideoFilePaths.end() )
            {
                mSelectedVideoIndex = static_cast<int>( std::distance( mVideoFilePaths.begin(), it ) );
                mScrollToSelected = true;
            }
        }
        invalidate();
    } );

    ClipboardMonitor::getInstance();// setup Clipboard monitor.

    ClipboardMonitor::getInstance().getUrlSig().connect( [this]( const std::string &url ) {
       mDownloadModel.addUrl( url );
       mDoShowDownloaderDlg = true;

#if defined( CINDER_MSW )
            auto nativeWindow = static_cast<HWND>( ci::app::getWindow()->getNative() );
            ::SetForegroundWindow( nativeWindow );
            ::SetFocus( nativeWindow );
#endif // CINDER_MSW
            invalidate();
    });

//#ifdef ENABLE_ENERGY_SAVER
    auto renderer = std::static_pointer_cast<ci::app::RendererGl>( ci::app::getWindow()->getRenderer() );
    renderer->setFinishDrawFn( [this] ( ci::app::Renderer *renderer )
        {
            if( mMovie && mMovie->isPlaying() )
            {
                renderer->swapBuffers();
                return;
            }
            if( mGalleryView.isOpen() && mGalleryView.hasHoveredVideo() )
            {
                renderer->swapBuffers();
                return;
            }
            if( mRefreshCount )
            {
                renderer->swapBuffers();
                --mRefreshCount;
            }
        }
    );
//#endif

}

void VideoPlayerApp::setupIcon()
{
#ifdef CINDER_MSW
    HWND hwnd = ( HWND ) getWindow()->getNative();
    HICON hIcon = static_cast< HICON >( LoadImage(
        GetModuleHandle( nullptr ),
        MAKEINTRESOURCE( 101 ),  // or whatever ID your icon is
        IMAGE_ICON,
        0, 0,
        LR_DEFAULTCOLOR
    ) );
    SendMessage( hwnd, WM_SETICON, ICON_BIG, ( LPARAM ) hIcon );
    SendMessage( hwnd, WM_SETICON, ICON_SMALL, ( LPARAM ) hIcon );
#endif
}

void VideoPlayerApp::resize()
{
    if( mGalleryView.isOpen() )
    {
        mGalleryView.resize( ci::app::getWindowSize() );
        return;
    }
}

void VideoPlayerApp::draw()
{
    if( !mRefreshCount )
    {
        return;
    }
    //static uint64_t frameCount = 0;
    //spdlog::info( "FrameCount: {}", frameCount++ );
    ci::gl::clear();

    if( mGalleryView.isOpen() )
    {
        mGalleryView.draw();
        return;
    }

    if( mEnableCamera && mCamFrameTex )
    {
        const auto viewportRect = ci::app::getWindowBounds();
        ci::vec2 lowerLeftOrigin( viewportRect.x1, ci::app::getWindowHeight() - viewportRect.y2 );
        ci::gl::ScopedViewport scopedViewport( lowerLeftOrigin, viewportRect.getSize() );
        ci::gl::ScopedMatrices scopedMatrices;
        ci::gl::setMatricesWindow( viewportRect.getSize() );

        ci::gl::ScopedModelMatrix scopedModelMtx;
        ci::gl::setModelMatrix( mCamFrameTransform.getMatrix() );
        ci::gl::draw( mCamFrameTex, ci::Rectf( mCamFrameTex->getWidth(), 0, 0, mCamFrameTex->getHeight() ) );
    }

    if( mMovie )
    {
        const auto viewportRect = ci::app::getWindowBounds();
        ci::vec2 lowerLeftOrigin( viewportRect.x1, ci::app::getWindowHeight() - viewportRect.y2 );
        ci::gl::ScopedViewport scopedViewport( lowerLeftOrigin, viewportRect.getSize() );
        ci::gl::ScopedMatrices scopedMatrices;
        ci::gl::setMatricesWindow( viewportRect.getSize() );

        ci::gl::ScopedModelMatrix scopedModelMtx;
        ci::gl::setModelMatrix( mViewportTransform.getMatrix() );
#ifdef CINDER_MSW
        if( !mMovie->isReady() )
        {
            return;
        }
        auto lease = mMovie->getTexture();
        if( lease && lease->ToTexture() )
        {
            const float aspectRatio = lease->ToTexture()->getAspectRatio();
            ci::gl::draw( *lease );
        }
#else
        auto tex = mMovie->getTexture();
        if( tex )
        {
            ci::gl::draw( tex );
        }
#endif
    }
}

void VideoPlayerApp::update()
{
    const float elapsed = static_cast<float>( ci::app::getElapsedSeconds() );
    const float dt = elapsed - mPrevElapsedSecs;
    mPrevElapsedSecs = elapsed;

    mActionDisplayTimer = std::max( 0.0f, mActionDisplayTimer - dt );

    mController.Update( dt );
    checkControllerInput( dt );

    if( mGalleryView.isOpen() )
    {
        mGalleryView.update();
        return;
    }

    updateGui();
    mFrameNumber = ( mMovie ) ? mMovie->getCurrentFrame() : 0;
    if( mMovie )
    {
        if( mDoesLoop )
        {
            if( mFrameNumber >= mLoopEndFrame )
            {
                mSeekFinishedActions.push( [this] () {  mMovie->play(); } );
                seekToFrame( mLoopStartFrame );
            }
        }
        else
        {
            if( ( mFrameNumber != 0 ) && ( mFrameNumber >= ( mTotalFrameCount - 1 ) ) )
            {
                if( mDoesRepeat )
                {
                    mSeekFinishedActions.push( [this] () {  mMovie->play(); } );
                }
                seekToFrame( 0 );
            }
        }
    }

    if( mEnableCamera )
    {
        if( mCapture == nullptr )
        {
            setupCapture();
        }
        if( mCapture && mCapture->checkNewFrame() )
        {
            mCamFrameTex = ci::gl::Texture::create( *mCapture->getSurface() );

            if( mIsRecording && mCameraWriter && mCamFrameTex )
            {
                constexpr const bool flipUpDown = true;
                constexpr const bool flipLeftRight = true;
                constexpr const bool reverseRgb = true;
                mCameraWriter->Write( mCamFrameTex, flipUpDown, flipLeftRight, reverseRgb );
            }
        }
    }
    else
    {
        // Camera disabled - stop capture to turn off camera light
        if( mCapture )
        {
            mCapture->stop();
            mCapture.reset();
            mCamFrameTex.reset();
        }
    }
}

void VideoPlayerApp::invalidate()
{
    mRefreshCount = DefaultRefreshCount;
}

void VideoPlayerApp::updateGui()
{
    ImGui::SetCurrentFont( mFont );

    const int ItemWidth = 40;
    ImGui::Begin( "Controls" );
#ifdef _DEBUG
    ImGui::Text( "fps: %f", ci::app::App::getAverageFps() );
#endif
    ImGui::Text( "Frame: %ld", mFrameNumber );
   
    //ImGui::PushItemWidth( ItemWidth );
  /*  ImGui::Text( "%s", mMovieFilePath.c_str()  );
    ImGui::SameLine();
    if( ImGui::Button( "..." ) )
    {
        auto const path = getOpenFilePath( mMovieFilePath );
        if( !path.string().empty() )
        {
            mMovieFilePath = std::string( path.string().c_str() );
            loadMovie( mMovieFilePath );
        }
    }*/
    ImGui::Text( "File Mode: " );
    ImGui::SameLine();
    if( ImGui::RadioButton( "File", mFileMode == FileMode::File ) )
    {
        mFileMode = FileMode::File;
    }
    ImGui::SameLine();
    if( ImGui::RadioButton( "Directory", mFileMode == FileMode::Directory ) )
    {
        mFileMode = FileMode::Directory;
    }

    auto constexpr textFlags = ImGuiInputTextFlags_None | ImGuiInputTextFlags_ReadOnly;
    ImGui::SetNextItemWidth( ImGui::GetContentRegionAvail().x - ( ImGui::GetFontSize() * 4.5f ) );
    ImGui::InputText( "##FilePath", const_cast<char *>( mPath.c_str() ), mPath.size(), textFlags );
    if( ImGui::IsItemHovered( ImGuiHoveredFlags_AllowWhenDisabled ) )
    {
        if( !mPath.empty() )
        {
            ImGui::SetTooltip( mPath.data() );
        }
        else
        {
            ImGui::SetTooltip( "Click ... to load your image." );
        }
    }
    ImGui::SameLine();
    if( ImGui::Button( "Browse..." ) )
    {
        if( mFileMode == FileMode::File )
        {
            auto const path = getOpenFilePath( mPath );
            if( !path.string().empty() )
            {
                mPath = std::string( path.string().c_str() );
                mVideoFilePaths = { mPath };
                mSelectedVideoIndex = -1;
                loadMovie( mPath );
                resetPanZoom();
            }
        }
        else
        {
            auto const path = getFolderPath();
            if( !path.string().empty() )
            {
                mPath = std::string( path.string().c_str() );
                if( loadMoviesInDir() )
                {
                    loadMovie( 0 );
                }
                mDownloadModel.setOutputDir( path.string() );
                //loadMovie( mMovieFilePath );
            }
        }
    }

    if( mFileMode == FileMode::Directory )
    {
        ImGui::Text( "Video files [%d]", mVideoFilePaths.size() );

        // List box with all video files
        ImGui::BeginChild( "VideoListBox", ImVec2( 0, ImGui::GetFontSize() * 7 ), true );
        for( int i = 0; i < (int)mVideoFilePaths.size(); i++ )
        {
            if( ImGui::Selectable( mVideoFilePaths[i].c_str(), mSelectedVideoIndex == i ) )
            {
                loadMovie( i );
            }
            if( mScrollToSelected && mSelectedVideoIndex == i )
            {
                ImGui::SetScrollHereY( 0.5f );
                mScrollToSelected = false;
            }
        }
        ImGui::EndChild();
    }
    //ImGui::PopItemWidth();
    
    if( ImGui::Button( "Fit" ) )
    {
        resetPanZoom();
    }
    ImGui::SameLine();
    if( ImGui::Button( "Prev Vid" ) )
    {
        prevVideo();
    }
    ImGui::SameLine();
    if( ImGui::Button( "Next Vid" ) )
    {
        nextVideo();
    }
    ImGui::SameLine();
    if( ImGui::Button( "Gallery" ) )
    {
        openGallery();
    }
    ImGui::SameLine();
    if( ImGui::Button( "Downloader" ) )
    {
        mDoShowDownloaderDlg = true;
    }

    ImGui::Separator();

    ImGui::PushFont( mFontAwesomeTweaked );
    constexpr const char * const StopStr = "f";
    constexpr const char * const PlayStr = "d";
    constexpr const char * const PauseStr = "e";
    constexpr const char * const FFBwdStr = "c";
    constexpr const char * const FFFwdStr = "g";

    if( ImGui::Button( StopStr ) )
    {
        mMovie->stop();
        mMovie->seekToStart();
    }
    ImGui::SameLine();
    if( ImGui::Button( ( mMovie && mMovie->isPlaying() ) ? PauseStr : PlayStr ) )
    {
        if( mMovie )
        {
            if( mMovie->isPlaying() )
            {
                mMovie->pause();
            }
#ifndef CINDER_MSW
            else if( !mIsSeeking )
#else 
            else
#endif
            {
                mMovie->play();
            }
        }
    }
    ImGui::SameLine();
    if( ImGui::Button( FFBwdStr ) )
    {
        prevFrame();
    }
    ImGui::SameLine();
    if( ImGui::Button( FFFwdStr ) )
    {
        nextFrame();
    }
    ImGui::PopFont();
    mSliderFrame = mMovie ? mMovie->getCurrentFrame() : 0;
    ImGui::SetNextItemWidth( ImGui::GetContentRegionAvail().x );// -( ImGui::GetFontSize() * 4.5f ) );
    if( ImGui::SliderInt( "TimeLine", &mSliderFrame, 0, mTotalFrameCount ) )
    {
        seekToFrame( mSliderFrame );
        mIsSliding = true;
    }
#ifndef CINDER_MSW
    if( !mIsSeeking )
    {
        mSliderFrame = mFrameNumber;
    }
#endif

    ImGui::Text( "Frame Count: %ld", mTotalFrameCount );

    ImGui::SameLine( ImGui::GetFontSize() * 8.5f );
    if( ImGui::Checkbox( "Repeat", &mDoesRepeat ) )
    {
        if( mDoesRepeat )
        {
            mDoesLoop = false;
        }
    }

    ImGui::PushItemWidth( ItemWidth * 3.0f );
    ImGui::InputInt( "##Frame Number", &mGotoFrame );
    ImGui::SameLine();
    if( ImGui::Button( "Jump To" ) )
    {
        if( mMovie )
        {
            CI_LOG_I( "Seek to frame " + std::to_string( mGotoFrame ) );
            mMovie->stop();
            seekToFrame( mGotoFrame );
        }
    }
    if( ImGui::InputFloat( "Rate", &mRate, 0.05f, 0.1f, 2 ) )
    {
        mRate = ci::clamp( mRate, 0.05f, 2.0f );
        if( mMovie )
        {
            mMovie->setRate( mRate );
        }
    }
    ImGui::Separator();

    if( ImGui::CollapsingHeader( "Loop" ) )
    {
        if( ImGui::Checkbox( "Enable##Loop", &mDoesLoop ) )
        {
            if( mDoesLoop )
            {
                mDoesRepeat = false;
            }
        }

        ImGui::InputInt( "Start Frame", &mLoopStartFrame );
        ImGui::SameLine();
        if( ImGui::Button( "Set##Start" ) )
        {
            if( mMovie )
            {
                mLoopStartFrame = mMovie->getCurrentFrame();
            }
        }
        ImGui::InputInt( "End Frame  ", &mLoopEndFrame );
        ImGui::SameLine();
        if( ImGui::Button( "Set##End" ) )
        {
            if( mMovie )
            {
                mLoopEndFrame = mMovie->getCurrentFrame();
            }
        }
    }
    
    ImGui::PopItemWidth();
    
    if( ImGui::CollapsingHeader( "Camera" ) )
    {
        if( mCameraDeviceCount != 0 )
        {
            ImGui::Checkbox( "Enable##Camera", &mEnableCamera );
            
            if( mEnableCamera )
            {
                ImGui::Separator();
                
                if( !mIsRecording )
                {
                    if( ImGui::Button( "Start Recording" ) )
                    {
                        startCameraRecording();
                    }
                }
                else
                {
                    if( ImGui::Button( "Stop Recording" ) )
                    {
                        stopCameraRecording();
                    }
                    ImGui::SameLine();
                    ImGui::Text( "Recording..." );
                }
                
                if( !mRecordingFilePath.empty() )
                {
                    ImGui::Text( "Output: %s", mRecordingFilePath.c_str() );
                }
            }
        }
        else
        {
            ImGui::Text( "No camera devices found" );
        }
    }

    if( mDoShowDownloaderDlg )
    {
        mDoShowDownloaderDlg = downloader::drawDownloaderDialog( ci::ivec2( 20, 40 ), mDownloadModel );
    }

    if( ImGui::IsKeyPressed( ci::app::KeyEvent::KEY_g, false ) )
    {
        auto const key = ci::app::KeyEvent::KEY_g;
        keyDown( ci::app::KeyEvent( ci::app::getWindow(), key, key, key, key, key ) );
    }
    else if( ImGui::IsKeyPressed( ci::app::KeyEvent::KEY_ESCAPE, false ) )
    {
        auto const key = ci::app::KeyEvent::KEY_ESCAPE;
        keyDown( ci::app::KeyEvent( ci::app::getWindow(), key, key, key, key, key ) );
    }
    else if( ImGui::IsKeyPressed( ci::app::KeyEvent::KEY_SPACE, false ) )
    {
        auto const key = ci::app::KeyEvent::KEY_SPACE;
        keyDown( ci::app::KeyEvent( ci::app::getWindow(), key, key, key, key, key ) );
    }
    else if( ImGui::IsKeyPressed( ci::app::KeyEvent::KEY_LEFT, false ) )
    {
        auto const key = ci::app::KeyEvent::KEY_LEFT;
        keyDown( ci::app::KeyEvent( ci::app::getWindow(), key, key, key, key, key ) );
    }
    else if( ImGui::IsKeyPressed( ci::app::KeyEvent::KEY_RIGHT, false ) )
    {
        auto const key = ci::app::KeyEvent::KEY_RIGHT;
        keyDown( ci::app::KeyEvent( ci::app::getWindow(), key, key, key, key, key ) );
    }
    else if( ImGui::IsKeyPressed( ci::app::KeyEvent::KEY_UP, false ) )
    {
        auto const key = ci::app::KeyEvent::KEY_UP;
        keyDown( ci::app::KeyEvent( ci::app::getWindow(), key, key, key, key, key ) );
    }
    else if( ImGui::IsKeyPressed( ci::app::KeyEvent::KEY_DOWN, false ) )
    {
        auto const key = ci::app::KeyEvent::KEY_DOWN;
        keyDown( ci::app::KeyEvent( ci::app::getWindow(), key, key, key, key, key ) );
    }

    ImGui::End();

    if( mActionDisplayTimer > 0.0f )
    {
        auto* dl = ImGui::GetForegroundDrawList();
        const float renderSize = static_cast<float>( mFontSize * 8 );
        const float scale      = renderSize / static_cast<float>( mFontSize );
        ImVec2 textSize        = ImGui::CalcTextSize( mLastControllerAction.c_str() );
        ImVec2 pos( ( ci::app::getWindowWidth()  - textSize.x * scale ) * 0.5f,
                      ci::app::getWindowHeight() - mFontSize * 10 );
        dl->AddText( mFont, renderSize, pos,
                     IM_COL32( 255, 255, 255, 255 ), mLastControllerAction.c_str() );
    }
}

void VideoPlayerApp::keyDown( ci::app::KeyEvent event )
{
    invalidate();
    // Handle Escape to close gallery
    if( event.getCode() == ci::app::KeyEvent::KEY_ESCAPE )
    {
        if( mGalleryView.isOpen() )
        {
            mGalleryView.close();
            return;
        }
        else
        {
            resetPanZoom();
            return;
        }
    }

    if( event.getCode() == ci::app::KeyEvent::KEY_g )
    { 
        openGallery();
    }
    if( mMovie == nullptr )
    {
        return;
    }

    switch( event.getCode() )
    {
        case ci::app::KeyEvent::KEY_SPACE:
        {
            if( mMovie->isPlaying() )
            {
                mMovie->stop();
            }
            else
            {
                mMovie->play();
            }
            break;
        }
        case ci::app::KeyEvent::KEY_LEFT:
        {
            prevFrame();
            break;
        }
        case ci::app::KeyEvent::KEY_RIGHT:
        {
            nextFrame();
            break;
        }
        case ci::app::KeyEvent::KEY_UP:
        {
            prevVideo();
        }
        case ci::app::KeyEvent::KEY_DOWN:
        {
            nextVideo();
        }
        default:
        {
            break;
        }
    }// end switch
}

void VideoPlayerApp::mouseDown( ci::app::MouseEvent event )
{
    invalidate();
    const ci::ivec2 &pos = event.getPos();

    // Handle gallery mouse down first if gallery is open
    if( mGalleryView.isOpen() )
    {
        mGalleryView.mouseDown( pos );
        return;
    }

    auto currTime = std::chrono::system_clock::now();
    auto deltaMS = std::chrono::duration_cast<std::chrono::milliseconds>( currTime - mLastMouseDownTime ).count();

    if( deltaMS < DoubleClickMS )
    {
        resetPanZoom();
    }

    if( isInCameraFrame( pos ) && !isInVideoFrame( pos ) )
    {
        mCamFrameTransform.mouseDown( pos );
        mWasMouseDownInCamFrame = true;
        return;
    }

    mViewportTransform.mouseDown( pos );
    mLastMouseDownTime = currTime;
}

void VideoPlayerApp::mouseDrag( ci::app::MouseEvent event )
{
    invalidate();
    // Don't handle drag if gallery is open
    if( mGalleryView.isOpen() )
    {
        mGalleryView.mouseDrag( event.getPos() );
        return;
    }

    const ci::ivec2 &pos = event.getPos();
    if( isInCameraFrame( pos ) && ( mWasMouseDownInCamFrame || !isInVideoFrame( pos ) ) )
    {
        mCamFrameTransform.mouseDrag( pos );
        return;
    }
    mViewportTransform.mouseDrag( pos );
}

void VideoPlayerApp::mouseMove( ci::app::MouseEvent event )
{
    if( mGalleryView.isOpen() )
    {
        mGalleryView.mouseMove( event.getPos() );
        invalidate();
    }
}

void VideoPlayerApp::mouseUp( ci::app::MouseEvent event )
{
    mWasMouseDownInCamFrame = false;
    invalidate();
}

void VideoPlayerApp::mouseWheel( ci::app::MouseEvent event )
{
    const ci::ivec2 &pos = event.getPos();
    invalidate();

    // Handle gallery mouse wheel if gallery is open
    if( mGalleryView.isOpen() )
    {
        mGalleryView.mouseWheel( pos, event.getWheelIncrement() );
        return;
    }

    if( isInCameraFrame( pos ) && !isInVideoFrame( pos ) )
    {
        mCamFrameTransform.mouseWheel( pos, event.getWheelIncrement() );
        return;
    }

    mViewportTransform.mouseWheel( pos, event.getWheelIncrement() );
}

void VideoPlayerApp::fileDrop( ci::app::FileDropEvent event )
{
    if( std::filesystem::is_directory( event.getFile( 0 ) ) )
    {
        mFileMode = FileMode::Directory;
        mPath = event.getFile( 0 ).string();
        if( loadMoviesInDir() )
        {
            loadMovie( 0 );
        }
    }
    else
    {
        loadMovie( event.getFile( 0 ).string() );
    }
}

void VideoPlayerApp::addZoom( float wheelIncrements )
{
    mViewportTransform.mouseWheel( ci::vec2( ci::app::getWindowSize() ) / 2.0f, wheelIncrements );
}

bool VideoPlayerApp::isTransformedAreaOverThresh( float thresh ) const
{
    const ci::Rectf screenRect = ci::app::getWindowBounds();
    auto xformUL = mViewportTransform.getMatrix() * ci::vec4(screenRect.getUpperLeft(), 0, 1);
    auto xformLR = mViewportTransform.getMatrix() * ci::vec4( screenRect.getLowerRight(), 0, 1 );
    ci::Rectf transformedRect( xformUL, xformLR );
    float origArea = transformedRect.calcArea();
    transformedRect.clipBy( screenRect );
    const float eps = 0.0001f;
    if( ( std::abs( transformedRect.x1 - screenRect.x1 ) < eps ) &&
        ( std::abs( transformedRect.x2 - screenRect.x2 ) < eps ) &&
        ( std::abs( transformedRect.y1 - screenRect.y1 ) < eps ) &&
        ( std::abs( transformedRect.y2 - screenRect.y2 ) < eps ) )
    {
        return true;// Screen is completely contained.
    }
    float clippedArea = transformedRect.calcArea();
    float overLapPercent = clippedArea / origArea;
    
    return ( overLapPercent > thresh );
}

bool VideoPlayerApp::isZoomed() const
{
    constexpr float eps = 0.001f;
    return ( std::abs( mViewportTransform.getScale() ) > eps );
}

void VideoPlayerApp::resetPanZoom()
{
    mViewportTransform.reset();
    fit();
}

void VideoPlayerApp::fit()
{
    fit( ci::app::getWindowBounds() );
}

void VideoPlayerApp::fit( const ci::Area &area )
{
    if( mMovie == nullptr )
    {
        return;
    }
    auto size = mMovie->getSize();
    if( ( size.x < ci::EPSILON_VALUE ) || ( size.y < ci::EPSILON_VALUE ) )
    {
        size = ci::app::getWindowSize();
    }
    mViewportTransform.setRotation( 0.0f );
    
    auto scale = ci::vec2( area.getSize() ) / ci::vec2( size );
    auto scaleScalar = std::min<float>( scale.x, scale.y );
    mViewportTransform.setScale( scaleScalar );
    mViewportTransform.setTranslation( ci::vec2( area.getSize() / 2 ) - ( ci::vec2( size / 2 ) * scaleScalar ) );
}

void VideoPlayerApp::loadMovie( int index )
{
    if( !mVideoFilePaths.empty() )
    {
        mSelectedVideoIndex = std::clamp<int>( index, 0, mVideoFilePaths.size() - 1 );
        loadMovie( mPath + "/" + mVideoFilePaths[mSelectedVideoIndex] );
        resetPanZoom();
    }
}

void VideoPlayerApp::loadMovie( const std::string &movieFilePath )
{
    //mMovieFilePath = movieFilePath;
    const bool wasPlaying = ( mMovie && mMovie->isPlaying() );
#ifdef CINDER_MSW
    mMovie = AxMovie::create( movieFilePath );
#else
    mMovie = ci::qtime::MovieGl::create( movieFilePath );
#endif
  
    if( mMovie == nullptr )
    {
        CI_LOG_E( "Failed to load movie." );
        return;
    }
    resetPanZoom();
    if( mSeekFinishConn.isConnected() )
    {
        mSeekFinishConn.disconnect();
    }
    mSeekFinishConn = mMovie->getSeekFinishedSignal().connect( [this] () {
#ifndef CINDER_MSW
        mIsSeeking = false;
#endif
        while( !mSeekFinishedActions.empty() )
        {
            mSeekFinishedActions.front()();
            mSeekFinishedActions.pop();
        }

        mSignalIsSeekFinished.emit();
    } );
#ifdef CINDER_MSW
    if( mMovie->isReady() )
    {
        mSignalIsReady.emit();
    }
    if( mIsReadyConn.isConnected() )
    {
        mIsReadyConn.disconnect();
    }
    mIsReadyConn = mMovie->getIsReadySignal().connect( [this, wasPlaying] () {
        resetPanZoom();
        mTotalFrameCount = mMovie->getFrameCount();
        mLoopEndFrame = mTotalFrameCount;
        mMovie->setRate( mRate );
        if( wasPlaying )
        {
            mMovie->play();
        }
        mSignalIsReady.emit();
    } );
#endif
    mTotalFrameCount = mMovie->getFrameCount();
    mLoopStartFrame = 0;
    mLoopEndFrame = mTotalFrameCount;
}

void VideoPlayerApp::reset()
{
    if( mIsRecording )
    {
        stopCameraRecording();
    }
    
    if( mSeekFinishConn.isConnected() )
    {
        mSeekFinishConn.disconnect();
    }
#ifdef CINDER_MSW
    if( mIsReadyConn.isConnected() )
    {
        mIsReadyConn.disconnect();
    }
#endif
}

void VideoPlayerApp::prevVideo()
{
    loadMovie( std::clamp<int>( mSelectedVideoIndex - 1, 0, mVideoFilePaths.size() - 1 ) );
}

void VideoPlayerApp::nextVideo()
{
    loadMovie( std::clamp<int>( mSelectedVideoIndex + 1, 0, mVideoFilePaths.size() - 1 ) );
}

void VideoPlayerApp::prevFrame()
{
    if( !mMovie->isPlaying() )
    {
        const int stepFrames = 1;
        auto frame = ci::clamp<int>( std::round( mMovie->getCurrentTime() * mMovie->getFramerate() ) - stepFrames,
                                        0,
                                        std::round( mMovie->getDuration() * mMovie->getFramerate() ) );    
        seekToFrame( frame );
    }
}

void VideoPlayerApp::nextFrame()
{
    if( !mMovie->isPlaying() )
    {
#ifndef CINDER_MSW
        if( !mIsSeeking )
        {
#endif
            mMovie->stepForward(); // Note: does not cause a seek.
#ifndef CINDER_MSW
        }
#endif
    }
}

void VideoPlayerApp::seekToFrame( int64_t frameNumber )
{
    if( mMovie )
    {
#ifndef CINDER_MSW
        if( !mIsSeeking )
        {
            mIsSeeking = true;
#endif
            CI_LOG_I( "Seek to frame " + std::to_string( frameNumber ) );
            mMovie->seekToFrame( frameNumber );
#ifndef CINDER_MSW
        }
#endif
    }

}

bool VideoPlayerApp::loadMoviesInDir()
{
    mVideoFilePaths.clear();
    mSelectedVideoIndex = -1;

    static const std::vector<std::string> VideoExtensions =
    {
        ".mp4", ".avi", ".mkv", ".mov", ".wmv", ".flv", ".webm", ".m4v", ".mpg", ".mpeg", ".3gp"
    };

    try 
    {
        for( const auto &entry : std::filesystem::directory_iterator( mPath ) ) 
        {
            if( entry.is_regular_file() ) 
            {
                std::string extension = entry.path().extension().string();
                // Convert extension to lowercase for case-insensitive comparison
                std::transform( extension.begin(), extension.end(), extension.begin(),
                    [] ( unsigned char c ) { return std::tolower( c ); } );

                if( std::find( VideoExtensions.begin(), VideoExtensions.end(), extension ) != VideoExtensions.end() ) 
                {
                    mVideoFilePaths.push_back( entry.path().filename().string() );
                }
            }
        }

        // Sort video files alphabetically
        std::sort( mVideoFilePaths.begin(), mVideoFilePaths.end() );
    }
    catch( const std::filesystem::filesystem_error &e ) 
    {
        std::cerr << "Error accessing directory: " << e.what() << std::endl;
    }

    return !mVideoFilePaths.empty();
}

void VideoPlayerApp::setupCapture()
{
    if( ci::Capture::getDevices().empty() )
    {
        return;
    }

    try 
    {
        mCapture = ci::Capture::create( 640, 480 );
        mCapture->start();
    }
    catch( const ci::CaptureExc &e ) 
    {
        ci::app::console() << "Error opening camera: " << e.what() << std::endl;
    }
}

bool VideoPlayerApp::isInCameraFrame( const ci::ivec2 &pos ) const
{
    if( mCapture == nullptr )
    {
        return false;
    }
    auto const area = mCapture->getBounds();
    auto const &mtx = mCamFrameTransform.getMatrix();
    const ci::Area transformedArea(
        mtx * ci::vec4( area.getUL(), 0.0f, 1.0f ),
        mtx * ci::vec4( area.getLR(), 0.0f, 1.0f )
    );

    return ( transformedArea.contains( pos ) );
}

bool VideoPlayerApp::isInVideoFrame( const ci::ivec2 &pos ) const
{
    if( mMovie == nullptr )
    {
        return false;
    }

    auto const size = mMovie->getSize();
    auto const &mtx = mViewportTransform.getMatrix();
    const ci::Area transformedArea(
        mtx * ci::vec4( 0.0f, 0.0f, 0.0f, 1.0f ),
        mtx * ci::vec4( size, 0.0f, 1.0f )
    );

    return ( transformedArea.contains( pos ) );
}

void VideoPlayerApp::startCameraRecording()
{
    if( !mCapture || mIsRecording )
    {
        return;
    }
    
    mRecordingFilePath = generateRecordingFilename();
    auto captureSize = mCapture->getSize();
    
    constexpr int bitrate = 5000000;
    constexpr int fps = 30;
    
    mCameraWriter = AX::Video::MediaWriter::Create( mRecordingFilePath, captureSize, bitrate, fps );
    
    if( mCameraWriter )
    {
        mIsRecording = true;
        CI_LOG_I( "Started camera recording to: " + mRecordingFilePath );
    }
    else
    {
        CI_LOG_E( "Failed to create camera writer" );
        mRecordingFilePath.clear();
    }
}

void VideoPlayerApp::stopCameraRecording()
{
    if( !mIsRecording || !mCameraWriter )
    {
        return;
    }
    
    mCameraWriter->Finalize();
    mCameraWriter.reset();
    mIsRecording = false;
    
    CI_LOG_I( "Stopped camera recording. File saved to: " + mRecordingFilePath );
}

std::string VideoPlayerApp::generateRecordingFilename() const
{
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t( now );
    auto tm = *std::localtime( &time_t );

    char buffer[100];
    std::strftime( buffer, sizeof( buffer ), "camera_recording_%Y%m%d_%H%M%S.mp4", &tm );

    return std::string( buffer );
}

void VideoPlayerApp::checkControllerInput( float dt )
{
    if( !mController.IsConnected() ) return;

    using Btn = ControllerManager::Button;
    using Ax  = ControllerManager::Axis;

    // Triangle: toggle gallery
    if( mController.IsButtonDown( Btn::Triangle ) )
    {
        if( mGalleryView.isOpen() ) { mGalleryView.close(); setControllerAction( "Close Gallery" ); }
        else                        { openGallery();         setControllerAction( "Gallery" ); }
    }

    if( mGalleryView.isOpen() )
    {
        // D-pad and stick directions navigate the active video
        int dc = 0, dr = 0;
        if( mController.IsButtonDown( Btn::DpadLeft  ) || mController.IsButtonDown( Btn::LStickLeft  ) || mController.IsButtonDown( Btn::RStickLeft  ) ) dc = -1;
        if( mController.IsButtonDown( Btn::DpadRight ) || mController.IsButtonDown( Btn::LStickRight ) || mController.IsButtonDown( Btn::RStickRight ) ) dc =  1;
        if( mController.IsButtonDown( Btn::DpadUp    ) || mController.IsButtonDown( Btn::LStickUp    ) || mController.IsButtonDown( Btn::RStickUp    ) ) dr = -1;
        if( mController.IsButtonDown( Btn::DpadDown  ) || mController.IsButtonDown( Btn::LStickDown  ) || mController.IsButtonDown( Btn::RStickDown  ) ) dr =  1;
        if( dc != 0 || dr != 0 )
        {
            mGalleryView.navigateActive( dc, dr );
            setControllerAction( "Navigate" );
        }

        // Cross: select active video and play in regular view
        if( mController.IsButtonDown( Btn::Cross ) )
        {
            mGalleryView.selectActive();
            setControllerAction( "Select" );
        }

        // R2: zoom in   L2: zoom out
        constexpr float ZoomDeadZone = 0.1f;
        float r2 = mController.GetAxis( Ax::R2 );
        float l2 = mController.GetAxis( Ax::L2 );
        if( r2 > ZoomDeadZone )      { mGalleryView.zoom(  r2 * 0.1f ); setControllerAction( "Zoom In"  ); invalidate(); }
        else if( l2 > ZoomDeadZone ) { mGalleryView.zoom( -l2 * 0.1f ); setControllerAction( "Zoom Out" ); invalidate(); }
    }
    else
    {
        // D-pad up/down: previous/next video   D-pad left: seek to beginning
        if( mController.IsButtonDown( Btn::DpadUp ) )   { prevVideo(); setControllerAction( "Prev Video" ); }
        if( mController.IsButtonDown( Btn::DpadDown ) )  { nextVideo(); setControllerAction( "Next Video" ); }
        if( mController.IsButtonDown( Btn::DpadLeft ) && mMovie ) { seekToFrame( 0 ); setControllerAction( "Restart" ); }

        // L3: toggle repeat   R3: toggle loop
        if( mController.IsButtonDown( Btn::L3 ) )
        {
            mDoesRepeat = !mDoesRepeat;
            if( mDoesRepeat ) mDoesLoop = false;
            setControllerAction( mDoesRepeat ? "Repeat On" : "Repeat Off" );
        }
        if( mController.IsButtonDown( Btn::R3 ) )
        {
            mDoesLoop = !mDoesLoop;
            if( mDoesLoop ) mDoesRepeat = false;
            setControllerAction( mDoesLoop ? "Loop On" : "Loop Off" );
        }

        if( mMovie )
        {
            // Cross: play/pause
            if( mController.IsButtonDown( Btn::Cross ) )
            {
                if( mMovie->isPlaying() ) { mMovie->pause(); setControllerAction( "Pause" ); }
                else                     { mMovie->play();  setControllerAction( "Play"  ); }
            }

            // L1: prev frame   R1: next frame
            if( mController.IsButtonDown( Btn::L1 ) )  { prevFrame(); setControllerAction( "Prev Frame" ); }
            if( mController.IsButtonDown( Btn::R1 ) )  { nextFrame(); setControllerAction( "Next Frame" ); }

            // Square: set loop start   Circle: set loop end
            if( mController.IsButtonDown( Btn::Square ) )
            {
                mLoopStartFrame = static_cast<int>( mMovie->getCurrentFrame() );
                setControllerAction( "Loop Start: " + std::to_string( mLoopStartFrame ) );
            }
            if( mController.IsButtonDown( Btn::Circle ) )
            {
                mLoopEndFrame = static_cast<int>( mMovie->getCurrentFrame() );
                setControllerAction( "Loop End: " + std::to_string( mLoopEndFrame ) );
            }

            constexpr float DeadZone = 0.1f;
            constexpr float ScrubFramesPerSec = 60.0f;

            // Left analog left/right: scrub timeline
            float lx = mController.GetAxis( Ax::LX );
            if( std::abs( lx ) > DeadZone )
            {
                auto delta = static_cast<int64_t>( lx * ScrubFramesPerSec * dt );
                if( delta != 0 )
                {
                    seekToFrame( std::clamp( mFrameNumber + delta, int64_t( 0 ), mTotalFrameCount - 1 ) );
                    setControllerAction( "Scrub" );
                }
            }

            // Right analog left/right: step rate up or down
            constexpr float RateStep = 0.05f;
            if( mController.IsButtonDown( Btn::RStickRight ) )
            {
                mRate = std::min( mRate + RateStep, 3.0f );
                mMovie->setRate( mRate );
                char buf[32]; std::snprintf( buf, sizeof( buf ), "Rate: %.2fx", mRate );
                setControllerAction( buf );
            }
            else if( mController.IsButtonDown( Btn::RStickLeft ) )
            {
                mRate = std::max( mRate - RateStep, 0.05f );
                mMovie->setRate( mRate );
                char buf[32]; std::snprintf( buf, sizeof( buf ), "Rate: %.2fx", mRate );
                setControllerAction( buf );
            }

            // R2: fast forward (seek forward proportional to trigger)
            float r2 = mController.GetAxis( Ax::R2 );
            if( r2 > DeadZone )
            {
                auto delta = static_cast<int64_t>( r2 * ScrubFramesPerSec * dt );
                if( delta != 0 )
                {
                    seekToFrame( std::clamp( mFrameNumber + delta, int64_t( 0 ), mTotalFrameCount - 1 ) );
                    setControllerAction( "Fast Forward" );
                }
            }

            // L2: rewind (seek backward proportional to trigger)
            float l2 = mController.GetAxis( Ax::L2 );
            if( l2 > DeadZone )
            {
                auto delta = static_cast<int64_t>( l2 * ScrubFramesPerSec * dt );
                if( delta != 0 )
                {
                    seekToFrame( std::max<int64_t>( mFrameNumber - delta, 0 ) );
                    setControllerAction( "Rewind" );
                }
            }
        }
    }
}

void VideoPlayerApp::openGallery()
{
    std::string galleryPath;
    if( ( mFileMode == FileMode::File ) && std::filesystem::is_regular_file( mPath ) )
    {
        galleryPath = std::filesystem::path( mPath ).parent_path().string();
    }
    else
    {
        galleryPath = mPath;
    }

    // If no directory is currently selected, open folder browser
    if( galleryPath.empty() || !std::filesystem::is_directory( galleryPath ) )
    {
        auto const path = getFolderPath();
        if( !path.string().empty() )
        {
            galleryPath = path.string();
        }
        else
        {
            // User cancelled folder selection
            return;
        }
    }

    if( mMovie )
    {
        mMovie->pause();
    }

    // Open the gallery with the selected directory
    mGalleryView.resize( ci::app::getWindowSize() );
    mGalleryView.open( galleryPath );
    mPath = galleryPath;
    mFileMode = FileMode::Directory;
}

/*
void VideoPlayerApp::makeThumbnails( const std::string &dir )
{
    CoInitialize( nullptr );

    const std::vector<std::string> videoFilePaths = mVideoFilePaths;
    mThumbnailFut = std::async( [videoFilePaths] () {

        for( auto const videoPath : videoFilePaths )
        {
            LPCWSTR videoPath = L"C:\\Path\\To\\Your\\Video.mp4";

            IShellItem *pShellItem = nullptr;
            HRESULT hr = SHCreateItemFromParsingName( videoPath, nullptr, IID_PPV_ARGS( &pShellItem ) );
            if( SUCCEEDED( hr ) )
            {
                IShellItemImageFactory *pImageFactory = nullptr;
                hr = pShellItem->QueryInterface( IID_PPV_ARGS( &pImageFactory ) );
                if( SUCCEEDED( hr ) )
                {
                    SIZE size = { 256, 256 }; // Thumbnail size
                    HBITMAP hBitmap = nullptr;
                    hr = pImageFactory->GetImage( size, SIIGBF_BIGGERSIZEOK | SIIGBF_THUMBNAILONLY, &hBitmap );
                    if( SUCCEEDED( hr ) )
                    {
                        // You now have an HBITMAP with the thumbnail. You can save it or display it.
                        std::cout << "Thumbnail successfully retrieved." << std::endl;
                        saveHBitmapAsPng( hBitmap );
                        // Clean up
                        DeleteObject( hBitmap );
                    }
                    else
                    {
                        std::cerr << "GetImage failed: " << std::hex << hr << std::endl;
                    }
                    pImageFactory->Release();
                }
                pShellItem->Release();
            }
        }
        CoUninitialize();
    } );
}

bool saveHBitmapAsPng( HBITMAP hBitmap, const std::string &outputPath ) 
{
    BITMAP bmp = {};
    GetObject( hBitmap, sizeof( BITMAP ), &bmp );

    BITMAPINFOHEADER bi = {};
    bi.biSize = sizeof( BITMAPINFOHEADER );
    bi.biWidth = bmp.bmWidth;
    bi.biHeight = -bmp.bmHeight; // top-down
    bi.biPlanes = 1;
    bi.biBitCount = 32;
    bi.biCompression = BI_RGB;

    int dataSize = bmp.bmWidth * bmp.bmHeight * 4;
    std::vector<uint8_t> pixels( dataSize );

    HDC hDC = GetDC( nullptr );
    GetDIBits( hDC, hBitmap, 0, bmp.bmHeight, pixels.data(), reinterpret_cast< BITMAPINFO * >( &bi ), DIB_RGB_COLORS );
    ReleaseDC( nullptr, hDC );

    // Save with Cinder (if you're using it)
    ci::Surface8u surface( pixels.data(), bmp.bmWidth, bmp.bmHeight, bmp.bmWidth * 4, ci::SurfaceChannelOrder::BGRA );
    ci::writeImage( outputPath, surface );

    return true;
}

*/

CINDER_APP( VideoPlayerApp, ci::app::RendererGl( ci::app::RendererGl::Options() ), VideoPlayerApp::prepareSettings );





/*

// Define common video file extensions
const std::vector<std::string> VideoExtensions = 
{
    ".mp4", ".avi", ".mkv", ".mov", ".wmv", ".flv", ".webm", ".m4v", ".mpg", ".mpeg", ".3gp"
};

 void refreshVideoFiles() {
        videoFiles.clear();
        selectedIndex = -1;

        try {
            for (const auto& entry : std::filesystem::directory_iterator(currentDirectory)) {
                if (entry.is_regular_file()) {
                    std::string extension = entry.path().extension().string();
                    // Convert extension to lowercase for case-insensitive comparison
                    std::transform(extension.begin(), extension.end(), extension.begin(),
                        [](unsigned char c) { return std::tolower(c); });

                    if (std::find(VideoExtensions.begin(), VideoExtensions.end(), extension) != VideoExtensions.end()) {
                        videoFiles.push_back(entry.path().filename().string());
                    }
                }
            }

            // Sort video files alphabetically
            std::sort(videoFiles.begin(), videoFiles.end());
        }
        catch (const std::filesystem::filesystem_error& e) {
            std::cerr << "Error accessing directory: " << e.what() << std::endl;
        }
    }

    void renderImGuiListBox() {
        // Display current directory
        ImGui::Text("Current Directory: %s", currentDirectory.c_str());

        // Button to refresh the file list
        if (ImGui::Button("Refresh")) {
            refreshVideoFiles();
        }

        ImGui::SameLine();

        // Button to go up one directory
        if (ImGui::Button("Up One Level")) {
            std::filesystem::path currentPath(currentDirectory);
            if (currentPath.has_parent_path()) {
                setDirectory(currentPath.parent_path().string());
            }
        }

        // Display number of video files found
        ImGui::Text("Found %zu video files", videoFiles.size());

        // List box with all video files
        ImGui::BeginChild("VideoListBox", ImVec2(0, 300), true);
        for (int i = 0; i < videoFiles.size(); i++) {
            if (ImGui::Selectable(videoFiles[i].c_str(), selectedIndex == i)) {
                selectedIndex = i;
            }
        }
        ImGui::EndChild();

        // Show selected file info
        if (selectedIndex >= 0 && selectedIndex < videoFiles.size()) {
            ImGui::Text("Selected: %s", videoFiles[selectedIndex].c_str());

            std::filesystem::path fullPath = std::filesystem::path(currentDirectory) / videoFiles[selectedIndex];
            auto fileSize = std::filesystem::file_size(fullPath);

            // Convert file size to appropriate unit
            const char* units[] = {"B", "KB", "MB", "GB"};
            int unitIndex = 0;
            double size = static_cast<double>(fileSize);

            while (size > 1024 && unitIndex < 3) {
                size /= 1024;
                unitIndex++;
            }

            ImGui::Text("Size: %.2f %s", size, units[unitIndex]);

            // Add buttons for actions
            if (ImGui::Button("Open")) {
                // Add your code to open the video file
                std::cout << "Opening: " << fullPath.string() << std::endl;
            }
        }
    }

    std::string getSelectedFilePath() const {
        if (selectedIndex >= 0 && selectedIndex < videoFiles.size()) {
            return (std::filesystem::path(currentDirectory) / videoFiles[selectedIndex]).string();
        }
        return "";
    }
};


class VideoPreviewerApp : public App {
public:
    void setup() override;
    void update() override;
    void draw() override;
    void fileDrop(FileDropEvent event) override;
    void loadVideosFromFolder(const fs::path& path);

private:
    struct VideoItem {
        fs::path                     path;
        qtime::MovieGlRef            movie;
        gl::TextureRef               texture;
        Rectf                        rect;
        bool                         isPlaying = false;
        bool                         isHovered = false;
    };

    vector<VideoItem>                mVideos;
    const float                      THUMBNAIL_SIZE = 160.0f;
    const float                      THUMBNAIL_SPACING = 20.0f;
    const int                        THUMBNAILS_PER_ROW = 4;
    const vec2                       THUMBNAIL_DIMS = vec2(THUMBNAIL_SIZE, THUMBNAIL_SIZE * 9.0f / 16.0f); // 16:9 aspect ratio
};

void VideoPreviewerApp::setup() {
    setWindowSize(800, 600);

    // Default directory to search if none provided
    fs::path defaultPath = getHomeDirectory() / "Videos";
    if (fs::exists(defaultPath) && fs::is_directory(defaultPath)) {
        loadVideosFromFolder(defaultPath);
    }

    // Instructions
    console() << "Drag and drop a folder containing video files to load them." << endl;
}

void VideoPreviewerApp::loadVideosFromFolder(const fs::path& folderPath) {
    mVideos.clear();

    try {
        for (const auto& entry : fs::directory_iterator(folderPath)) {
            if (!fs::is_regular_file(entry.path())) continue;

            // Check if the file has a video extension
            string ext = entry.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

            if (ext == ".mp4" || ext == ".mov" || ext == ".avi" || ext == ".m4v" || ext == ".mkv") {
                try {
                    VideoItem item;
                    item.path = entry.path();

                    // Load the movie
                    qtime::MovieGl::Format format;
                    format.setPlayRate(1.0f);
                    item.movie = qtime::MovieGl::create(entry.path(), format);

                    // Get the first frame as texture
                    item.movie->seekToStart();
                    item.movie->stop();
                    if (item.movie->checkNewFrame()) {
                        item.texture = gl::Texture::create(item.movie->getTexture());
                    }

                    mVideos.push_back(item);
                }
                catch (const std::exception& e) {
                    console() << "Error loading video " << entry.path() << ": " << e.what() << endl;
                }
            }
        }

        // Calculate rectangles for each video
        for (size_t i = 0; i < mVideos.size(); i++) {
            int row = i / THUMBNAILS_PER_ROW;
            int col = i % THUMBNAILS_PER_ROW;

            float x = THUMBNAIL_SPACING + col * (THUMBNAIL_DIMS.x + THUMBNAIL_SPACING);
            float y = THUMBNAIL_SPACING + row * (THUMBNAIL_DIMS.y + THUMBNAIL_SPACING);

            mVideos[i].rect = Rectf(x, y, x + THUMBNAIL_DIMS.x, y + THUMBNAIL_DIMS.y);
        }

        console() << "Loaded " << mVideos.size() << " videos from " << folderPath << endl;
    }
    catch (const std::exception& e) {
        console() << "Error loading from directory: " << e.what() << endl;
    }
}

void VideoPreviewerApp::fileDrop(FileDropEvent event) {
    if (event.getNumFiles() >= 1) {
        const fs::path& path = event.getFile(0);
        if (fs::is_directory(path)) {
            loadVideosFromFolder(path);
        }
    }
}

void VideoPreviewerApp::update() {
    vec2 mousePos = getMousePos();

    for (auto& video : mVideos) {
        bool wasHovered = video.isHovered;
        video.isHovered = video.rect.contains(mousePos);

        // Handle hover state changes
        if (video.isHovered && !wasHovered) {
            // Mouse entered - start playing
            video.movie->play();
            video.isPlaying = true;
        }
        else if (!video.isHovered && wasHovered) {
            // Mouse exited - pause and reset to start
            video.movie->stop();
            video.movie->seekToStart();
            video.isPlaying = false;
        }

        // Update texture if playing
        if (video.isPlaying && video.movie->checkNewFrame()) {
            video.texture = gl::Texture::create(video.movie->getTexture());
        }
    }
}

void VideoPreviewerApp::draw() {
    gl::clear(Color(0.2f, 0.2f, 0.2f));

    // Draw videos
    for (const auto& video : mVideos) {
        if (video.texture) {
            gl::color(Color::white());
            gl::draw(video.texture, video.rect);

            // Draw border for hovered videos
            if (video.isHovered) {
                gl::color(Color(1.0f, 0.5f, 0.0f));
                gl::drawStrokedRect(video.rect, 2.0f);
            }

            // Draw filename below thumbnail
            gl::color(Color::white());
            gl::drawString(video.path.filename().string(),
                           vec2(video.rect.x1, video.rect.y2 + 5),
                           Color::white(), Font("Arial", 12));
        }
    }

    // Draw instructions if no videos are loaded
    if (mVideos.empty()) {
        gl::drawStringCentered("Drag and drop a folder containing video files",
                              getWindowCenter(),
                              Color::white(),
                              Font("Arial", 20));
    }
}

CINDER_APP(VideoPreviewerApp, RendererGl)

*/