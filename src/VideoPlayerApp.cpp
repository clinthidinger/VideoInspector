#include <filesystem>
#include <functional>
#include <queue>
#include <cinder/app/App.h>
#include <cinder/app/RendererGl.h>
#include <cinder/CinderImGui.h>
#include <cinder/gl/gl.h>
#include <cinder/Log.h>
#include <cinder/Utilities.h>
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
#include <spdlog/spdlog.h>
#include "graphics/ViewportTransform.h"


class VideoPlayerApp : public ci::app::App
{
public:
    static void prepareSettings( Settings *settings );

    void setup() override;
    void draw() override;
    void update() override;
    void keyDown( ci::app::KeyEvent event ) override;
    void mouseWheel( ci::app::MouseEvent event ) override;
    void mouseDown( ci::app::MouseEvent event ) override;
    void mouseDrag( ci::app::MouseEvent event ) override;

private:
    void updateGui();
    void addZoom( float wheelIncrements );
    void resetPanZoom();
    bool isZoomed() const;
    bool isTransformedAreaOverThresh( float thresh ) const;
    void fit( const ci::Area &area );
    void loadMovie( const std::string &movieFilePath );
    void prevFrame();
    void nextFrame();
    void seekToFrame( int64_t frameNumber );
    void reset();

    std::string mMovieFilePath;
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
    ViewportTransform mViewportTransform;
    std::chrono::system_clock::time_point mLastMouseDownTime;

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
}

void VideoPlayerApp::setup()
{
    mViewportTransform.reset();
    mLastMouseDownTime = std::chrono::system_clock::now();
    ImGui::Initialize();
    auto args = getCommandLineArgs();
    if( args.size() > 1 )
    {
        mMovieFilePath = args[1];
        loadMovie( mMovieFilePath );
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
}

void VideoPlayerApp::draw()
{
    ci::gl::clear();
    
    ci::gl::ScopedMatrices scopedMatrices;
    if( mMovie )
    {
        const auto viewportRect = ci::app::getWindowBounds();
        ci::vec2 lowerLeftOrigin( viewportRect.x1, ci::app::getWindowHeight() - viewportRect.y2 );
        ci::gl::ScopedViewport scopedViewport( lowerLeftOrigin, viewportRect.getSize() );
        ci::gl::ScopedMatrices scopedMatrices;
        ci::gl::setMatricesWindow( viewportRect.getSize() );

        ci::gl::ScopedModelMatrix scopedModelMtx();
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
}

void VideoPlayerApp::updateGui()
{
    const int ItemWidth = 40;
    ImGui::Begin( "App Parameters " );
    ImGui::Text( "fps: %f", ci::app::App::getAverageFps() );
    ImGui::Text( "Frame: %ld", mFrameNumber );
    ImGui::PushItemWidth( ItemWidth );
    ImGui::Text( "%s", mMovieFilePath.c_str()  );
    ImGui::SameLine();
    if( ImGui::Button( "..." ) )
    {
        auto const path = getOpenFilePath( mMovieFilePath );
        if( !path.string().empty() )
        {
            mMovieFilePath = std::string( path.string().c_str() );
            loadMovie( mMovieFilePath );
        }
    }
    ImGui::PopItemWidth();
    
    if( ImGui::Button( "Fit" ) )
    {
        resetPanZoom();
    }

    if( ImGui::Button( ( mMovie && mMovie->isPlaying() ) ? "Pause" : "Play" ) )
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
    ImGui::Text( "Frame Count: %ld", mTotalFrameCount );
    mSliderFrame = mMovie ? mMovie->getCurrentFrame() : 0;
    if( ImGui::SliderInt("TimeLine", &mSliderFrame, 0, mTotalFrameCount ) )
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

    ImGui::PushItemWidth( ItemWidth * 3.0f );
    ImGui::InputInt( "Frame Number:", &mGotoFrame );
    ImGui::SameLine();
    if( ImGui::Button( "Go To" ) )
    {
        spdlog::info( "Seek to frame {}", mGotoFrame );
        mMovie->stop();
        seekToFrame( mGotoFrame );
    }
    if( ImGui::Button( "Prev Frame" ) )
    {
        prevFrame();
    }
    ImGui::SameLine();
    if( ImGui::Button( "Next Frame" ) )
    {
        nextFrame();
    }
    if( ImGui::InputFloat( "Rate", &mRate, 0.1f ) )
    {
        mRate = ci::clamp( mRate, 0.1f, 2.0f );
        if( mMovie )
        {
            mMovie->setRate( mRate );
        }
    }
    if( ImGui::Checkbox( "Repeat", &mDoesRepeat ) )
    {
        if( mDoesRepeat )
        {
            mDoesLoop = false;
        }
    }

    if( ImGui::Checkbox( "Loop", &mDoesLoop ) )
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
    ImGui::InputInt( "End Frame", &mLoopEndFrame );
    ImGui::SameLine();
    if( ImGui::Button( "Set##End" ) )
    {
        if( mMovie )
        {
            mLoopEndFrame = mMovie->getCurrentFrame();
        }
    }
    
    ImGui::PopItemWidth();
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
    
    mViewportTransform.mouseDown( event.getPos() );
    mLastMouseDownTime = currTime;
}

void VideoPlayerApp::mouseDrag( ci::app::MouseEvent event )
{
    mViewportTransform.mouseDrag( event.getPos() );
}

void VideoPlayerApp::mouseWheel( ci::app::MouseEvent event )
{
    mViewportTransform.mouseWheel( event.getPos(), event.getWheelIncrement() );
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

void VideoPlayerApp::loadMovie( const std::string &movieFilePath )
{
    mMovieFilePath = movieFilePath;
#ifdef CINDER_MSW
    mMovie = AxMovie::create( mMovieFilePath );
#else
    mMovie = ci::qtime::MovieGl::create( mMovieFilePath );
#endif
  
    if( mMovie == nullptr )
    {
        //spdlog::error( "Failed to load movie for view {}.", view );
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
        //std::thread::id id;
        while( !mSeekFinishedActions.empty() )
        {
            mSeekFinishedActions.front()();
            mSeekFinishedActions.pop();
        }

        mSignalIsSeekFinished.emit();
        //spdlog::info( "Viewport ctx seek finished.  Thread id: {}", -1 );// int( std::this_thread::get_id() ) );
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
    mIsReadyConn = mMovie->getIsReadySignal().connect( [this] () {
        resetPanZoom();
        mTotalFrameCount = mMovie->getFrameCount();
        mLoopEndFrame = mTotalFrameCount;
        mSignalIsReady.emit();
    } );
#endif
    mTotalFrameCount = mMovie->getFrameCount();//( mMovie->getDuration() * mMovie->getFramerate() );
    mLoopStartFrame = 0;
    mLoopEndFrame = mTotalFrameCount;
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
            spdlog::info( "Seek to frame {}", frameNumber );
            mMovie->seekToFrame( frameNumber );
#ifndef CINDER_MSW
        }
#endif
    }

}

CINDER_APP( VideoPlayerApp, ci::app::RendererGl( ci::app::RendererGl::Options() ), VideoPlayerApp::prepareSettings );
