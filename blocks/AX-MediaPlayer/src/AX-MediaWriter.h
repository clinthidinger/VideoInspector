#pragma once
#include "cinder/app/RendererGl.h"
#include "cinder/app/App.h"
#include "cinder/gl/gl.h"
#include "cinder/audio/audio.h"
#include "guiddef.h"
#include <Windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <Mfreadwrite.h>

#include <mferror.h>
#include <AX-MediaPlayer.h>

namespace AX::Video
{
    // MediaWriter was built by using tutorial found here:
    // https://docs.microsoft.com/en-us/windows/win32/medfound/tutorial--using-the-sink-writer-to-encode-video

    using MediaWriterRef = std::shared_ptr<class MediaWriter>;
    class MediaWriter : public ci::Noncopyable
    {
    public:
        static  MediaWriterRef Create ( const ci::fs::path & filePath, const ci::ivec2& size, int bitrate, int fps, bool enableAudio = false, int sampleRate = 44100 );
        MediaWriter ( const ci::fs::path & filePath, const ci::ivec2& size, int bitrate, int fps, bool enableAudio = false, int sampleRate = 44100 );
        ~MediaWriter ( );

        bool Write ( ci::gl::TextureRef textureRef, bool flipUpDown = true, bool flipLeftRight = false, bool reverseRgb = false );
        bool WriteAudio ( const float* audioData, size_t sampleCount );
        bool WriteAudio ( const float* audioData, size_t sampleCount, int channels, int sampleRate );
        bool Finalize ( );
    protected:
        HRESULT InitializeSinkWriter ( );
        HRESULT WriteFrame ( BYTE* videoBuffer );
        HRESULT WriteAudioFrame ( const float* audioData, size_t sampleCount, long duration );
        HRESULT InitializeAudioStreamInline ( IMFSinkWriter* pSinkWriter, DWORD* pAudioStreamIndex );

        std::unique_ptr<IMFSinkWriter, std::function<void ( IMFSinkWriter* )>> _pSinkWriter;
        ci::ivec2 _size;
        DWORD _videoStream = -1;
        DWORD _audioStream = -1;
        LONGLONG _videoRtStart = 0;
        LONGLONG _audioRtStart = 0;
        long _videoFrameDuration = 0;
        long _audioFrameDuration = 0;
        std::vector<DWORD> _videoFrameBuffer;
        bool _isReady = false;
        bool _enableAudio = false;
        ci::gl::FboRef _fbo;
        int _videoBitrate = 0;
        int _framerate = 0;
        int _sampleRate = 44100;
        int _channels = 2;
        ci::fs::path _filePath;
        
        ci::gl::GlslProgRef _rgbSwapShader;
        void InitializeShaders();

        template <class T> void mfSafeRelease ( T** ppT )
        {
            if ( *ppT )
            {
                ( *ppT )->Release ( );
                *ppT = nullptr;
            }
        }

    };

}