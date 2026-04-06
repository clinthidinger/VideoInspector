#include "GalleryView.h"
#include <algorithm>
#include <cinder/app/App.h>
#include <cinder/ImageIo.h>
#include <cinder/Log.h>
#include <cinder/Timeline.h>
#include <spdlog/spdlog.h>

#ifdef CINDER_MSW
#include <windows.h>
#include <shobjidl.h>   // For IShellItemImageFactory
#include <shlobj.h>     // For SHCreateItemFromParsingName
#include <comdef.h>
#pragma comment(lib, "ole32.lib")
#endif

namespace fs = std::filesystem;

//GalleryView::GalleryView()
//{
//}
//
//GalleryView::~GalleryView()
//{
//}

void GalleryView::open(const std::string& directoryPath)
{
    if (directoryPath.empty())
    {
        return;
    }

    mIsOpen = true;
    mCurrentDirectory = directoryPath;
    mScrollOffset = 0.0f;
    mTransform.reset();
    mLastClickTime = std::chrono::system_clock::now();
    loadVideosFromDirectory(directoryPath);
}

void GalleryView::close()
{
    // Stop all playing videos
    for (auto& video : mVideos)
    {
        if (video.movie && video.movie->isPlaying())
        {
            video.movie->stop();
        }
    }

    mVideos.clear();
    mSubdirectories.clear();
    mIsOpen = false;
    mActiveIndex = -1;
}

void GalleryView::loadVideosFromDirectory(const std::string& dirPath)
{
    mVideos.clear();
    mSubdirectories.clear();

    static const std::vector<std::string> VideoExtensions =
    {
        ".mp4", ".avi", ".mkv", ".mov", ".wmv", ".flv", ".webm", ".m4v", ".mpg", ".mpeg", ".3gp"
    };

    try
    {
        // Find subdirectories
        for (const auto& entry : fs::directory_iterator(dirPath))
        {
            if (entry.is_directory())
            {
                mSubdirectories.push_back(entry.path().filename().string());
            }
        }

        // Find video files
        for (const auto& entry : fs::directory_iterator(dirPath))
        {
            if (entry.is_regular_file())
            {
                std::string extension = entry.path().extension().string();
                std::transform(extension.begin(), extension.end(), extension.begin(),
                    [](unsigned char c) { return std::tolower(c); });

                if (std::find(VideoExtensions.begin(), VideoExtensions.end(), extension) != VideoExtensions.end())
                {
                    VideoItem item;
                    item.path = entry.path();
                    loadThumbnail(item); // TODO: background thread.
                    mVideos.push_back(item);
                }
            }
        }

        // Sort subdirectories and videos alphabetically
        std::sort(mSubdirectories.begin(), mSubdirectories.end());

        fitToWindow( ci::app::getWindowSize() );
    }
    catch (const fs::filesystem_error& e)
    {
        CI_LOG_E("Error accessing directory: " << e.what());
    }
}

#ifdef CINDER_MSW
ci::Surface8u GalleryView::convertHBitmapToSurface(void* hBitmapVoid)
{
    HBITMAP hBitmap = static_cast<HBITMAP>(hBitmapVoid);

    BITMAP bmp = {};
    GetObject(hBitmap, sizeof(BITMAP), &bmp);

    BITMAPINFOHEADER bi = {};
    bi.biSize = sizeof(BITMAPINFOHEADER);
    bi.biWidth = bmp.bmWidth;
    bi.biHeight = -bmp.bmHeight; // Negative for top-down DIB
    bi.biPlanes = 1;
    bi.biBitCount = 32;
    bi.biCompression = BI_RGB;

    int dataSize = bmp.bmWidth * bmp.bmHeight * 4;
    std::vector<uint8_t> pixels(dataSize);

    HDC hDC = GetDC(nullptr);
    GetDIBits(hDC, hBitmap, 0, bmp.bmHeight, pixels.data(),
              reinterpret_cast<BITMAPINFO*>(&bi), DIB_RGB_COLORS);
    ReleaseDC(nullptr, hDC);

    // Create Cinder surface from pixel data (BGRA format)
    ci::Surface8u surface(pixels.data(), bmp.bmWidth, bmp.bmHeight,
                          bmp.bmWidth * 4, ci::SurfaceChannelOrder::BGRA);

    return surface.clone();
}

