//
//  SimpleWriteApp.cxx
//  SimpleWriteApp
//
//  Created by Andrew Wright on 17/08/21.
//  (c) 2021 AX Interactive
//

#include "cinder/app/RendererGl.h"
#include "cinder/app/App.h"
#include "cinder/gl/gl.h"
#include "cinder/audio/audio.h"
#include "AX-MediaPlayer.h"
#include "guiddef.h"

#include "AX-MediaWriter.h"

#pragma comment(lib, "Mfreadwrite.lib")

#if __has_include( "cinder/CinderImGui.h")
#include "cinder/CinderImGui.h"
#define HAS_DEBUG_UI
namespace ui = ImGui;
#endif

#undef HAS_DEBUG_UI

#define FORCE_NVIDIA_CARD_IF_PRESENT

#ifdef FORCE_NVIDIA_CARD_IF_PRESENT
extern "C"
{
    __declspec( dllexport ) int NvOptimusEnablement = 0x00000001; // This forces Intel GPU / Nvidia card selection
}
#endif

using namespace ci;
using namespace ci::app;

class SimpleWriteApp : public app::App
{
public:
    void setup ( ) override;
    void update ( ) override;
    void draw ( ) override;

protected:
    void connectSignals ( );
    void loadDefaultVideo ( );

    AX::Video::MediaWriterRef     _writer;

    AX::Video::MediaPlayerRef     _player;
    AX::Video::MediaPlayer::Error _error{ AX::Video::MediaPlayer::Error::NoError };

    bool _hardwareAccelerated{ true };
    gl::TextureRef _texture;

};

void SimpleWriteApp::setup ( )
{
    console ( ) << gl::getString ( GL_RENDERER ) << std::endl;
    console ( ) << gl::getString ( GL_VERSION ) << std::endl;

    loadDefaultVideo ( );
}

void SimpleWriteApp::loadDefaultVideo ( )
{
    auto fmt = AX::Video::MediaPlayer::Format ( ).HardwareAccelerated ( _hardwareAccelerated );
    _player = AX::Video::MediaPlayer::Create ( CINDER_PATH "/samples/QuickTimeBasic/assets/bbb.mp4", fmt );
    connectSignals ( );
}


void SimpleWriteApp::connectSignals ( )
{
    _player->OnSeekStart.connect ( [=] { std::cout << "OnSeekStart\n"; } );
    _player->OnSeekEnd.connect ( [=] { std::cout << "OnSeekEnd\n"; } );
    _player->OnComplete.connect ( [=]
        {
            std::cout << "OnComplete\n";

            _writer->Finalize ( );
            std::this_thread::sleep_for ( std::chrono::seconds ( 1 ) );
            quit ( );
        } );
    _player->OnReady.connect ( [=]
        {
            std::cout << "OnReady: " << _player->GetDurationInSeconds ( ) << std::endl;

            _writer = AX::Video::MediaWriter::Create ( "bbb_copy.mp4", _player->GetSize ( ), _player->GetBitrate ( ), _player->GetFps( ) );
            if ( _writer )
            {
                setWindowSize ( _player->GetSize ( ) );
                _player->SetMuted ( true );
                _player->Play ( );
            }
        } );
    _player->OnError.connect ( [=] ( AX::Video::MediaPlayer::Error error ) { _error = error; } );
    _player->OnBufferingStart.connect ( [=] { std::cout << "OnBufferingStart\n"; } );
    _player->OnBufferingEnd.connect ( [=] { std::cout << "OnBufferingEnd\n"; } );
}

void SimpleWriteApp::update ( )
{
    if ( _player->IsHardwareAccelerated ( ) )
    {
        if ( auto lease = _player->GetTexture ( ) )
        {
            // You can now use this texture until `lease` goes out
            // of scope (it will Unlock() the texture when destructing )

            auto textureRef = lease->ToTexture ( );

            if ( _writer )
            {
                _writer->Write ( textureRef );
            }
        }
    }
}



void SimpleWriteApp::draw ( )
{
    gl::clear ( Colorf::black ( ) );

    auto s = std::to_string ( ( _player->GetPositionInSeconds ( ) / _player->GetDurationInSeconds ( ) ) * 100 ) + "%";
    ci::gl::drawStringCentered ( s, ci::app::getWindowCenter ( ), ci::ColorA ( 1, 1, 1, 1 ), ci::Font ( "Arial", 68 ) );

    if ( _player->CheckNewFrame ( ) )
    {
        if ( auto surface = _player->GetSurface ( ) )
        {
            _texture = *_player->GetTexture ( );
        }
    }
}

void Init ( App::Settings* settings )
{
#ifdef CINDER_MSW
    settings->setConsoleWindowEnabled ( );
#endif
}

CINDER_APP ( SimpleWriteApp, RendererGl ( RendererGl::Options ( ) ), Init );
