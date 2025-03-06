#pragma once

#include "Transform2D.h"

// Based on Paul Houx's CanvasUi gist: https://gist.github.com/paulhoux/e91c1dcbc6b727d4b6c13f560186006f

class ViewportTransform : public Transform2D
{
public:
	ViewportTransform &operator= ( const Transform2D &xform );
	void set( const Transform2D &xform );

	void enable( bool enable = true );

	//! Prevents the CanvasUi from modifying its transform matrix either through its Window signals or through the various mouse*() member functions.
	void disable();

	//! mousePos is in screen-space coordinates.
	void mouseDown( const ci::vec2 &mousePos );
	void mouseDrag( const ci::vec2 &mousePos );
	void mouseWheel( const ci::vec2 &mousePos, float increment );

	//! Sets the multiplier on mouse wheel zooming. Larger values zoom faster. Negative values invert the direction. Default is \c 0.1.
	void setMouseWheelMultiplier( float multiplier );
	//! Returns the multiplier on mouse wheel zooming. Default is \c 0.1.
	float getMouseWheelMultiplier() const;

private:
	bool mEnabled{ true };
    float mMouseWheelMultiplier{ 0.1f };
	ci::vec2 mMouse{ 0.0f };
	ci::vec2 mClick{ 0.0f };
	ci::vec2 mOriginal{ 0.0f };
};

inline void ViewportTransform::enable( bool enable ) { mEnabled = enable; }
inline void ViewportTransform::disable() { mEnabled = false; }

inline ViewportTransform &ViewportTransform::operator= ( const Transform2D &xform )
{
	Transform2D::operator= ( xform );
	return *this;
}

inline void ViewportTransform::set( const Transform2D &xform )
{
	*this = xform;
}

inline void ViewportTransform::mouseDown( const ci::vec2 &mousePos )
{
	if( !mEnabled )
	{
		return;
	}

	reposition( mousePos );

	mClick = mousePos;
	mOriginal = mTranslation;
}

inline void ViewportTransform::mouseDrag( const ci::vec2 &mousePos )
{
	if( !mEnabled )
	{
		return;
	}

	mMouse = mousePos;
	mTranslation = mOriginal + mMouse - mClick;
	mIsClean = false;
}

inline void ViewportTransform::mouseWheel( const ci::vec2 &mousePos, float increment )
{
	if( !mEnabled )
	{
		return;
	}
	reposition( mousePos );

	mMouse = mousePos;
	mScale *= 1.0f + mMouseWheelMultiplier * increment;
	mIsClean = false;
}

inline void ViewportTransform::setMouseWheelMultiplier( float multiplier ) { mMouseWheelMultiplier = multiplier; }
inline float ViewportTransform::getMouseWheelMultiplier() const { return mMouseWheelMultiplier; }