ci::gl::TextureRef GalleryView::loadWindowsThumbnail(const std::filesystem::path& videoPath)
{
    // Initialize COM for this thread if needed
    static bool comInitialized = false;
    if (!comInitialized)
    {
        CoInitialize(nullptr);
        comInitialized = true;
    }

    ci::gl::TextureRef texture;

    try
    {
        // Convert path to wide string
        std::wstring wPath = videoPath.wstring();

        IShellItem* pShellItem = nullptr;
        HRESULT hr = SHCreateItemFromParsingName(wPath.c_str(), nullptr,
                                                  IID_PPV_ARGS(&pShellItem));

        if (SUCCEEDED(hr))
        {
            IShellItemImageFactory* pImageFactory = nullptr;
            hr = pShellItem->QueryInterface(IID_PPV_ARGS(&pImageFactory));

            if (SUCCEEDED(hr))
            {
                // Request thumbnail size
                int thumbSize = static_cast<int>(mThumbnailSize * 1.5f); // Request larger for better quality
                SIZE size = { thumbSize, thumbSize };
                HBITMAP hBitmap = nullptr;

                // SIIGBF_THUMBNAILONLY - use thumbnail cache
                // SIIGBF_BIGGERSIZEOK - allow larger size if available
                hr = pImageFactory->GetImage(size, SIIGBF_THUMBNAILONLY | SIIGBF_BIGGERSIZEOK, &hBitmap);

                if (SUCCEEDED(hr) && hBitmap)
                {
                    // Convert HBITMAP to Cinder Surface
                    ci::Surface8u surface = convertHBitmapToSurface(hBitmap);

                    // Create texture from surface
                    texture = ci::gl::Texture::create(surface);

                    DeleteObject(hBitmap);
                }
                else
                {
                    CI_LOG_W("GetImage failed for: " << videoPath.string() << " HRESULT: 0x" << std::hex << hr);
                }

                pImageFactory->Release();
            }
            pShellItem->Release();
        }
        else
        {
            CI_LOG_W("SHCreateItemFromParsingName failed for: " << videoPath.string() << " HRESULT: 0x" << std::hex << hr);
        }
    }
    catch (const std::exception& e)
    {
        CI_LOG_E("Error loading Windows thumbnail for " << videoPath << ": " << e.what());
    }

    return texture;
}
#endif

void GalleryView::loadThumbnailFromVideo(VideoItem& item)
{
    try
    {
#ifdef CINDER_MSW
        item.movie = AxMovie::create(item.path.string());
#else
        item.movie = ci::qtime::MovieGl::create(item.path.string());
#endif

        if (item.movie)
        {
            // Seek to start and pause to get first frame
            item.movie->seekToStart();
            item.movie->stop();

#ifdef CINDER_MSW
            // Wait for movie to be ready
            if (item.movie->isReady())
            {
                auto lease = item.movie->getTexture();
                if (lease && lease->ToTexture())
                {
                    item.thumbnail = lease->ToTexture();
                }
            }
#else
            // For non-Windows platforms
            auto tex = item.movie->getTexture();
            if (tex)
            {
                item.thumbnail = tex;
            }
#endif
        }
    }
    catch (const std::exception& e)
    {
        CI_LOG_E("Error loading video thumbnail for " << item.path << ": " << e.what());
    }
}

void GalleryView::loadThumbnail(VideoItem& item)
{
#ifdef CINDER_MSW
    // Try Windows thumbnail first (faster, uses cache)
    item.thumbnail = loadWindowsThumbnail(item.path);

    // If Windows thumbnail failed, fall back to video loading
    if (!item.thumbnail)
    {
        CI_LOG_I("Windows thumbnail failed for " << item.path.filename() << ", falling back to video loading");
        loadThumbnailFromVideo(item);
    }
#else
    // On non-Windows platforms, use video loading
    loadThumbnailFromVideo(item);
#endif
}

