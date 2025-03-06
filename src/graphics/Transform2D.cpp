#include "Transform2D.h"


void Transform2D::setTranslation( const ci::vec2 &translation )
{
	mTranslation = translation;
	mIsInvClean = false;
	mIsClean = false;
}

void Transform2D::setScale( float scale )
{
	mScale = scale;
	mIsInvClean = false;
	mIsClean = false;
}

void Transform2D::setRotation( float rotation )
{
	mRotation = rotation;
	mIsInvClean = false;
	mIsClean = false;
}

void Transform2D::pan( const ci::vec2 &panOffset )
{
	mTranslation += panOffset;
	mIsInvClean = false;
	mIsClean = false;
}

void Transform2D::reset()
{
	mScale = 1.0f;
	mAnchor = ci::vec2( 0.0f );
	mTranslation = ci::vec2( 0.0f );

	mIsInvClean = false;
	mIsClean = false;
}

void Transform2D::set( const ci::vec2 &translation, float scale, float rotation )
{
	mTranslation = translation;
	mScale = scale;
	mRotation = rotation;
	mAnchor = ci::vec2( 0.0f );

	mIsInvClean = false;
	mIsClean = false;
}

void Transform2D::setMatrix( const ci::mat4 &modelMtx )
{
	/*
	mModelMatrix = modelMtx;
	// need to extract parts.
	// See: https://math.stackexchange.com/questions/13150/extracting-rotation-scale-values-from-2d-transformation-matrix

	float a = modelMtx[0][0];
	float b = modelMtx[0][1];
	float c = modelMtx[1][0];
	float d = modelMtx[1][1];
	float tx = modelMtx[3][0];
	float ty = modelMtx[3][1];

	float scaleX = sqrt( ( a * a ) + ( c * c ) ); // sign of A??
	float scaleY = sqrt( ( b * b ) + ( d * d ) );// sign of A??


	float sign = atan( -c / a );
	float rad = acos( a / scaleX );
	//deg = rad * degree;

	float rad2 = atan2( c, d );

	constexpr const float NinetyDegrees = 1.5708f;
	constexpr const float ThreeSixtyDegrees = M_PI * 2.0f;


	if( ( rad > NinetyDegrees ) && ( sign > 0 ) )
	{
		mRotation = ( ThreeSixtyDegrees - rad );
	}
	else if( ( rad < NinetyDegrees ) && ( sign < 0 ) )
	{
		mRotation = ( ThreeSixtyDegrees - rad );
	}
	else
	{
		mRotation = rad;
	}
	
	mScale = scaleX;
	mTranslation = ci::vec2( tx, ty );

	mIsInvClean = false;
	mIsClean = true;
	*/
}

const ci::mat4 &Transform2D::getMatrix() const
{
	if( !mIsClean )
	{
		// Update model matrix.
		mMatrix = glm::translate( ci::vec3( mTranslation, 0.0f ) );
		mMatrix *= glm::rotate( mRotation, ci::vec3( 0.0f, 0.0f, 1.0f ) );
		mMatrix *= glm::scale( ci::vec3( mScale ) );
		mMatrix *= glm::translate( ci::vec3( -mAnchor, 0.0f ) );
		mIsInvClean = false;
		mIsClean = true;
	}

	return mMatrix;
}

const ci::mat4 &Transform2D::getInverseMatrix() const
{
	if( !mIsInvClean )
	{
		mInvMatrix = glm::inverse( getMatrix() );
		mIsInvClean = true;
	}

	return mInvMatrix;
}

ci::vec2 Transform2D::toLocal( const ci::vec2 &pt )
{
	auto const &invMtx = getInverseMatrix();
	return ci::vec2( invMtx * ci::vec4( pt, 0.0f, 1.0f ) );
}

ci::vec2 Transform2D::toWorld( const ci::vec2 &pt )
{
	auto const &mtx = getMatrix();
	return ci::vec2( mtx * ci::vec4( pt, 0.0f, 1.0f ) );
}

void Transform2D::reposition( const ci::vec2 &pos )
{
	// Convert mouse to object space.
	ci::vec2 anchor = ci::vec2( getInverseMatrix() * ci::vec4( pos, 0.0f, 1.0f ) );

	// Calculate new position, anchor and scale.
	mTranslation += ci::vec2( mMatrix * ci::vec4( anchor - mAnchor, 0.0f, 0.0f ) );
	mAnchor = anchor;
}
