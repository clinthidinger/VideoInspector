#include "AX-MediaWriter.h"
#include <algorithm>
#include <cmath>

namespace AX::Video
{
    MediaWriterRef MediaWriter::Create ( const ci::fs::path & filePath, const ci::ivec2& size, int bitrate, int fps, bool enableAudio, int sampleRate )
    {
        return MediaWriterRef ( new MediaWriter ( filePath, size, bitrate, fps, enableAudio, sampleRate ) );
    }

    MediaWriter::MediaWriter ( const ci::fs::path & filePath, const ci::ivec2& size, int bitrate, int fps, bool enableAudio, int sampleRate ) : _pSinkWriter ( nullptr, [this] ( IMFSinkWriter* sw ) { mfSafeRelease ( &sw ); } )
    {
        _filePath = filePath;
        _size = size;
        _videoBitrate = bitrate;
        _framerate = fps;
        _enableAudio = enableAudio;
        _sampleRate = sampleRate;
        
        ci::app::console() << "MediaWriter constructor - enableAudio: " << (enableAudio ? "true" : "false") << ", sampleRate: " << sampleRate << std::endl;

        //_videoFrameDuration = (long)(0.4055375 * ( 10 * 1000 * 1000 / fps ));
        _videoFrameDuration = ( long ) ( 10 * 1000 * 1000 / fps );
        _audioFrameDuration = ( long ) ( 10 * 1000 * 1000.0 * 1024 / _sampleRate ); // 1024 samples per frame

        _videoFrameBuffer.resize ( _size.x * _size.y );
        _videoFrameBuffer.clear ( );

        HRESULT hr = CoInitializeEx ( nullptr, COINIT_APARTMENTTHREADED );
        if ( SUCCEEDED ( hr ) )
        {
            hr = MFStartup ( MF_VERSION );
            if ( SUCCEEDED ( hr ) )
            {
                hr = InitializeSinkWriter ( );
            }
            else
            {
                ci::app::console() << "MFStartup failed: 0x" << std::hex << hr << std::endl;
            }
        }
        else
        {
            ci::app::console() << "CoInitializeEx failed: 0x" << std::hex << hr << std::endl;
        }

        if ( SUCCEEDED ( hr ) )
        {
            InitializeShaders();
            _fbo = ci::gl::Fbo::create ( _size.x, _size.y, ci::gl::Fbo::Format ( ).disableDepth ( ) );

            _videoRtStart = 0;
            _audioRtStart = 0;
            _isReady = true;
            ci::app::console() << "MediaWriter initialized successfully" << std::endl;
        }
        else
        {
            ci::app::console() << "MediaWriter initialization failed: 0x" << std::hex << hr << std::endl;
        }
    }

    bool MediaWriter::Finalize ( )
    {
        HRESULT hr = E_FAIL;

        if ( _isReady && _pSinkWriter )
        {
            ci::app::console() << "Finalizing MediaWriter..." << std::endl;
            hr = _pSinkWriter->Finalize ( );
            if ( SUCCEEDED ( hr ) )
            {
                ci::app::console() << "MediaWriter finalized successfully" << std::endl;
            }
            else
            {
                ci::app::console() << "MediaWriter finalize failed: 0x" << std::hex << hr << std::endl;
            }
            _isReady = false;
        }
        else
        {
            ci::app::console() << "MediaWriter not ready for finalization" << std::endl;
        }
        return SUCCEEDED ( hr );
    }
    MediaWriter::~MediaWriter ( )
    {
        Finalize ( );

        MFShutdown ( );
        CoUninitialize ( );
    }