void GalleryView::calculateThumbnailRects()
{
    const float startY = GALLERY_TOP_MARGIN;

    for (size_t i = 0; i < mVideos.size(); i++)
    {
        int row = i / mThumbnailsPerRow;
        int col = i % mThumbnailsPerRow;

        float x = THUMBNAIL_SPACING + col * (mThumbnailSize + THUMBNAIL_SPACING);
        float y = startY + THUMBNAIL_SPACING + row * (mThumbnailSize + THUMBNAIL_SPACING);

        mVideos[i].rect = ci::Rectf(x, y, x + mThumbnailSize, y + mThumbnailSize);

        // Calculate expanded rect (centered expansion)
        float expandedSize = mThumbnailSize * HOVER_SCALE;
        float offset = (expandedSize - mThumbnailSize) / 2.0f;
        mVideos[i].expandedRect = ci::Rectf(
            x - offset, y - offset,
            x + mThumbnailSize + offset, y + mThumbnailSize + offset
        );
    }
}

void GalleryView::fitToWindow(const ci::ivec2& windowSize)
{
    if( mVideos.empty() )
    {
        calculateThumbnailRects();
        return;
    }

    const float availW = static_cast<float>( windowSize.x );
    const float availH = static_cast<float>( windowSize.y ) - GALLERY_TOP_MARGIN;
    const int N = static_cast<int>( mVideos.size() );

    float bestSize = 0.0f;
    int bestCols = 1;

    for( int cols = 1; cols <= N; ++cols )
    {
        const int rows = ( N + cols - 1 ) / cols;
        const float thumbW = ( availW - THUMBNAIL_SPACING * ( cols + 1 ) ) / cols;
        const float thumbH = ( availH - THUMBNAIL_SPACING * ( rows + 1 ) ) / rows;
        const float thumbSize = std::min( thumbW, thumbH );

        if( thumbSize > bestSize )
        {
            bestSize  = thumbSize;
            bestCols  = cols;
        }
    }

    mThumbnailSize   = std::max( bestSize, 40.0f );
    mThumbnailsPerRow = bestCols;

    calculateThumbnailRects();
    mTransform.reset();
}

void GalleryView::update()
{
    if (!mIsOpen)
    {
        return;
    }

    /*
    // Should have a handle to playing video.  No for loop!!!
    // Update playing videos
    //for (auto& video : mVideos)
    if( mActiveIndex != -1 )
    {
        auto &video = mVideos[mActiveIndex];
        if (video.isHovered && video.movie && video.movie->isPlaying())
        {
#ifdef CINDER_MSW
            if (video.movie->isReady())
            {
                auto lease = video.movie->getTexture();
                if (lease && lease->ToTexture())
                {
                    video.videoFrame = lease->ToTexture();
                }
            }
#else
            auto tex = video.movie->getTexture();
            if (tex)
            {
                video.videoFrame = tex;
            }
#endif
        }
    }
    */
}

void GalleryView::setActiveIndex(int newIndex, bool centerView)
{
    if( newIndex == mActiveIndex ) return;

    // Deactivate previously active video
    if( ( mActiveIndex >= 0 ) && ( mActiveIndex < (int)mVideos.size() ) )
    {
        auto& prev = mVideos[mActiveIndex];
        prev.isHovered = false;
        if( prev.movie )
        {
            prev.movie->stop();
            prev.isReadyConn.disconnect();
            prev.movie.reset();
        }
        ci::app::timeline().apply( &prev.scale, 1.0f, ANIM_DURATION );
    }

    // Activate newly selected video
    if( newIndex >= 0 && newIndex < (int)mVideos.size() )
    {
        auto& newVid = mVideos[newIndex];
        newVid.isHovered = true;
#ifdef CINDER_MSW
        newVid.movie = AxMovie::create( newVid.path.string() );
#else
        newVid.movie = ci::qtime::MovieGl::create( newVid.path.string() );
#endif
        newVid.isReadyConn = newVid.movie->getIsReadySignal().connect(
            [this, &newVid]()
            {
                if( !newVid.movie->isPlaying() )
                {
                    newVid.movie->play();
                }
            } );
        if( newVid.movie )
        {
            newVid.movie->play();
        }
        ci::app::timeline().apply( &newVid.scale, HOVER_SCALE, ANIM_DURATION );

        if( centerView )
        {
            // Pan so the active item is near the screen center
            ci::vec2 contentCenter   = mVideos[newIndex].rect.getCenter();
            ci::vec2 currentOnScreen = ci::vec2( mTransform.getMatrix() * ci::vec4( contentCenter, 0.0f, 1.0f ) );
            ci::vec2 delta           = ci::app::getWindowCenter() - currentOnScreen;
            mTransform.setTranslation( mTransform.getTranslation() + delta );
        }
    }

    mActiveIndex = newIndex;
}

