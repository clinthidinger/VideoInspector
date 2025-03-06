#pragma once

#include <cinder/Matrix44.h>
#include <cinder/Vector.h>

// See CanvasUi https://gist.github.com/paulhoux/e91c1dcbc6b727d4b6c13f560186006f

class Transform2D
{
public:

	void reposition( const ci::vec2 &pos );

	void setMatrix( const ci::mat4 &modelMtx );

	const ci::mat4 &getMatrix() const;
	
	//! Returns the inverse of the current transform matrix. Can be used to convert coordinates. See also `CanvasUi::toLocal`.
	const ci::mat4 &getInverseMatrix() const;
	
	//! Converts a given point \a pt from world to object space, effectively undoing the canvas transformations.
	ci::vec2 toLocal( const ci::vec2 &pt );

	ci::vec2 toWorld( const ci::vec2 &pt );

	void set( const ci::vec2 &translation, float scale, float rotation );

	const ci::vec2 &getTranslation() const;
	void setTranslation( const ci::vec2 &translation );

	float getScale() const;
	void setScale( float scale );

	float getRotation() const;
	void setRotation( float rotation );

	void pan( const ci::vec2 &panOffset );

	void reset();

protected:
	float mScale{ 1.0f };
	float mRotation{ 0.0f };
	ci::vec2 mAnchor{ 0.0f };
	ci::vec2 mTranslation{ 0.0f };
	mutable ci::mat4 mMatrix{ 1.0f };
	mutable ci::mat4 mInvMatrix{ 1.0f };
	mutable bool mIsClean{ false};
	mutable bool mIsInvClean{ false };
};

inline float Transform2D::getScale() const { return mScale; }
inline const ci::vec2 &Transform2D::getTranslation() const { return mTranslation; }
inline float Transform2D::getRotation() const { return mRotation; }