    bool MediaWriter::Write( ci::gl::TextureRef textureRef, bool flipUpDown, bool flipLeftRight, bool reverseRgb )
    {
        if ( !_isReady )
            return false;

        HRESULT hr = E_FAIL;

        if ( _pSinkWriter && _fbo )
        {
            {
                ci::gl::ScopedFramebuffer scopedFbo ( _fbo );
                ci::gl::ScopedModelMatrix scopedMM;

                ci::vec2 lowerLeftOrigin( 0, 0 );
                ci::gl::ScopedViewport scopedViewport( lowerLeftOrigin, _fbo->getSize( ) );
                ci::gl::ScopedMatrices scopedMatrices;
                ci::gl::setMatricesWindow( _fbo->getSize( ) );

                if( flipUpDown )
                {
                    ci::gl::translate ( 0.0f, ( float ) _size.y, 0.0f );
                    ci::gl::scale ( 1.0f, -1.0f, 1.0f );
                }
                
                if( reverseRgb && _rgbSwapShader )
                {
                    ci::gl::ScopedGlslProg scopedShader( _rgbSwapShader );
                    ci::gl::ScopedTextureBind scopedTexture( textureRef, 0 );
                    _rgbSwapShader->uniform( "uTexture", 0 );
                    
                    ci::Rectf drawRect;
                    if( flipLeftRight )
                    {
                        drawRect = ci::Rectf( textureRef->getWidth(), 0, 0, textureRef->getHeight() );
                    }
                    else
                    {
                        drawRect = ci::Rectf( 0, 0, textureRef->getWidth(), textureRef->getHeight() );
                    }
                    
                    ci::gl::drawSolidRect( drawRect );
                }
                else
                {
                    if( flipLeftRight )
                    {
                        ci::gl::draw( textureRef, ci::Rectf( textureRef->getWidth(), 0, 0, textureRef->getHeight() ) );
                    }
                    else
                    {
                        ci::gl::draw ( textureRef );
                    }
                }
            }
            auto surface = _fbo->readPixels8u ( _fbo->getBounds ( ) );
            hr = WriteFrame ( surface.getData ( ) ); // Move to separate thread??? Can we add audio???
            if ( !SUCCEEDED ( hr ) )
            {
                ci::app::console ( ) << "error on write" << std::endl;
                return false;
            }
            _videoRtStart += _videoFrameDuration;
            return SUCCEEDED ( hr );
        }

        return SUCCEEDED ( hr );
    }

