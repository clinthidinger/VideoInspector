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
    mHoveredIndex = -1;
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
                    loadThumbnail(item);
                    mVideos.push_back(item);
                }
            }
        }

        // Sort subdirectories and videos alphabetically
        std::sort(mSubdirectories.begin(), mSubdirectories.end());

        calculateThumbnailRects();
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
                int thumbSize = static_cast<int>(THUMBNAIL_SIZE * 1.5f); // Request larger for better quality
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
        int row = i / THUMBNAILS_PER_ROW;
        int col = i % THUMBNAILS_PER_ROW;

        float x = THUMBNAIL_SPACING + col * (THUMBNAIL_SIZE + THUMBNAIL_SPACING);
        float y = startY + THUMBNAIL_SPACING + row * (THUMBNAIL_SIZE + THUMBNAIL_SPACING);

        mVideos[i].rect = ci::Rectf(x, y, x + THUMBNAIL_SIZE, y + THUMBNAIL_SIZE);

        // Calculate expanded rect (centered expansion)
        float expandedSize = THUMBNAIL_SIZE * HOVER_SCALE;
        float offset = (expandedSize - THUMBNAIL_SIZE) / 2.0f;
        mVideos[i].expandedRect = ci::Rectf(
            x - offset, y - offset,
            x + THUMBNAIL_SIZE + offset, y + THUMBNAIL_SIZE + offset
        );
    }
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
    if( mHoveredIndex != -1 )
    {
        auto &video = mVideos[mHoveredIndex];
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

void GalleryView::updateHoverState(const ci::ivec2& mousePos)
{
    int newHoveredIndex = -1;

    // Adjust mouse position for scroll
    ci::vec2 adjustedPos(mousePos.x, mousePos.y + mScrollOffset);

    // Check which video is hovered
    for (size_t i = 0; i < mVideos.size(); i++)
    {
        if (mVideos[i].rect.contains(adjustedPos))
        {
            newHoveredIndex = static_cast<int>(i);
            break;
        }
    }
    //spdlog::info( "newHoveredIndex, mHoveredIndex: {}, {}", newHoveredIndex, mHoveredIndex );
    //CI_LOG_E( "newHoveredIndex: " + std::to_string( newHoveredIndex ) +
    //          " mHoveredIndex: " + std::to_string( mHoveredIndex ) );

    // Handle hover state changes
    if (newHoveredIndex != mHoveredIndex)
    {
        // Stop previously hovered video
        if (mHoveredIndex >= 0 && mHoveredIndex < mVideos.size())
        {
            auto& prevVideo = mVideos[mHoveredIndex];
            prevVideo.isHovered = false;
            if (prevVideo.movie)
            {
                prevVideo.movie->stop();
                //prevVideo.movie->seekToStart();
                prevVideo.isReadyConn.disconnect();
                prevVideo.movie.reset();
            }
            // Animate scale back to 1.0
            ci::app::timeline().apply(&prevVideo.scale, 1.0f, ANIM_DURATION);
        }

        // Start newly hovered video
        if (newHoveredIndex >= 0 && newHoveredIndex < mVideos.size())
        {
            auto& newVideo = mVideos[newHoveredIndex];
            newVideo.isHovered = true; // same thing???
#ifdef CINDER_MSW
            newVideo.movie = AxMovie::create( newVideo.path.string() );
#else
            newVideo.movie = ci::qtime::MovieGl::create( newVideo.path.string() );
#endif
            newVideo.isReadyConn = newVideo.movie->getIsReadySignal().connect(
                [this, &newVideo]()
                {
                    //resetPanZoom();
                    //mTotalFrameCount = mMovie->getFrameCount();
                    //mLoopEndFrame = mTotalFrameCount;
                    //mMovie->setRate( mRate );
                    //if( wasPlaying )
                    //{
                    if( !newVideo.movie->isPlaying() )
                    {
                        newVideo.movie->play();
                    }
                    //}
                    //mSignalIsReady.emit();
                } );
            if (newVideo.movie)
            {
                newVideo.movie->play();
            }

            // Animate scale to HOVER_SCALE
            ci::app::timeline().apply(&newVideo.scale, HOVER_SCALE, ANIM_DURATION);
        }

        mHoveredIndex = newHoveredIndex;
    }
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
    ci::gl::translate(0.0f, -mScrollOffset);

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
            ci::gl::translate(center);
            //ci::gl::scale(video.scale.value(), video.scale.value());
            ci::gl::translate(-center);

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
                ci::gl::drawStrokedRect(video.rect, 1.0f);
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

    if( mHoveredIndex != -1 )
    {
        auto &video = mVideos[mHoveredIndex];
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
    ci::vec2 adjustedPos(pos.x, pos.y + mScrollOffset);

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
            selectVideo(static_cast<int>(i));
            return;
        }
    }
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

void GalleryView::mouseWheel(const ci::ivec2& pos, float increment)
{
    if (!mIsOpen)
    {
        return;
    }

    // Scroll the gallery
    mScrollOffset -= increment * 20.0f;
    mScrollOffset = std::max(0.0f, mScrollOffset);

    // Update hover state after scrolling
    updateHoverState(pos);
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
