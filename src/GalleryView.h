#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <cinder/app/App.h>
#include <cinder/Tween.h>
#include <cinder/gl/gl.h>

#ifdef CINDER_MSW
#include "AxMovie.h"
using MovieRef = AxMovieRef;
#else
#include <cinder/qtime/QuickTimeGl.h>
using MovieRef = ci::qtime::MovieGlRef;
#endif

#include "graphics/ViewportTransform.h"

class GalleryView
{
public:
    struct VideoItem
    {
        std::filesystem::path path;
        MovieRef movie;
        ci::gl::TextureRef thumbnail;
        ci::gl::TextureRef videoFrame;
        ci::Rectf rect;
        ci::Rectf expandedRect;
        bool isHovered{ false };
        //bool isPlaying{ false };
        ci::Anim<float> scale{ 1.0f };
        ci::signals::Connection isReadyConn;
    };

    using SelectionCallback = std::function<void(const std::string&)>;

    /*GalleryView();
    ~GalleryView();*/

    void open(const std::string& directoryPath);
    void close();
    bool isOpen() const { return mIsOpen; }

    void update();
    void draw();
    void resize( const ci::ivec2 &size );

    void mouseDown( const ci::ivec2& pos );
    void mouseDrag( const ci::ivec2 &pos );
    void mouseMove( const ci::ivec2& pos );
    void mouseWheel( const ci::ivec2& pos, float increment, bool ctrlDown = false );
    void keyDown( const ci::app::KeyEvent& event );

    // Controller navigation: move active video by delta column/row in the grid
    void navigateActive( int dc, int dr );
    // Select the currently active video (as if double-clicked)
    void selectActive();
    // Zoom around the screen center (positive = in, negative = out)
    void zoom( float increment );

    void setSelectionCallback(SelectionCallback callback) { mSelectionCallback = callback; }

    bool hasHoveredVideo() const;

private:
    void loadVideosFromDirectory(const std::string& dirPath);
    void loadThumbnail(VideoItem& item);
    void calculateThumbnailRects();
    void updateHoverState(const ci::ivec2& mousePos);
    void setActiveIndex(int index, bool centerView = false);
    void selectVideo(int index);
    void navigateToDirectory(const std::string& dirPath);
    void fitToWindow(const ci::ivec2& windowSize);

    static ci::Rectf getCenteredRect( const ci::Rectf &r, const ci::vec2 &size );

#ifdef CINDER_MSW
    ci::gl::TextureRef loadWindowsThumbnail(const std::filesystem::path& videoPath);
    ci::Surface8u convertHBitmapToSurface(void* hBitmap);
#endif
    void loadThumbnailFromVideo(VideoItem& item);

    bool mIsOpen{ false };
    std::string mCurrentDirectory;
    std::vector<VideoItem> mVideos;
    std::vector<std::string> mSubdirectories;
    ViewportTransform mTransform;

    static constexpr float THUMBNAIL_SPACING = 10.0f;
    static constexpr float GALLERY_TOP_MARGIN = 100.0f;

    float mThumbnailSize{ 180.0f };
    int mThumbnailsPerRow{ 5 };
    static constexpr float HOVER_SCALE = 1.33f;
    static constexpr float ANIM_DURATION = 0.5f;

    SelectionCallback mSelectionCallback;
    int mActiveIndex{ -1 };
    float mScrollOffset{ 0.0f };
    ci::vec2 mLastMousePos{ 0.0f };
    std::chrono::system_clock::time_point mLastClickTime;
    static constexpr int DoubleClickThreshMS = 500;
};

inline bool GalleryView::hasHoveredVideo() const
{
    return ( mActiveIndex != -1 );
}