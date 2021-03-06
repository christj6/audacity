/**********************************************************************

  Audacity: A Digital Audio Editor

  Envelope.h

  Dominic Mazzoni

**********************************************************************/

#ifndef __AUDACITY_ENVELOPE__
#define __AUDACITY_ENVELOPE__

#include <stdlib.h>
#include <algorithm>
#include <vector>

#include <wx/brush.h>
#include <wx/pen.h>

#include "Internat.h"

class wxRect;
class wxDC;
class wxMouseEvent;
class wxTextFile;

class Envelope;
class EnvPoint;

class ZoomInfo;

class EnvPoint final {

public:
   EnvPoint() {}
   inline EnvPoint( double t, double val ) : mT{ t }, mVal{ val } {}

   double GetT() const { return mT; }
   void SetT(double t) { mT = t; }
   double GetVal() const { return mVal; }
   inline void SetVal( Envelope *pEnvelope, double val );

private:
   double mT {};
   double mVal {};

};

typedef std::vector<EnvPoint> EnvArray;
struct TrackPanelDrawingContext;

class Envelope final {
public:
   // Envelope can define a piecewise linear function, or piecewise exponential.
   Envelope(bool exponential, double minValue, double maxValue, double defaultValue);

   Envelope(const Envelope &orig);

   // Create from a subrange of another envelope.
   Envelope(const Envelope &orig, double t0, double t1);

   void Initialize(int numPoints);

   virtual ~Envelope();

   double GetMinValue() const { return mMinValue; }
   double GetMaxValue() const { return mMaxValue; }

   double ClampValue(double value) { return std::max(mMinValue, std::min(mMaxValue, value)); }

   // Accessors
   /** \brief Get envelope value at time t */
   double GetValue( double t, double sampleDur = 0 ) const;

   /** \brief Get many envelope points at once.
    *
    * This is much faster than calling GetValue() multiple times if you need
    * more than one value in a row. */
   void GetValues(double *buffer, int len, double t0, double tstep) const;

   /** \brief Get many envelope points for pixel columns at once,
    * but don't assume uniform time per pixel.
   */
   void GetValues
      ( double sampleDur,
        double *buffer, int bufferLen, int leftOffset,
        const ZoomInfo &zoomInfo) const;

private:

   void GetValuesRelative
      (double *buffer, int len, double t0, double tstep, bool leftLimit = false)
      const;

public:
   void print() const;

private:
   /** \brief Accessor for points */
   const EnvPoint &operator[] (int index) const
   {
      return mEnv[index];
   }

   std::pair<int, int> EqualRange( double when, double sampleDur ) const;

private:
   // relative time
   void BinarySearchForTime( int &Lo, int &Hi, double t ) const;
   void BinarySearchForTime_LeftLimit( int &Lo, int &Hi, double t ) const;
   double GetInterpolationStartValueAtPoint( int iPoint ) const;

   // The list of envelope control points.
   EnvArray mEnv;

   /** \brief The time at which the envelope starts, i.e. the start offset */
   double mOffset { 0.0 };
   /** \brief The length of the envelope, which is the same as the length of the
    * underlying track (normally) */
   double mTrackLen { 0.0 };

   double mMinValue, mMaxValue;
   double mDefaultValue;

   mutable int mSearchGuess { -2 };
   friend class GetInfoCommand;
};

inline void EnvPoint::SetVal( Envelope *pEnvelope, double val )
{
   if ( pEnvelope )
      val = pEnvelope->ClampValue(val);
   mVal = val;
}

#endif
