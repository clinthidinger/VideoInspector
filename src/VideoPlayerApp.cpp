#include <filesystem>
#include <functional>
#include <future>
#include <queue>
#include <cinder/app/App.h>
#include <cinder/app/RendererGl.h>
#include <cinder/Capture.h>
//#include <cinder/CinderImGui.h>
#include <CinderImGui.h>
#include <cinder/gl/gl.h>
#include <cinder/Log.h>
#include <cinder/Utilities.h>
//#include <imgui/imgui_internal.h>
#include "../lib/imgui/imgui_internal.h"
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
#ifdef CINDER_MAC
#include <cinder/qtime/QuickTimeGl.h>
using MovieRef = ci::qtime::MovieGlRef;
#endif
#include "fonts/RobotoRegular.h"
#include "fonts/FontAwesome-tweak.h"
#include "graphics/ViewportTransform.h"


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
// Buttons
// Energy saver
// Move list
// file drop
// camera feed and multi-viewport.  three transforms.
// brightness/contrast?
// gallery view

class VideoPlayerApp : public ci::app::App
{
public:
    static void prepareSettings( Settings *settings );

    void setup() override;
    void setupIcon();
    void draw() override;
    void update() override;
    void keyDown( ci::app::KeyEvent event ) override;
    void mouseWheel( ci::app::MouseEvent event ) override;
    void mouseDown( ci::app::MouseEvent event ) override;
    void mouseDrag( ci::app::MouseEvent event ) override;
    void mouseUp( ci::app::MouseEvent event ) override;
    void fileDrop( ci::app::FileDropEvent event ) override;
    
private:
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

    void makeThumbnails( const std::string &dir );

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
    
    std::future<void> mThumbnailFut;

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
    static constexpr const int DefaultFontSize{ 16 };
    int mFontSize{ DefaultFontSize };

    static constexpr int DefaultWindowWidth{ 1280 };
    static constexpr int DefaultWindowHeight{ 720 };
    static constexpr int DoubleClickMS{ 300 };
};

using namespace ci;
using namespace ci::app;

void VideoPlayerApp::prepareSettings( Settings *settings )
{
#if defined ( CINDER_MAC )
//    if( ci::app::getWindowContentScale() > 1.0f )
//    {
//        settings->setHighDensityDisplayEnabled( true );
//    }
//    std::cout << "Mac window content scale: " << app::getWindowContentScale() << "\n";
#endif
    settings->setWindowSize( DefaultWindowWidth, DefaultWindowHeight );
}