void GalleryView::updateHoverState(const ci::ivec2& mousePos)
{
    const ci::vec2 adjustedPos = mTransform.getInverseMatrix() * ci::vec4( ci::vec2( mousePos ), 0.0f, 1.0f );

    int newIndex = -1;
    for( size_t i = 0; i < mVideos.size(); ++i )
    {
        if( mVideos[i].rect.contains( adjustedPos ) )
        {
            newIndex = static_cast<int>( i );
            break;
        }
    }

    setActiveIndex( newIndex );
}

void GalleryView::navigateActive( int dc, int dr )
{
    if( mVideos.empty() ) return;

    int startIdx = ( mActiveIndex >= 0 ) ? mActiveIndex : 0;
    int newIdx = startIdx + dc + dr * mThumbnailsPerRow;
    newIdx = std::clamp( newIdx, 0, static_cast<int>( mVideos.size() ) - 1 );
    setActiveIndex( newIdx, /*centerView=*/true );
}

void GalleryView::selectActive()
{
    selectVideo( mActiveIndex );
}

void GalleryView::zoom( float increment )
{
    ci::vec2 center = ci::app::getWindowCenter();
    mTransform.mouseWheel( center, increment );
}

void GalleryView::draw()
{
    if (!mIsOpen)
    {
        return;
    }

    // Draw semi-transparent background
    ci::gl::ScopedColor colorScope;
    ci::gl::color(0.0f, 0.0f, 0.0f, 0.85f);
    ci::gl::drawSolidRect(ci::app::getWindowBounds());

    // Set up matrices for scrolling
    ci::gl::ScopedMatrices matricesScope;
    //ci::gl::translate(0.0f, -mScrollOffset);
    ci::gl::multModelMatrix( mTransform.getMatrix() );

    // Draw header with directory info
    ci::gl::color(1.0f, 1.0f, 1.0f);

    // Draw directory navigation buttons
    float buttonY = 20.0f;
    float buttonX = 20.0f;

    // Parent directory button
    if (!mCurrentDirectory.empty())
    {
        fs::path currentPath(mCurrentDirectory);
        if (currentPath.has_parent_path())
        {
            ci::gl::color(0.3f, 0.3f, 0.3f);
            ci::Rectf parentButton(buttonX, buttonY, buttonX + 100, buttonY + 30);
            ci::gl::drawSolidRect(parentButton);
            ci::gl::color(1.0f, 1.0f, 1.0f);
            ci::gl::drawStringCentered("Parent", parentButton.getCenter(), ci::Color::white());
            buttonX += 110;
        }
    }

    // Draw current directory name
    ci::gl::color(1.0f, 1.0f, 1.0f);
    ci::gl::drawString("Directory: " + mCurrentDirectory, ci::vec2(buttonX, buttonY + 10), ci::Color::white());

    // Draw subdirectory buttons
    buttonY = 60.0f;
    buttonX = 20.0f;
    for (const auto& subdir : mSubdirectories)
    {
        ci::gl::color(0.2f, 0.4f, 0.6f);
        ci::Rectf subdirButton(buttonX, buttonY, buttonX + 150, buttonY + 30);
        ci::gl::drawSolidRect(subdirButton);
        ci::gl::color(1.0f, 1.0f, 1.0f);

        // Truncate long names
        std::string displayName = subdir;
        if (displayName.length() > 18)
        {
            displayName = displayName.substr(0, 15) + "...";
        }
        ci::gl::drawStringCentered(displayName, subdirButton.getCenter(), ci::Color::white());

        buttonX += 160;
        if (buttonX > ci::app::getWindowWidth() - 160)
        {
            buttonX = 20.0f;
            buttonY += 40.0f;
        }
    }

    // Draw video thumbnails
    for (size_t i = 0; i < mVideos.size(); i++)
    {
        const auto& video = mVideos[i];

        if (video.thumbnail)
        {
            ci::gl::ScopedMatrices thumbScope;

            // Apply scaling animation
            ci::vec2 center = video.rect.getCenter();
            //ci::gl::translate(center);
            //ci::gl::scale(video.scale.value(), video.scale.value());
            //ci::gl::translate(-center);

            // Draw thumbnail
            ci::gl::color(1.0f, 1.0f, 1.0f);
            const ci::Rectf rect = getCenteredRect( video.rect, video.thumbnail->getSize() );
            ci::gl::draw( video.thumbnail, rect );

            // Draw border
            if (!video.isHovered )
            {
               /* ci::gl::color(1.0f, 0.5f, 0.0f);
                ci::gl::drawStrokedRect(video.rect, 3.0f);
            }
            else
            {*/
                ci::gl::color(0.5f, 0.5f, 0.5f);
                const float lineWidth = std::max( mTransform.getScale(), 1.0f );
                ci::gl::drawStrokedRect( video.rect, lineWidth );
            }
        }
        else
        {
            // Draw placeholder if thumbnail not loaded
            ci::gl::color(0.2f, 0.2f, 0.2f);
            ci::gl::drawSolidRect(video.rect);
            ci::gl::color(0.5f, 0.5f, 0.5f);
            ci::gl::drawStrokedRect(video.rect);
            ci::gl::color(1.0f, 1.0f, 1.0f);
            ci::gl::drawStringCentered("Loading...", video.rect.getCenter(), ci::Color::white());
        }

        // Draw filename below thumbnail
        ci::gl::color(1.0f, 1.0f, 1.0f);
        std::string filename = video.path.filename().string();
        if (filename.length() > 25)
        {
            filename = filename.substr(0, 22) + "...";
        }
        ci::gl::drawStringCentered(filename, ci::vec2(video.rect.getCenter().x, video.rect.y2 + 15), ci::Color::white());
    }

    if( mActiveIndex != -1 )
    {
        auto &video = mVideos[mActiveIndex];
        if( video.movie && video.movie->isReady() && video.movie->isPlaying() )
        {
            {
                const float alpha = ci::lmap<float>( video.scale.value() - 1.0f, 0.0f, HOVER_SCALE - 1.0f, 0.0f, 0.5f );
                ci::gl::ScopedColor scopedColor( ci::ColorAf( 0.0f, 0.0f, 0.0f, alpha ) );
                ci::gl::drawSolidRect(ci::app::getWindowBounds() );
            }

            ci::gl::ScopedMatrices thumbScope;
            ci::vec2 center = video.rect.getCenter();
            ci::gl::translate( center );
            ci::gl::scale( video.scale.value(), video.scale.value() );
            ci::gl::translate( -center );

            {
                ci::gl::ScopedColor scopedColor( ci::Color::black() );
                ci::gl::drawSolidRect( video.rect );
            }

            const ci::Rectf rect = getCenteredRect( video.rect, video.movie->getSize() );
            ci::gl::draw( *video.movie->getTexture(), rect );

            ci::gl::ScopedColor scopedColor( ci::Color::white() );
            ci::gl::drawStrokedRect( video.rect );
        }
    }

    // Draw instruction text if no videos
    if (mVideos.empty())
    {
        ci::gl::color(1.0f, 1.0f, 1.0f);
        ci::gl::drawStringCentered("No video files found in this directory",
            ci::app::getWindowCenter(), ci::Color::white());
    }
}

