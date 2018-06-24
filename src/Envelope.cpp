/**********************************************************************

  Audacity: A Digital Audio Editor

  Envelope.cpp

  Dominic Mazzoni (original author)
  Dr William Bland (integration - the Calculus kind)
  Monty (xiphmont) (important bug fixes)

*******************************************************************//**

\class Envelope
\brief Draggable curve used in TrackPanel for varying amplification.

  This class manages an envelope - i.e. a piecewise linear funtion
  that the user can edit by dragging control points around.  The
  envelope is most commonly used to control the amplitude of a
  waveform, but it is also used to shape the Equalization curve.

*//****************************************************************//**

\class EnvPoint
\brief EnvPoint, derived from XMLTagHandler, provides Envelope with
a draggable point type.

*//*******************************************************************/

#include "Envelope.h"
#include "Experimental.h"
#include "ViewInfo.h"

#include <math.h>

#include <wx/dc.h>
#include <wx/brush.h>
#include <wx/event.h>
#include <wx/pen.h>
#include <wx/textfile.h>
#include <wx/log.h>

#include "AColor.h"
#include "TrackArtist.h"

static const double VALUE_TOLERANCE = 0.001;

Envelope::Envelope(bool exponential, double minValue, double maxValue, double defaultValue)
   : mMinValue(minValue)
   , mMaxValue(maxValue)
   , mDefaultValue { ClampValue(defaultValue) }
{
}

Envelope::~Envelope()
{
}

Envelope::Envelope(const Envelope &orig, double t0, double t1)
   : mMinValue(orig.mMinValue)
   , mMaxValue(orig.mMaxValue)
   , mDefaultValue(orig.mDefaultValue)
{
   mOffset = wxMax(t0, orig.mOffset);
   mTrackLen = wxMin(t1, orig.mOffset + orig.mTrackLen) - mOffset;

   auto range1 = orig.EqualRange( t0 - orig.mOffset, 0 );
   auto range2 = orig.EqualRange( t1 - orig.mOffset, 0 );
}

Envelope::Envelope(const Envelope &orig)
   : mMinValue(orig.mMinValue)
   , mMaxValue(orig.mMaxValue)
   , mDefaultValue(orig.mDefaultValue)
{
   mOffset = orig.mOffset;
   mTrackLen = orig.mTrackLen;
}

// Private methods

std::pair<int, int> Envelope::EqualRange( double when, double sampleDur ) const
{
   // Find range of envelope points matching the given time coordinate
   // (within an interval of length sampleDur)
   // by binary search; if empty, it still indicates where to
   // insert.
   const auto tolerance = sampleDur / 2;
   auto begin = mEnv.begin();
   auto end = mEnv.end();
   auto first = std::lower_bound(
      begin, end,
      EnvPoint{ when - tolerance, 0.0 },
      []( const EnvPoint &point1, const EnvPoint &point2 )
         { return point1.GetT() < point2.GetT(); }
   );
   auto after = first;
   while ( after != end && after->GetT() <= when + tolerance )
      ++after;
   return { first - begin, after - begin };
}

// Accessors
double Envelope::GetValue( double t, double sampleDur ) const
{
   // t is absolute time
   double temp;

   GetValues( &temp, 1, t, sampleDur );
   return temp;
}

// relative time
/// @param Lo returns last index at or before this time, maybe -1
/// @param Hi returns first index after this time, maybe past the end
void Envelope::BinarySearchForTime( int &Lo, int &Hi, double t ) const
{
   // Optimizations for the usual pattern of repeated calls with
   // small increases of t.
   {
      if (mSearchGuess >= 0 && mSearchGuess < (int)mEnv.size()) {
         if (t >= mEnv[mSearchGuess].GetT() &&
             (1 + mSearchGuess == (int)mEnv.size() ||
              t < mEnv[1 + mSearchGuess].GetT())) {
            Lo = mSearchGuess;
            Hi = 1 + mSearchGuess;
            return;
         }
      }

      ++mSearchGuess;
      if (mSearchGuess >= 0 && mSearchGuess < (int)mEnv.size()) {
         if (t >= mEnv[mSearchGuess].GetT() &&
             (1 + mSearchGuess == (int)mEnv.size() ||
              t < mEnv[1 + mSearchGuess].GetT())) {
            Lo = mSearchGuess;
            Hi = 1 + mSearchGuess;
            return;
         }
      }
   }

   Lo = -1;
   Hi = mEnv.size();

   // Invariants:  Lo is not less than -1, Hi not more than size
   while (Hi > (Lo + 1)) {
      int mid = (Lo + Hi) / 2;
      // mid must be strictly between Lo and Hi, therefore a valid index
      if (t < mEnv[mid].GetT())
         Hi = mid;
      else
         Lo = mid;
   }
   wxASSERT( Hi == ( Lo+1 ));

   mSearchGuess = Lo;
}

