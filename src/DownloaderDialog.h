#pragma once

#include <cinder/Vector.h>

class IDownloadModel;

namespace downloader
{
bool drawDownloaderDialog( const ci::ivec2 &size, IDownloadModel &model );
}