void GalleryView::resize( const ci::ivec2 &size )
{
    if( !mIsOpen ) return;
    fitToWindow( size );
}

ci::Rectf GalleryView::getCenteredRect( const ci::Rectf &r, const ci::vec2 &size )
{
    float xScale = 1.0f;
    float yScale = 1.0f;
    if( size.x > size.y )
    {
        yScale = size.y / size.x;
    }
    else
    {
        xScale = size.x / size.y;
    }
    auto const scale = r.getSize() / size;
    auto const scaleScalar = std::min<float>( scale.x, scale.y );
    auto const offset = r.getSize() / 2.0f - ( ci::vec2( size / 2.0f ) * scaleScalar );
    const ci::Rectf rect( r.x1 + offset.x, r.y1 + offset.y,
                          r.x1 + offset.x + ( r.getWidth() * xScale ),
                          r.y1 + offset.y + ( r.getHeight() * yScale ) );

    return rect;
}

void GalleryView::mouseDown(const ci::ivec2& pos)
{
    if (!mIsOpen)
    {
        return;
    }

    // Adjust for scroll
    const ci::vec2 adjustedPos = mTransform.getInverseMatrix() * ci::vec4( ci::vec2( pos ), 0.0f, 1.0f );

    // Check for directory navigation clicks
    float buttonY = 20.0f;
    float buttonX = 20.0f;

    // Check parent button
    fs::path currentPath(mCurrentDirectory);
    if (currentPath.has_parent_path())
    {
        ci::Rectf parentButton(buttonX, buttonY, buttonX + 100, buttonY + 30);
        if (parentButton.contains(adjustedPos))
        {
            navigateToDirectory(currentPath.parent_path().string());
            return;
        }
        buttonX += 110;
    }

    // Check subdirectory buttons
    buttonY = 60.0f;
    buttonX = 20.0f;
    for (const auto& subdir : mSubdirectories)
    {
        ci::Rectf subdirButton(buttonX, buttonY, buttonX + 150, buttonY + 30);
        if (subdirButton.contains(adjustedPos))
        {
            navigateToDirectory((currentPath / subdir).string());
            return;
        }

        buttonX += 160;
        if (buttonX > ci::app::getWindowWidth() - 160)
        {
            buttonX = 20.0f;
            buttonY += 40.0f;
        }
    }

    // Check for video selection (double-click will be handled by the app)
    for (size_t i = 0; i < mVideos.size(); i++)
    {
        if (mVideos[i].rect.contains(adjustedPos))
        {
            auto nowTime = std::chrono::system_clock::now();
            if( std::chrono::duration_cast< std::chrono::milliseconds >( nowTime - mLastClickTime ).count() < DoubleClickThreshMS )
            {
                selectVideo( static_cast<int>( i ) ); 
                return;
            }
            mLastClickTime = nowTime;
        }
    }

    mTransform.mouseDown( pos );
}