    HRESULT MediaWriter::InitializeSinkWriter ( )
    {
        _videoStream = -1;
        _audioStream = -1;

        std::unique_ptr<IMFSinkWriter, std::function<void ( IMFSinkWriter* )>> pSinkWriter ( nullptr, [this] ( IMFSinkWriter* sw ) { mfSafeRelease ( &sw ); } );
        std::unique_ptr<IMFMediaType, std::function<void ( IMFMediaType* )>> pMediaTypeOut ( nullptr, [this] ( IMFMediaType* mt ) { mfSafeRelease ( &mt ); } );
        std::unique_ptr<IMFMediaType, std::function<void ( IMFMediaType* )>> pMediaTypeIn ( nullptr, [this] ( IMFMediaType* mt ) { mfSafeRelease ( &mt ); } );
        DWORD videoStreamIndex = -1;
        DWORD audioStreamIndex = -1;

        //std::unique_ptr<IMFSourceReader, std::function<void ( IMFSourceReader* )>> pMediaReader ( nullptr, [this] ( IMFSourceReader* sr ) { mfSafeRelease ( &sr ); } );
        //std::unique_ptr<IMFMediaType, std::function<void ( IMFMediaType* )>> pMediaType ( nullptr, [this] ( IMFMediaType* mt ) { mfSafeRelease ( &mt ); } );

        HRESULT hr = E_FAIL;
        {
            IMFSinkWriter* p;
            auto s = _filePath.string ( );
            auto ws = std::wstring ( s.begin ( ), s.end ( ) );
            hr = MFCreateSinkWriterFromURL ( ws.c_str ( ), nullptr, nullptr, &p );
            pSinkWriter.reset ( p );
        }

        // Set the output media type.
        if ( SUCCEEDED ( hr ) )
        {
            IMFMediaType* p;
            hr = MFCreateMediaType ( &p );
            pMediaTypeOut.reset ( p );
        }
        if ( SUCCEEDED ( hr ) )
        {
            hr = pMediaTypeOut->SetGUID ( MF_MT_MAJOR_TYPE, MFMediaType_Video );
        }
        if ( SUCCEEDED ( hr ) )
        {
            hr = pMediaTypeOut->SetGUID ( MF_MT_SUBTYPE, MFVideoFormat_H264 );
        }
        if ( SUCCEEDED ( hr ) )
        {
            hr = pMediaTypeOut->SetUINT32 ( MF_MT_AVG_BITRATE, _videoBitrate );
        }
        if ( SUCCEEDED ( hr ) )
        {
            hr = pMediaTypeOut->SetUINT32 ( MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive );
        }
        if ( SUCCEEDED ( hr ) )
        {
            hr = MFSetAttributeSize ( pMediaTypeOut.get ( ), MF_MT_FRAME_SIZE, _size.x, _size.y );//VIDEO_WIDTH, VIDEO_HEIGHT);
        }
        if ( SUCCEEDED ( hr ) )
        {
            hr = MFSetAttributeRatio ( pMediaTypeOut.get ( ), MF_MT_FRAME_RATE, _framerate, 1 );
        }
        if ( SUCCEEDED ( hr ) )
        {
            hr = MFSetAttributeRatio ( pMediaTypeOut.get ( ), MF_MT_PIXEL_ASPECT_RATIO, 1, 1 );
        }
        if ( SUCCEEDED ( hr ) )
        {
            hr = pSinkWriter->AddStream ( pMediaTypeOut.get ( ), &videoStreamIndex );
        }

        // Set the input media type.
        if ( SUCCEEDED ( hr ) )
        {
            IMFMediaType* p;
            hr = MFCreateMediaType ( &p );
            pMediaTypeIn.reset ( p );
        }
        if ( SUCCEEDED ( hr ) )
        {
            hr = pMediaTypeIn->SetGUID ( MF_MT_MAJOR_TYPE, MFMediaType_Video );
        }
        if ( SUCCEEDED ( hr ) )
        {
            hr = pMediaTypeIn->SetGUID ( MF_MT_SUBTYPE, MFVideoFormat_RGB32 );
        }
        if ( SUCCEEDED ( hr ) )
        {
            hr = pMediaTypeIn->SetUINT32 ( MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive );
        }
        if ( SUCCEEDED ( hr ) )
        {
            hr = MFSetAttributeSize ( pMediaTypeIn.get ( ), MF_MT_FRAME_SIZE, _size.x, _size.y );//VIDEO_WIDTH, VIDEO_HEIGHT);
        }
        if ( SUCCEEDED ( hr ) )
        {
            hr = MFSetAttributeRatio ( pMediaTypeIn.get ( ), MF_MT_FRAME_RATE, _framerate, 1 );
        }
        if ( SUCCEEDED ( hr ) )
        {
            hr = MFSetAttributeRatio ( pMediaTypeIn.get ( ), MF_MT_PIXEL_ASPECT_RATIO, 1, 1 );
        }
        if ( SUCCEEDED ( hr ) )
        {
            hr = pSinkWriter->SetInputMediaType ( videoStreamIndex, pMediaTypeIn.get ( ), nullptr );
        }

        // Initialize audio stream BEFORE BeginWriting
        audioStreamIndex = -1;
        if ( SUCCEEDED ( hr ) && _enableAudio )
        {
            ci::app::console ( ) << "Initializing audio stream..." << std::endl;
            hr = InitializeAudioStreamInline( pSinkWriter.get(), &audioStreamIndex );
            if ( !SUCCEEDED ( hr ) )
            {
                ci::app::console ( ) << "Audio initialization failed. Falling back to video-only recording." << std::endl;
                _enableAudio = false;
                hr = S_OK; // Reset hr to allow video-only recording
            }
            else
            {
                ci::app::console ( ) << "Audio stream initialized successfully." << std::endl;
            }
        }

        // Tell the sink writer to start accepting data.
        if ( SUCCEEDED ( hr ) )
        {
            ci::app::console ( ) << "Calling BeginWriting..." << std::endl;
            hr = pSinkWriter->BeginWriting ( );
            if ( !SUCCEEDED ( hr ) )
            {
                ci::app::console ( ) << "BeginWriting failed: 0x" << std::hex << hr << std::dec << std::endl;
            }
            else
            {
                ci::app::console ( ) << "BeginWriting succeeded" << std::endl;
            }
        }

        if ( hr == MF_E_TOPO_CODEC_NOT_FOUND )
        {
            ci::app::console ( ) << "codec not found" << std::endl;
        }
        else if ( !SUCCEEDED ( hr ) )
        {
            ci::app::console ( ) << "InitializeSinkWriter failed: 0x" << std::hex << hr << std::dec << std::endl;
        }

        // Return the pointer to the caller.
        if ( SUCCEEDED ( hr ) )
        {
            _pSinkWriter = std::move ( pSinkWriter );
            _pSinkWriter.get ( )->AddRef ( );
            _videoStream = videoStreamIndex;
            _audioStream = audioStreamIndex;
            
            if ( _enableAudio && audioStreamIndex != -1 )
            {
                ci::app::console ( ) << "Audio stream initialized successfully with index: " << _audioStream << std::endl;
            }
        }

        return hr;
    }