void VideoPlayerApp::setup()
{
    ci::app::getWindow()->setTitle( "Video Bite" );
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

void VideoPlayerApp::draw()
{
    ci::gl::clear();
    
    if( mEnableCamera && mCamFrameTex )
    {
        const auto viewportRect = ci::app::getWindowBounds();
        ci::vec2 lowerLeftOrigin( viewportRect.x1, ci::app::getWindowHeight() - viewportRect.y2 );
        ci::gl::ScopedViewport scopedViewport( lowerLeftOrigin, viewportRect.getSize() );
        ci::gl::ScopedMatrices scopedMatrices;
        ci::gl::setMatricesWindow( viewportRect.getSize() );

        ci::gl::ScopedModelMatrix scopedModelMtx;
        ci::gl::setModelMatrix( mCamFrameTransform.getMatrix() );
        ci::gl::draw( mCamFrameTex );
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
    updateGui();
    mFrameNumber = ( mMovie ) ? mMovie->getCurrentFrame() : 0;
    if( mMovie )
    {
        if( mDoesLoop )
        {
            if( mFrameNumber >= mLoopEndFrame )
            {
                mSeekFinishedActions.push( [this] () {
                    mMovie->play();
                    std::this_thread::sleep_for( std::chrono::milliseconds(200));
                    mMovie->setRate( mRate );
                } );
                seekToFrame( mLoopStartFrame );
            }
        }
        else
        {
            if( ( mFrameNumber != 0 ) && ( mFrameNumber >= ( mTotalFrameCount - 4 ) ) )
            {
                if( mDoesRepeat )
                {
                    mSeekFinishedActions.push( [this] () {
                        mMovie->play();
                        std::this_thread::sleep_for( std::chrono::milliseconds(200));
                        mMovie->setRate( mRate );
                    } );
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
        }
    }
}

void VideoPlayerApp::updateGui()
{
    //ImGui::SetCurrentFont( mFont );

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
            ImGui::SetTooltip( "%s", mPath.data() );
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
            auto const path = ci::app::getOpenFilePath( mPath );
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
                //loadMovie( mMovieFilePath );
            }
        }
    }

    if( mFileMode == FileMode::Directory )
    {
        ImGui::Text( "Video files [%lu]", mVideoFilePaths.size() );

        // List box with all video files
        ImGui::BeginChild( "VideoListBox", ImVec2( 0, ImGui::GetFontSize() * 7 ), true );
        for( int i = 0; i < mVideoFilePaths.size(); i++ ) 
        {
            if( ImGui::Selectable( mVideoFilePaths[i].c_str(), mSelectedVideoIndex == i ) )
            {
                loadMovie( i );
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
                
                mMovie->stop();
                //mMovie->pause();
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

    ImGui::Text( "Frame Count: %lld", mTotalFrameCount );

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
        }
    }
    ImGui::End();
}

void VideoPlayerApp::keyDown( ci::app::KeyEvent event )
{
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
        case ci::app::KeyEvent::KEY_ESCAPE:
        {
            resetPanZoom();
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
    auto currTime = std::chrono::system_clock::now();
    auto deltaMS = std::chrono::duration_cast<std::chrono::milliseconds>( currTime - mLastMouseDownTime ).count();

    if( deltaMS < DoubleClickMS )
    {
        resetPanZoom();
    }

    const ci::ivec2 &pos = event.getPos();
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
    const ci::ivec2 &pos = event.getPos();
    if( isInCameraFrame( pos ) && ( mWasMouseDownInCamFrame || !isInVideoFrame( pos ) ) )
    {
        mCamFrameTransform.mouseDrag( pos );
        return;
    }
    mViewportTransform.mouseDrag( pos );
}

void VideoPlayerApp::mouseUp( ci::app::MouseEvent event )
{
    mWasMouseDownInCamFrame = false;
}

void VideoPlayerApp::mouseWheel( ci::app::MouseEvent event )
{
    const ci::ivec2 &pos = event.getPos();
    if( isInCameraFrame( pos ) && !isInVideoFrame( pos ) )
    {
        mCamFrameTransform.mouseWheel( pos, event.getWheelIncrement() );
        return;
    }

    mViewportTransform.mouseWheel( pos, event.getWheelIncrement() );
}

void VideoPlayerApp::fileDrop( ci::app::FileDropEvent event )
{
    if( fs::is_directory( event.getFile( 0 ) ) )
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
        mSelectedVideoIndex = ci::clamp<int>( index, 0, mVideoFilePaths.size() - 1 );
        loadMovie( mPath + "/" + mVideoFilePaths[mSelectedVideoIndex] );
        resetPanZoom();
    }
}

void VideoPlayerApp::loadMovie( const std::string &movieFilePath )
{
    //mMovieFilePath = movieFilePath;
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
    //*
    mSeekFinishConn = mMovie->getJumpedSignal().connect( [this] () {
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
    //*/
#ifdef CINDER_MSW
    if( mMovie->isReady() )
    {
        mSignalIsReady.emit();
    }
    if( mIsReadyConn.isConnected() )
    {
        mIsReadyConn.disconnect();
    }
    mIsReadyConn = mMovie->getIsReadySignal().connect( [this] () {
        resetPanZoom();
        mTotalFrameCount = mMovie->getFrameCount();
        mLoopEndFrame = mTotalFrameCount;
        mMovie->setRate( mRate );
        mSignalIsReady.emit();
    } );
#endif
    mTotalFrameCount = mMovie->getNumFrames();
    mLoopStartFrame = 0;
    mLoopEndFrame = mTotalFrameCount;
    mMovie->setRate( mRate );
}

void VideoPlayerApp::reset()
{
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
    loadMovie( ci::clamp<int>( mSelectedVideoIndex - 1, 0, mVideoFilePaths.size() - 1 ) );
}

void VideoPlayerApp::nextVideo()
{
    loadMovie( ci::clamp<int>( mSelectedVideoIndex + 1, 0, mVideoFilePaths.size() - 1 ) );
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
        for( const auto &entry : fs::directory_iterator( mPath ) )
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
    catch( const fs::filesystem_error &e )
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