void GalleryView::mouseDrag( const ci::ivec2 &pos )
{
    if( !mIsOpen )
    {
        return;
    }
    mTransform.mouseDrag( pos );
}

void GalleryView::mouseMove(const ci::ivec2& pos)
{
    if (!mIsOpen)
    {
        return;
    }

    mLastMousePos = pos;
    updateHoverState(pos);
}

void GalleryView::mouseWheel(const ci::ivec2& pos, float increment, bool ctrlDown)
{
    if (!mIsOpen)
    {
        return;
    }

    if( ctrlDown )
    {
        // Ctrl+scroll (or trackpad pinch) → zoom
        mTransform.mouseWheel( pos, increment );
    }
    else
    {
        // Plain scroll → pan vertically
        static constexpr float PanStep = 60.0f;
        mTransform.pan( ci::vec2( 0.0f, increment * PanStep ) );
        updateHoverState( pos );
    }
}

void GalleryView::keyDown( const ci::app::KeyEvent& event )
{
    if( !mIsOpen ) return;

    const float step = ( mThumbnailSize + THUMBNAIL_SPACING ) * mTransform.getScale();
    const float pageStep = static_cast<float>( ci::app::getWindowHeight() );

    switch( event.getCode() )
    {
        case ci::app::KeyEvent::KEY_UP:
            mTransform.pan( ci::vec2( 0.0f,  step ) );
            break;
        case ci::app::KeyEvent::KEY_DOWN:
            mTransform.pan( ci::vec2( 0.0f, -step ) );
            break;
        case ci::app::KeyEvent::KEY_LEFT:
            mTransform.pan( ci::vec2(  step, 0.0f ) );
            break;
        case ci::app::KeyEvent::KEY_RIGHT:
            mTransform.pan( ci::vec2( -step, 0.0f ) );
            break;
        case ci::app::KeyEvent::KEY_PAGEUP:
            mTransform.pan( ci::vec2( 0.0f,  pageStep ) );
            break;
        case ci::app::KeyEvent::KEY_PAGEDOWN:
            mTransform.pan( ci::vec2( 0.0f, -pageStep ) );
            break;
        case ci::app::KeyEvent::KEY_RETURN:
        case ci::app::KeyEvent::KEY_KP_ENTER:
            selectActive();
            break;
        default:
            break;
    }
}

void GalleryView::selectVideo(int index)
{
    if (index >= 0 && index < mVideos.size())
    {
        if (mSelectionCallback)
        {
            mSelectionCallback(mVideos[index].path.string());
        }
        close();
    }
}

void GalleryView::navigateToDirectory(const std::string& dirPath)
{
    if (fs::exists(dirPath) && fs::is_directory(dirPath))
    {
        close();
        open(dirPath);
    }
}