    HRESULT MediaWriter::WriteFrame ( BYTE* videoBuffer )
    {
        std::unique_ptr<IMFSample, std::function<void ( IMFSample* )>> pSample ( nullptr, [this] ( IMFSample* s ) { mfSafeRelease ( &s ); } );
        std::unique_ptr<IMFMediaBuffer, std::function<void ( IMFMediaBuffer* )>> pBuffer ( nullptr, [this] ( IMFMediaBuffer* mb ) { mfSafeRelease ( &mb ); } );

        const LONG cbWidth = 4 * _size.x;
        const DWORD cbBuffer = cbWidth * _size.y;

        BYTE* pData;

        // Create a new memory buffer.
        IMFMediaBuffer* ppBuffer;
        HRESULT hr = MFCreateMemoryBuffer ( cbBuffer, &ppBuffer );
        pBuffer.reset ( ppBuffer );

        // Lock the buffer and copy the video frame to the buffer.
        if ( SUCCEEDED ( hr ) )
        {
            hr = pBuffer->Lock ( &pData, nullptr, nullptr );
        }
        if ( SUCCEEDED ( hr ) )
        {
            hr = MFCopyImage (
                pData,          // Destination buffer.
                cbWidth,        // Destination stride.
                videoBuffer,    // First row in source image.
                cbWidth,        // Source stride.
                cbWidth,        // Image width in bytes.
                _size.y          // Image height in pixels.
            );
        }
        if ( pBuffer )
        {
            pBuffer->Unlock ( );
        }

        // Set the data length of the buffer.
        if ( SUCCEEDED ( hr ) )
        {
            hr = pBuffer->SetCurrentLength ( cbBuffer );
        }
        // Create a media sample and add the buffer to the sample.
        if ( SUCCEEDED ( hr ) )
        {
            IMFSample* ppSample;
            hr = MFCreateSample ( &ppSample );
            pSample.reset ( ppSample );
        }
        if ( SUCCEEDED ( hr ) )
        {
            hr = pSample->AddBuffer ( pBuffer.get ( ) );
        }
        // Set the time stamp and the duration.
        if ( SUCCEEDED ( hr ) )
        {
            hr = pSample->SetSampleTime ( _videoRtStart );
        }
        if ( SUCCEEDED ( hr ) )
        {
            hr = pSample->SetSampleDuration ( _videoFrameDuration );
        }

        // Send the sample to the Sink Writer.
        if ( SUCCEEDED ( hr ) )
        {
            hr = _pSinkWriter->WriteSample ( _videoStream, pSample.get ( ) );
        }

        pSample.reset ( );
        pBuffer.reset ( );

        return hr;
    }

    void MediaWriter::InitializeShaders()
    {
        try 
        {
            const std::string vertexShader = R"(
                #version 150
                
                uniform mat4 ciModelViewProjection;
                
                in vec4 ciPosition;
                in vec2 ciTexCoord0;
                
                out vec2 TexCoord;
                
                void main() {
                    gl_Position = ciModelViewProjection * ciPosition;
                    TexCoord = ciTexCoord0;
                }
            )";
            
            const std::string fragmentShader = R"(
                #version 150
                
                uniform sampler2D uTexture;
                
                in vec2 TexCoord;
                out vec4 FragColor;
                
                void main() {
                    vec4 texColor = texture(uTexture, TexCoord);
                    FragColor = vec4(texColor.b, texColor.g, texColor.r, texColor.a);
                }
            )";
            
