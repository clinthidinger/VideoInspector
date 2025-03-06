#include "AxMovie.h"
#include <Mfapi.h>

#ifndef DONT_FORCE_NVIDIA_CARD_IF_PRESENT
extern "C"
{
    __declspec( dllexport ) int NvOptimusEnablement = 0x00000001; // This forces Intel GPU / Nvidia card selection
}
#endif

//// How to sync mulitple videos:
//
// Maybe just extend AX-MediaPlayer to be AX-MediaPlayerSynced.  Pass in a a MediaPlayerRef to sync against or seconday video to keep synced.  Could be null.
// When play is started, it checks if the video is synced.  If not, it will seek and start playing after the seek from the callback. Will need
// primary/secondary flags.  One video may need to trigger the play on the other.  Still may need to sync on pts time. 
// 
//https://stackoverflow.com/q/46473883
//
//MediaPlayer::Impl::Update() calls _mediaEngine->OnVideoStreamTick( &time ).  time is a PTS time.  Could do this for multiple
//_mediaEngine instances and manually sink based ont the PTS times.
//
// Also maybe just make a sync'ed mediaplayer that can play multiple videos.  Should seek and then play on update after seek callback hits if the videos are not synced.
// Could have primary and secondary videos.  Secondary video could be forced to match time of primary before a play happens.
// 
//https ://github.com/microsoft/Windows-classic-samples/tree/main/Samples/DX11VideoRenderer/cpp
//IMFPresentationClock???

