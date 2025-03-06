#pragma once

#include <atomic>
#include <mutex>
#include <queue>
#include <cinder/app/App.h>
#include "AxMovie.h"

using AxMovieSyncedRef = std::shared_ptr<class AxMovieSynced>;
class AxMovieSynced : public AxMovie
{
public:
    static AxMovieSyncedRef create( const std::string &filePath );
    ~AxMovieSynced() override;
    void setSyncedSecondary( const AxMovieRef &secondary );
    void play() override final;
    
private:
    AxMovieSynced( const std::string &filePath );
    AxMovieRef mSecondary;
    std::mutex mMutex;
    std::queue<ci::signals::Connection> mConnections;
    ci::signals::Connection mUpdateConnection;
    std::atomic_bool mPlayOnUpdate{ false };
};

inline AxMovieSyncedRef AxMovieSynced::create( const std::string &filePath )
{
    return AxMovieSyncedRef( new AxMovieSynced( filePath ) );
}

inline AxMovieSynced::AxMovieSynced( const std::string &filePath )
    : AxMovie( filePath )
{
    mUpdateConnection = ci::app::App::get()->getSignalUpdate().connect( [=] {
        if( mPlayOnUpdate )
        {
            play();
            mPlayOnUpdate = false;
        }
    } );
}

inline AxMovieSynced::~AxMovieSynced()
{
    mUpdateConnection.disconnect();
}

inline void AxMovieSynced::setSyncedSecondary( const AxMovieRef &secondary ) { mSecondary = secondary; }
inline void AxMovieSynced::play()
{
    if( mSecondary )
    {
        auto const currFrame = getCurrentFrame();
        auto const secondaryCurrFrame = mSecondary->getCurrentFrame();
        if( currFrame != secondaryCurrFrame )
        {
            auto conn = mPlayer->OnSeekEnd.connect( [this] () {
                mPlayOnUpdate = true;
                {
                    std::unique_lock<std::mutex> lk( mMutex );
                    if( !mConnections.empty() )
                    {
                        mConnections.front().disconnect();
                        mConnections.pop();
                    }
                }
            } );
            {
                std::unique_lock<std::mutex> lk( mMutex );
                mConnections.push( conn );
            }
            return; // Will play on next update.
        }
    }

    AxMovie::play();
    // Note:  May need to keep a queue of copied frames with their presentation time.
}
