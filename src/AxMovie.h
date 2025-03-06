#pragma once
#include <memory>
#include "AX-MediaPlayer.h"
#include "cinder/Signals.h"

// TODO: https://docs.microsoft.com/en-us/windows/win32/medfound/tutorial--using-the-sink-writer-to-encode-video

using AxMovieRef = std::shared_ptr<class AxMovie>;
class AxMovie
{
public:
	static AxMovieRef create( const std::string &filePath );
	virtual ~AxMovie();
	AX::Video::MediaPlayer::FrameLeaseRef getTexture();
	void stop();
	virtual void play();
	void pause();
	void seekToStart();
	void seekToTime( float t );
	void seekToFrame( int frame );
	void stepForward();
	float getFramerate() const;
	float getCurrentTime() const;
	int64_t getCurrentFrame() const;
	int64_t getFrameCount() const;
	float getDuration() const;
	void setRate( float rate );
	float getRate() const;
	bool isPlaying() const;
	bool isReady() const;
	bool isSeeking() const;
	ci::ivec2 getSize() const;

	ci::signals::Signal<void()> &getSeekFinishedSignal();
	ci::signals::Signal<void()> &getIsReadySignal();

protected:
	AxMovie( const std::string &filePath );

	std::string mFilePath;
	AX::Video::MediaPlayerRef mPlayer;
	ci::gl::Texture2dRef mTexture;
	float mRate{ 1.0f };
	ci::signals::Signal<void()> mSignalSeekFinished;
	ci::signals::Signal<void()> mSignalIsReady;
	ci::signals::Connection mConnSeekFinished;
	ci::signals::Connection mConnIsReady;
};

inline AxMovie::AxMovie( const std::string &filePath )
	: mFilePath( filePath )
{
	auto fmt = AX::Video::MediaPlayer::Format()
		.HardwareAccelerated( true )
		.Audio( false );

	mPlayer = AX::Video::MediaPlayer::Create( filePath, fmt );
	//mPlayer->SetMuted( true );
	mConnIsReady = mPlayer->OnReady.connect( [this] () {
		//int s = mSignalIsReady.getNumSlots();
		//mPlayer->SetMuted( true );
		mSignalIsReady.emit();
	} );
	mConnSeekFinished = mPlayer->OnSeekEnd.connect( [this] () {
		mSignalSeekFinished.emit();
	} );
}

inline AxMovieRef AxMovie::create( const std::string &filePath )
{
	return AxMovieRef( new AxMovie( filePath ) );
}

inline AxMovie::~AxMovie()
{
	mConnIsReady.disconnect();
	mConnSeekFinished.disconnect();
}

inline AX::Video::MediaPlayer::FrameLeaseRef AxMovie::getTexture()
{
	return mPlayer ? mPlayer->GetTexture() : nullptr;
	//if( mPlayer->CheckNewFrame() )
	//{
		// Only true if using the CPU render path
	//	mTexture = *mPlayer->GetTexture();
	//}
}

inline void AxMovie::stop()
{
	mPlayer->Pause();
	//mPlayer->SeekToSeconds( 0.0f, false );
}

inline void AxMovie::play()
{
	mPlayer->Play();
	// Note: For some reason rate gets reset after a play.  Use the cached value.
	if( mRate != 1.0f )
	{
		mPlayer->SetPlaybackRate( mRate );
	}
}

inline void AxMovie::pause()
{
	mPlayer->Pause();
}

inline void AxMovie::seekToStart()
{
	mPlayer->SeekToSeconds( 0.0f, false );
}

inline void AxMovie::seekToTime( float t )
{
	mPlayer->SeekToSeconds( t, false );
}

inline void AxMovie::seekToFrame( int frame )
{
	mPlayer->SeekToSeconds( frame / getFramerate(), false);
}

inline void AxMovie::stepForward()
{
	auto const timeSecs = mPlayer->GetPositionInSeconds();
	const float oneFrameInSeconds = 1.0f / getFramerate();
	mPlayer->SeekToSeconds( timeSecs + oneFrameInSeconds, false );
}

inline float AxMovie::getFramerate() const
{
	return mPlayer->GetFramerate();
}

inline int64_t AxMovie::getCurrentFrame() const
{
	return std::round( getCurrentTime() * getFramerate() );
}

inline int64_t AxMovie::getFrameCount() const
{
	return std::round( getDuration() * getFramerate() );
}

inline float AxMovie::getCurrentTime() const
{
	return ( mPlayer ) ? mPlayer->GetPositionInSeconds() : 0.0f;
}

inline float AxMovie::getDuration() const
{
	return ( mPlayer ) ? mPlayer->GetDurationInSeconds() : 0.0f;
}

inline void AxMovie::setRate( float rate )
{
	mRate = rate;
	if( mPlayer )
	{
		mPlayer->SetPlaybackRate( rate );
	}
}

inline float AxMovie::getRate() const
{
	if( mPlayer->IsPlaybackRateSupported( mRate ) )
	{
		return mRate;
	}
	return ( mPlayer ) ? mPlayer->GetPlaybackRate() : 1.0f;
}

inline bool AxMovie::isPlaying() const
{
	return ( mPlayer ) ? mPlayer->IsPlaying() : false;
}

inline ci::signals::Signal<void()> &AxMovie::getSeekFinishedSignal()
{
	return mSignalSeekFinished;
}

inline ci::signals::Signal<void()> &AxMovie::getIsReadySignal()
{
	return mSignalIsReady;
}

inline bool AxMovie::isReady() const
{
	return  ( mPlayer ) ? mPlayer->IsReady() : false;
}

inline bool AxMovie::isSeeking() const
{
	return  ( mPlayer ) ? mPlayer->IsSeeking() : false;
}

inline ci::ivec2 AxMovie::getSize() const
{
	return ( mPlayer ) ? mPlayer->GetSize() : ci::ivec2( 0 );
}