// relative time
/// @param Lo returns last index before this time, maybe -1
/// @param Hi returns first index at or after this time, maybe past the end
void Envelope::BinarySearchForTime_LeftLimit( int &Lo, int &Hi, double t ) const
{
   Lo = -1;
   Hi = mEnv.size();

   // Invariants:  Lo is not less than -1, Hi not more than size
   while (Hi > (Lo + 1)) {
      int mid = (Lo + Hi) / 2;
      // mid must be strictly between Lo and Hi, therefore a valid index
      if (t <= mEnv[mid].GetT())
         Hi = mid;
      else
         Lo = mid;
   }
   wxASSERT( Hi == ( Lo+1 ));

   mSearchGuess = Lo;
}

/// GetInterpolationStartValueAtPoint() is used to select the
/// envelope value (assuming we are doing linear interpolation).
/// @param iPoint index in env array to look at.
double Envelope::GetInterpolationStartValueAtPoint( int iPoint ) const
{
	return mEnv[iPoint].GetVal();
}

void Envelope::GetValues( double *buffer, int bufferLen,
                          double t0, double tstep ) const
{
   // Convert t0 from absolute to clip-relative time
   t0 -= mOffset;
   GetValuesRelative( buffer, bufferLen, t0, tstep);
}

void Envelope::GetValuesRelative
   (double *buffer, int bufferLen, double t0, double tstep, bool leftLimit)
   const
{
   // JC: If bufferLen ==0 we have probably just allocated a zero sized buffer.
   // wxASSERT( bufferLen > 0 );

   const auto epsilon = tstep / 2;
   int len = mEnv.size();

   double t = t0;
   double increment = 0;
   if ( len > 1 && t <= mEnv[0].GetT() && mEnv[0].GetT() == mEnv[1].GetT() )
      increment = leftLimit ? -epsilon : epsilon;

   double tprev, vprev, tnext = 0, vnext, vstep = 0;

   for (int b = 0; b < bufferLen; b++) {

      // Get easiest cases out the way first...
      // IF empty envelope THEN default value
      if (len <= 0) {
         buffer[b] = mDefaultValue;
         t += tstep;
         continue;
      }

      auto tplus = t + increment;

      // IF before envelope THEN first value
      if ( leftLimit ? tplus <= mEnv[0].GetT() : tplus < mEnv[0].GetT() ) {
         buffer[b] = mEnv[0].GetVal();
         t += tstep;
         continue;
      }
      // IF after envelope THEN last value
      if ( leftLimit
            ? tplus > mEnv[len - 1].GetT() : tplus >= mEnv[len - 1].GetT() ) {
         buffer[b] = mEnv[len - 1].GetVal();
         t += tstep;
         continue;
      }

      // be careful to get the correct limit even in case epsilon == 0
      if ( b == 0 ||
           ( leftLimit ? tplus > tnext : tplus >= tnext ) ) {

         // We're beyond our tnext, so find the next one.
         // Don't just increment lo or hi because we might
         // be zoomed far out and that could be a large number of
         // points to move over.  That's why we binary search.

         int lo,hi;
         if ( leftLimit )
            BinarySearchForTime_LeftLimit( lo, hi, tplus );
         else
            BinarySearchForTime( lo, hi, tplus );

         // mEnv[0] is before tplus because of eliminations above, therefore lo >= 0
         // mEnv[len - 1] is after tplus, therefore hi <= len - 1
         wxASSERT( lo >= 0 && hi <= len - 1 );

         tprev = mEnv[lo].GetT();
         tnext = mEnv[hi].GetT();

         if ( hi + 1 < len && tnext == mEnv[ hi + 1 ].GetT() )
            // There is a discontinuity after this point-to-point interval.
            // Usually will stop evaluating in this interval when time is slightly
            // before tNext, then use the right limit.
            // This is the right intent
            // in case small roundoff errors cause a sample time to be a little
            // before the envelope point time.
            // Less commonly we want a left limit, so we continue evaluating in
            // this interval until shortly after the discontinuity.
            increment = leftLimit ? -epsilon : epsilon;
         else
            increment = 0;

         vprev = GetInterpolationStartValueAtPoint( lo );
         vnext = GetInterpolationStartValueAtPoint( hi );

         // Interpolate, either linear or log depending on mDB.
         double dt = (tnext - tprev);
         double to = t - tprev;
         double v;
         if (dt > 0.0)
         {
            v = (vprev * (dt - to) + vnext * to) / dt;
            vstep = (vnext - vprev) * tstep / dt;
         }
         else
         {
            v = vnext;
            vstep = 0.0;
         }

         buffer[b] = v;
      } else {
         buffer[b] = buffer[b - 1] + vstep;
      }

      t += tstep;
   }
}

void Envelope::GetValues
   ( double sampleDur,
     double *buffer, int bufferLen, int leftOffset,
     const ZoomInfo &zoomInfo )
   const
{
   // Getting many envelope values, corresponding to pixel columns, which may
   // not be uniformly spaced in time when there is a fisheye.

   for ( int xx = 0; xx < bufferLen; ++xx ) 
   {
      auto time = zoomInfo.PositionToTime( xx, -leftOffset );
	  if (sampleDur <= 0)
	  {
		  // Sample interval not defined (as for time track)
		  buffer[xx] = GetValue(time);
	  }
   }
}

void Envelope::print() const
{
   for( unsigned int i = 0; i < mEnv.size(); i++ )
      wxPrintf( "(%.2f, %.2f)\n", mEnv[i].GetT(), mEnv[i].GetVal() );
}