            _rgbSwapShader = ci::gl::GlslProg::create( vertexShader, fragmentShader );
        }
        catch( const std::exception& e ) 
        {
            ci::app::console() << "Failed to create RGB swap shader: " << e.what() << std::endl;
            _rgbSwapShader = nullptr;
        }
    }

    bool MediaWriter::WriteAudio ( const float* audioData, size_t sampleCount )
    {
        if ( !_isReady )
        {
            ci::app::console() << "WriteAudio failed: MediaWriter not ready" << std::endl;
            return false;
        }
        if ( !_enableAudio )
        {
            ci::app::console() << "WriteAudio failed: Audio not enabled" << std::endl;
            return false;
        }
        if ( _audioStream == -1 )
        {
            ci::app::console() << "WriteAudio failed: Audio stream not initialized" << std::endl;
            return false;
        }

        ci::app::console() << "WriteAudio: " << sampleCount << " samples, expected " << _channels
                          << " channels, " << _sampleRate << " Hz" << std::endl;

        // Calculate the actual duration for this specific audio buffer
        // sampleCount here includes all channels (total float count)
        long actualDuration = ( long ) ( 10 * 1000 * 1000.0 * sampleCount / _channels / _sampleRate );

        HRESULT hr = WriteAudioFrame( audioData, sampleCount, actualDuration );
        if ( SUCCEEDED ( hr ) )
        {
            _audioRtStart += actualDuration;
            return true;
        }
        else
        {
            ci::app::console() << "WriteAudioFrame failed: 0x" << std::hex << hr << std::endl;
            return false;
        }
    }

    bool MediaWriter::WriteAudio ( const float* audioData, size_t sampleCount, int channels, int sampleRate )
    {
        if ( !_isReady )
        {
            ci::app::console() << "WriteAudio failed: MediaWriter not ready" << std::endl;
            return false;
        }
        if ( !_enableAudio )
        {
            ci::app::console() << "WriteAudio failed: Audio not enabled" << std::endl;
            return false;
        }
        if ( _audioStream == -1 )
        {
            ci::app::console() << "WriteAudio failed: Audio stream not initialized" << std::endl;
            return false;
        }

        // Check if the format matches our expectations
        if ( channels != _channels )
        {
            ci::app::console() << "Warning: Channel mismatch. Expected " << _channels << ", got " << channels << std::endl;
        }
        if ( sampleRate != _sampleRate )
        {
            ci::app::console() << "Warning: Sample rate mismatch. Expected " << _sampleRate << ", got " << sampleRate << std::endl;
        }

        // ci::app::console() << "WriteAudio: " << sampleCount << " samples, " << channels
        //                   << " channels, " << sampleRate << " Hz" << std::endl;

        // Calculate the actual duration for this specific audio buffer
        // sampleCount is the number of frames, duration = frames / sampleRate
        long actualDuration = ( long ) ( 10 * 1000 * 1000.0 * sampleCount / sampleRate );

        HRESULT hr = WriteAudioFrame( audioData, sampleCount * channels, actualDuration );
        if ( SUCCEEDED ( hr ) )
        {
            _audioRtStart += actualDuration;
            return true;
        }
        else
        {
            ci::app::console() << "WriteAudioFrame failed: 0x" << std::hex << hr << std::endl;
            return false;
        }
    }

    HRESULT MediaWriter::InitializeAudioStreamInline(IMFSinkWriter* pSinkWriter, DWORD* pAudioStreamIndex)
    {
        ci::app::console ( ) << "InitializeAudioStream: Starting audio stream initialization..." << std::endl;
        std::unique_ptr<IMFMediaType, std::function<void ( IMFMediaType* )>> pAudioTypeOut ( nullptr, [this] ( IMFMediaType* mt ) { mfSafeRelease ( &mt ); } );
        std::unique_ptr<IMFMediaType, std::function<void ( IMFMediaType* )>> pAudioTypeIn ( nullptr, [this] ( IMFMediaType* mt ) { mfSafeRelease ( &mt ); } );
        DWORD audioStreamIndex;

        HRESULT hr = S_OK;
        ci::app::console ( ) << "InitializeAudioStream: Sample rate: " << _sampleRate << ", Channels: " << _channels << std::endl;

        // Create output audio type (AAC)
        if ( SUCCEEDED ( hr ) )
        {
            IMFMediaType* p;
            hr = MFCreateMediaType ( &p );
            if ( !SUCCEEDED ( hr ) )
            {
                ci::app::console ( ) << "MFCreateMediaType (audio out) failed: 0x" << std::hex << hr << std::endl;
                return hr;
            }
            pAudioTypeOut.reset ( p );
        }
        if ( SUCCEEDED ( hr ) )
        {
            hr = pAudioTypeOut->SetGUID ( MF_MT_MAJOR_TYPE, MFMediaType_Audio );
        }
        if ( SUCCEEDED ( hr ) )
        {
            hr = pAudioTypeOut->SetGUID ( MF_MT_SUBTYPE, MFAudioFormat_AAC );
            if ( !SUCCEEDED ( hr ) )
            {
                ci::app::console ( ) << "SetGUID (AAC subtype) failed: 0x" << std::hex << hr << std::endl;
                return hr;
            }
        }
        if ( SUCCEEDED ( hr ) )
        {
            hr = pAudioTypeOut->SetUINT32 ( MF_MT_AUDIO_SAMPLES_PER_SECOND, _sampleRate );
        }
        if ( SUCCEEDED ( hr ) )
        {
            hr = pAudioTypeOut->SetUINT32 ( MF_MT_AUDIO_NUM_CHANNELS, _channels );
            if ( !SUCCEEDED ( hr ) )
            {
                ci::app::console ( ) << "SetUINT32 (output channels) failed: 0x" << std::hex << hr << std::endl;
                return hr;
            }
        }
        if ( SUCCEEDED ( hr ) )
        {
            // Set AAC bitrate - use standard rates (128 kbps for stereo, 64 kbps for mono)
            UINT32 aacBitrate = _channels == 2 ? 128000 : 64000;
            hr = pAudioTypeOut->SetUINT32 ( MF_MT_AUDIO_AVG_BYTES_PER_SECOND, aacBitrate / 8 );
            if ( !SUCCEEDED ( hr ) )
            {
                ci::app::console ( ) << "SetUINT32 (output bitrate) failed: 0x" << std::hex << hr << std::endl;
                return hr;
            }
            ci::app::console() << "AAC output bitrate: " << aacBitrate << " bps (" << (aacBitrate / 8) << " bytes/sec)" << std::endl;
        }
        if ( SUCCEEDED ( hr ) )
        {
            ci::app::console ( ) << "Attempting to add AAC audio stream to sink writer..." << std::endl;
            ci::app::console ( ) << "  Sample rate: " << _sampleRate << " Hz" << std::endl;
            ci::app::console ( ) << "  Channels: " << _channels << std::endl;
            hr = pSinkWriter->AddStream ( pAudioTypeOut.get ( ), pAudioStreamIndex );
            if ( !SUCCEEDED ( hr ) )
            {
                ci::app::console ( ) << "AddStream (audio) failed: 0x" << std::hex << hr << std::dec << std::endl;

                // Provide more specific error messages
                if ( hr == MF_E_TOPO_CODEC_NOT_FOUND )
                {
                    ci::app::console ( ) << "  Error: AAC codec not found. Audio will be disabled." << std::endl;
                }
                else if ( hr == E_INVALIDARG )
                {
                    ci::app::console ( ) << "  Error: Invalid audio parameters." << std::endl;
                }

                return hr;
            }
            else
            {
                ci::app::console ( ) << "Audio stream added successfully, index: " << *pAudioStreamIndex << std::endl;
            }
        }

        // Create input audio type (PCM)
        if ( SUCCEEDED ( hr ) )
        {
            IMFMediaType* p;
            hr = MFCreateMediaType ( &p );
            pAudioTypeIn.reset ( p );
        }
        if ( SUCCEEDED ( hr ) )
        {
            hr = pAudioTypeIn->SetGUID ( MF_MT_MAJOR_TYPE, MFMediaType_Audio );
        }
        if ( SUCCEEDED ( hr ) )
        {
            hr = pAudioTypeIn->SetGUID ( MF_MT_SUBTYPE, MFAudioFormat_Float );
        }
        if ( SUCCEEDED ( hr ) )
        {
            hr = pAudioTypeIn->SetUINT32 ( MF_MT_AUDIO_BITS_PER_SAMPLE, 32 ); // 32-bit float
        }
        if ( SUCCEEDED ( hr ) )
        {
            hr = pAudioTypeIn->SetUINT32 ( MF_MT_AUDIO_SAMPLES_PER_SECOND, _sampleRate );
        }
        if ( SUCCEEDED ( hr ) )
        {
            hr = pAudioTypeIn->SetUINT32 ( MF_MT_AUDIO_NUM_CHANNELS, _channels );
            if ( !SUCCEEDED ( hr ) )
            {
                ci::app::console ( ) << "SetUINT32 (input channels) failed: 0x" << std::hex << hr << std::endl;
                return hr;
            }
        }
        if ( SUCCEEDED ( hr ) )
        {
            hr = pAudioTypeIn->SetUINT32 ( MF_MT_AUDIO_BLOCK_ALIGNMENT, _channels * sizeof(float) );
            if ( !SUCCEEDED ( hr ) )
            {
                ci::app::console ( ) << "SetUINT32 (input block alignment) failed: 0x" << std::hex << hr << std::endl;
                return hr;
            }
        }
        if ( SUCCEEDED ( hr ) )
        {
            hr = pAudioTypeIn->SetUINT32 ( MF_MT_AUDIO_AVG_BYTES_PER_SECOND, _sampleRate * _channels * sizeof(float) );
            if ( !SUCCEEDED ( hr ) )
            {
                ci::app::console ( ) << "SetUINT32 (input avg bytes/sec) failed: 0x" << std::hex << hr << std::endl;
                return hr;
            }
        }
        if ( SUCCEEDED ( hr ) )
        {
            ci::app::console ( ) << "Setting input media type for audio stream " << *pAudioStreamIndex << std::endl;
            hr = pSinkWriter->SetInputMediaType ( *pAudioStreamIndex, pAudioTypeIn.get ( ), nullptr );
            if ( !SUCCEEDED ( hr ) )
            {
                ci::app::console ( ) << "SetInputMediaType (audio) failed: 0x" << std::hex << hr << std::dec << std::endl;
                return hr;
            }
            ci::app::console ( ) << "Input media type set successfully" << std::endl;
        }

        if ( SUCCEEDED ( hr ) )
        {
            ci::app::console() << "Audio stream initialized successfully. Stream index: " << *pAudioStreamIndex << std::endl;
        }
        else
        {
            ci::app::console() << "Audio stream initialization failed: 0x" << std::hex << hr << std::endl;
        }

        return hr;
    }

    HRESULT MediaWriter::WriteAudioFrame ( const float* audioData, size_t sampleCount, long duration )
    {
        if ( !_isReady || _audioStream == -1 || !audioData || sampleCount == 0 )
        {
            ci::app::console() << "WriteAudioFrame precondition failed - ready:" << _isReady
                              << " stream:" << _audioStream << " data:" << (audioData != nullptr)
                              << " samples:" << sampleCount << std::endl;
            return E_FAIL;
        }

        // Validate audio data (check for NaN or extreme values)
        static int validationCounter = 0;
        if ( validationCounter % 100 == 0 )
        {
            float maxVal = 0.0f;
            for ( size_t i = 0; i < std::min(sampleCount, size_t(100)); i++ )
            {
                if ( std::isnan( audioData[i] ) || std::isinf( audioData[i] ) )
                {
                    ci::app::console() << "WARNING: Invalid audio data detected (NaN/Inf)" << std::endl;
                    break;
                }
                maxVal = std::max( maxVal, std::abs( audioData[i] ) );
            }
            if ( maxVal > 0.0f )
            {
                ci::app::console() << "Audio data sample max: " << maxVal << std::endl;
            }
        }
        validationCounter++;

        std::unique_ptr<IMFSample, std::function<void ( IMFSample* )>> pSample ( nullptr, [this] ( IMFSample* s ) { mfSafeRelease ( &s ); } );
        std::unique_ptr<IMFMediaBuffer, std::function<void ( IMFMediaBuffer* )>> pBuffer ( nullptr, [this] ( IMFMediaBuffer* mb ) { mfSafeRelease ( &mb ); } );

        const DWORD bytesPerSample = sizeof(float); // Output is float
        const DWORD bufferSize = sampleCount * bytesPerSample; // sampleCount should already include channels

        BYTE* pData;

        // Create a new memory buffer for audio
        IMFMediaBuffer* ppBuffer;
        HRESULT hr = MFCreateMemoryBuffer ( bufferSize, &ppBuffer );
        if ( !SUCCEEDED ( hr ) )
        {
            return hr;
        }
        pBuffer.reset ( ppBuffer );

        // Lock the buffer and copy the audio data
        if ( SUCCEEDED ( hr ) )
        {
            hr = pBuffer->Lock ( &pData, nullptr, nullptr );
            if ( !SUCCEEDED ( hr ) )
            {
                ci::app::console() << "Buffer Lock failed: 0x" << std::hex << hr << std::endl;
                return hr;
            }
        }
        if ( SUCCEEDED ( hr ) )
        {
            // Direct copy since both input and output are float
            memcpy( pData, audioData, bufferSize );
        }
        if ( pBuffer )
        {
            pBuffer->Unlock ( );
        }

        // Set the data length of the buffer
        if ( SUCCEEDED ( hr ) )
        {
            hr = pBuffer->SetCurrentLength ( bufferSize );
            if ( !SUCCEEDED ( hr ) )
            {
                ci::app::console() << "SetCurrentLength failed: 0x" << std::hex << hr << std::endl;
                return hr;
            }
        }
        
        // Create a media sample and add the buffer to the sample
        if ( SUCCEEDED ( hr ) )
        {
            IMFSample* ppSample;
            hr = MFCreateSample ( &ppSample );
            if ( !SUCCEEDED ( hr ) )
            {
                ci::app::console() << "MFCreateSample failed: 0x" << std::hex << hr << std::endl;
                return hr;
            }
            pSample.reset ( ppSample );
        }
        if ( SUCCEEDED ( hr ) )
        {
            hr = pSample->AddBuffer ( pBuffer.get ( ) );
            if ( !SUCCEEDED ( hr ) )
            {
                ci::app::console() << "AddBuffer failed: 0x" << std::hex << hr << std::endl;
                return hr;
            }
        }
        
        // Set the time stamp and the duration
        if ( SUCCEEDED ( hr ) )
        {
            hr = pSample->SetSampleTime ( _audioRtStart );
            if ( !SUCCEEDED ( hr ) )
            {
                ci::app::console() << "SetSampleTime failed: 0x" << std::hex << hr << std::endl;
                return hr;
            }
        }
        if ( SUCCEEDED ( hr ) )
        {
            hr = pSample->SetSampleDuration ( duration );
            if ( !SUCCEEDED ( hr ) )
            {
                ci::app::console() << "SetSampleDuration failed: 0x" << std::hex << hr << std::endl;
                return hr;
            }
        }

        // Send the sample to the Sink Writer
        if ( SUCCEEDED ( hr ) )
        {
            hr = _pSinkWriter->WriteSample ( _audioStream, pSample.get ( ) );
            if ( !SUCCEEDED ( hr ) )
            {
                ci::app::console() << "WriteSample (audio) failed: 0x" << std::hex << hr << " stream:" << _audioStream << std::endl;
                return hr;
            }
        }

        return hr;
    }
}