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
#include "DirManager.h"
#include "TrackArtist.h"

static const double VALUE_TOLERANCE = 0.001;

Envelope::Envelope(bool exponential, double minValue, double maxValue, double defaultValue)
   : mDB(exponential)
   , mMinValue(minValue)
   , mMaxValue(maxValue)
   , mDefaultValue { ClampValue(defaultValue) }
{
}

Envelope::~Envelope()
{
}

Envelope::Envelope(const Envelope &orig, double t0, double t1)
   : mDB(orig.mDB)
   , mMinValue(orig.mMinValue)
   , mMaxValue(orig.mMaxValue)
   , mDefaultValue(orig.mDefaultValue)
{
   mOffset = wxMax(t0, orig.mOffset);
   mTrackLen = wxMin(t1, orig.mOffset + orig.mTrackLen) - mOffset;

   auto range1 = orig.EqualRange( t0 - orig.mOffset, 0 );
   auto range2 = orig.EqualRange( t1 - orig.mOffset, 0 );
}

Envelope::Envelope(const Envelope &orig)
   : mDB(orig.mDB)
   , mMinValue(orig.mMinValue)
   , mMaxValue(orig.mMaxValue)
   , mDefaultValue(orig.mDefaultValue)
{
   mOffset = orig.mOffset;
   mTrackLen = orig.mTrackLen;
}

void Envelope::Delete( int point )
{
}

void Envelope::Insert(int point, const EnvPoint &p)
{
}

void Envelope::InsertSpace( double t0, double tlen )
// NOFAIL-GUARANTEE
{
}

size_t Envelope::GetNumberOfPoints() const
{
   return mEnv.size();
}

void Envelope::GetPoints(double *bufferWhen,
                         double *bufferValue,
                         int bufferLen) const
{
}

void Envelope::Cap( double sampleDur )
{
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

// Control

void Envelope::SetOffset(double newOffset)
// NOFAIL-GUARANTEE
{
   mOffset = newOffset;
}

void Envelope::RescaleTimes( double newLength )
// NOFAIL-GUARANTEE
{
   if ( mTrackLen == 0 ) {
      for ( auto &point : mEnv )
         point.SetT( 0 );
   }
   else {
      auto ratio = newLength / mTrackLen;
      for ( auto &point : mEnv )
         point.SetT( point.GetT() * ratio );
   }
   mTrackLen = newLength;
}

// Accessors
double Envelope::GetValue( double t, double sampleDur ) const
{
   // t is absolute time
   double temp;

   GetValues( &temp, 1, t, sampleDur );
   return temp;
}

double Envelope::GetValueRelative(double t, bool leftLimit) const
{
   double temp;

   GetValuesRelative(&temp, 1, t, 0.0, leftLimit);
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

/// GetInterpolationStartValueAtPoint() is used to select either the
/// envelope value or its log depending on whether we are doing linear
/// or log interpolation.
/// @param iPoint index in env array to look at.
/// @return value there, or its (safe) log10.
double Envelope::GetInterpolationStartValueAtPoint( int iPoint ) const
{
   double v = mEnv[ iPoint ].GetVal();
   if( !mDB )
      return v;
   else
      return log10(v);
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

         // An adjustment if logarithmic scale.
         if( mDB )
         {
            v = pow(10.0, v);
            vstep = pow( 10.0, vstep );
         }

         buffer[b] = v;
      } else {
         if (mDB){
            buffer[b] = buffer[b - 1] * vstep;
         }else{
            buffer[b] = buffer[b - 1] + vstep;
         }
      }

      t += tstep;
   }
}

void Envelope::GetValues
   ( double alignedTime, double sampleDur,
     double *buffer, int bufferLen, int leftOffset,
     const ZoomInfo &zoomInfo )
   const
{
   // Getting many envelope values, corresponding to pixel columns, which may
   // not be uniformly spaced in time when there is a fisheye.

   double prevDiscreteTime=0.0, prevSampleVal=0.0, nextSampleVal=0.0;
   for ( int xx = 0; xx < bufferLen; ++xx ) {
      auto time = zoomInfo.PositionToTime( xx, -leftOffset );
      if ( sampleDur <= 0 )
         // Sample interval not defined (as for time track)
         buffer[xx] = GetValue( time );
      else {
         // The level of zoom-in may resolve individual samples.
         // If so, then instead of evaluating the envelope directly,
         // we draw a piecewise curve with knees at each sample time.
         // This actually makes clearer what happens as you drag envelope
         // points and make discontinuities.
         auto leftDiscreteTime = alignedTime +
            sampleDur * floor( ( time - alignedTime ) / sampleDur );
         if ( xx == 0 || leftDiscreteTime != prevDiscreteTime ) {
            prevDiscreteTime = leftDiscreteTime;
            prevSampleVal =
               GetValue( prevDiscreteTime, sampleDur );
            nextSampleVal =
               GetValue( prevDiscreteTime + sampleDur, sampleDur );
         }
         auto ratio = ( time - leftDiscreteTime ) / sampleDur;
         if ( GetExponential() )
            buffer[ xx ] = exp(
               ( 1.0 - ratio ) * log( prevSampleVal )
                  + ratio * log( nextSampleVal ) );
         else
            buffer[ xx ] =
               ( 1.0 - ratio ) * prevSampleVal + ratio * nextSampleVal;
      }
   }
}

// relative time
int Envelope::NumberOfPointsAfter(double t) const
{
   int lo,hi;
   BinarySearchForTime( lo, hi, t );

   return mEnv.size() - hi;
}

// relative time
double Envelope::NextPointAfter(double t) const
{
   int lo,hi;
   BinarySearchForTime( lo, hi, t );
   if (hi >= (int)mEnv.size())
      return t;
   else
      return mEnv[hi].GetT();
}

double Envelope::Average( double t0, double t1 ) const
{
  if( t0 == t1 )
    return GetValue( t0 );
  else
    return Integral( t0, t1 ) / (t1 - t0);
}

double Envelope::AverageOfInverse( double t0, double t1 ) const
{
  if( t0 == t1 )
    return 1.0 / GetValue( t0 );
  else
    return IntegralOfInverse( t0, t1 ) / (t1 - t0);
}

//
// Integration and debugging functions
//
// The functions below do not affect normal amplitude envelopes
// for waveforms, nor frequency envelopes for equalization.
// The 'Average' function also uses 'Integral'.
//

// A few helper functions to make the code below more readable.
static double InterpolatePoints(double y1, double y2, double factor, bool logarithmic)
{
   if(logarithmic)
      // you can use any base you want, it doesn't change the result
      return exp(log(y1) * (1.0 - factor) + log(y2) * factor);
   else
      return y1 * (1.0 - factor) + y2 * factor;
}
static double IntegrateInterpolated(double y1, double y2, double time, bool logarithmic)
{
   // Calculates: integral(interpolate(y1, y2, x), x = 0 .. time)
   // Integrating logarithmic interpolated segments is surprisingly simple. You can check this formula here:
   // http://www.wolframalpha.com/input/?i=integrate+10%5E%28log10%28y1%29*%28T-x%29%2FT%2Blog10%28y2%29*x%2FT%29+from+0+to+T
   // Again, the base you use for interpolation is irrelevant, the formula below should always use the natural
   // logarithm (i.e. 'log' in C/C++). If the denominator is too small, it's better to use linear interpolation
   // because the rounding errors would otherwise get too large. The threshold value is 1.0e-5 because at that
   // point the rounding errors become larger than the difference between linear and logarithmic (I tested this in Octave).
   if(logarithmic)
   {
      double l = log(y1 / y2);
      if(fabs(l) < 1.0e-5) // fall back to linear interpolation
         return (y1 + y2) * 0.5 * time;
      return (y1 - y2) / l * time;
   }
   else
   {
      return (y1 + y2) * 0.5 * time;
   }
}
static double IntegrateInverseInterpolated(double y1, double y2, double time, bool logarithmic)
{
   // Calculates: integral(1 / interpolate(y1, y2, x), x = 0 .. time)
   // This one is a bit harder. Linear:
   // http://www.wolframalpha.com/input/?i=integrate+1%2F%28y1*%28T-x%29%2FT%2By2*x%2FT%29+from+0+to+T
   // Logarithmic:
   // http://www.wolframalpha.com/input/?i=integrate+1%2F%2810%5E%28log10%28y1%29*%28T-x%29%2FT%2Blog10%28y2%29*x%2FT%29%29+from+0+to+T
   // Here both cases need a special case for y1 == y2. The threshold is 1.0e5 again, this is still the
   // best value in both cases.
   double l = log(y1 / y2);
   if(fabs(l) < 1.0e-5) // fall back to average
      return 2.0 / (y1 + y2) * time;
   if(logarithmic)
      return (y1 - y2) / (l * y1 * y2) * time;
   else
      return l / (y1 - y2) * time;
}
static double SolveIntegrateInverseInterpolated(double y1, double y2, double time, double area, bool logarithmic)
{
   // Calculates: solve (integral(1 / interpolate(y1, y2, x), x = 0 .. res) = area) for res
   // Don't try to derive these formulas by hand :). The threshold is 1.0e5 again.
   double a = area / time, res;
   if(logarithmic)
   {
      double l = log(y1 / y2);
      if(fabs(l) < 1.0e-5) // fall back to average
         res = a * (y1 + y2) * 0.5;
      else if(1.0 + a * y1 * l <= 0.0)
         res = 1.0;
      else
         res = log1p(a * y1 * l) / l;
   }
   else
   {
      if(fabs(y2 - y1) < 1.0e-5) // fall back to average
         res = a * (y1 + y2) * 0.5;
      else
         res = y1 * expm1(a * (y2 - y1)) / (y2 - y1);
   }
   return std::max(0.0, std::min(1.0, res)) * time;
}

// We should be able to write a very efficient memoizer for this
// but make sure it gets reset when the envelope is changed.
double Envelope::Integral( double t0, double t1 ) const
{
   if(t0 == t1)
      return 0.0;
   if(t0 > t1)
   {
      return -Integral(t1, t0); // this makes more sense than returning the default value
   }

   unsigned int count = mEnv.size();
   if(count == 0) // 'empty' envelope
      return (t1 - t0) * mDefaultValue;

   t0 -= mOffset;
   t1 -= mOffset;

   double total = 0.0, lastT, lastVal;
   unsigned int i; // this is the next point to check
   if(t0 < mEnv[0].GetT()) // t0 preceding the first point
   {
      if(t1 <= mEnv[0].GetT())
         return (t1 - t0) * mEnv[0].GetVal();
      i = 1;
      lastT = mEnv[0].GetT();
      lastVal = mEnv[0].GetVal();
      total += (lastT - t0) * lastVal;
   }
   else if(t0 >= mEnv[count - 1].GetT()) // t0 at or following the last point
   {
      return (t1 - t0) * mEnv[count - 1].GetVal();
   }
   else // t0 enclosed by points
   {
      // Skip any points that come before t0 using binary search
      int lo, hi;
      BinarySearchForTime(lo, hi, t0);
      lastVal = InterpolatePoints(mEnv[lo].GetVal(), mEnv[hi].GetVal(), (t0 - mEnv[lo].GetT()) / (mEnv[hi].GetT() - mEnv[lo].GetT()), mDB);
      lastT = t0;
      i = hi; // the point immediately after t0.
   }

   // loop through the rest of the envelope points until we get to t1
   while (1)
   {
      if(i >= count) // the requested range extends beyond the last point
      {
         return total + (t1 - lastT) * lastVal;
      }
      else if(mEnv[i].GetT() >= t1) // this point follows the end of the range
      {
         double thisVal = InterpolatePoints(mEnv[i - 1].GetVal(), mEnv[i].GetVal(), (t1 - mEnv[i - 1].GetT()) / (mEnv[i].GetT() - mEnv[i - 1].GetT()), mDB);
         return total + IntegrateInterpolated(lastVal, thisVal, t1 - lastT, mDB);
      }
      else // this point precedes the end of the range
      {
         total += IntegrateInterpolated(lastVal, mEnv[i].GetVal(), mEnv[i].GetT() - lastT, mDB);
         lastT = mEnv[i].GetT();
         lastVal = mEnv[i].GetVal();
         i++;
      }
   }
}

double Envelope::IntegralOfInverse( double t0, double t1 ) const
{
   if(t0 == t1)
      return 0.0;
   if(t0 > t1)
   {
      return -IntegralOfInverse(t1, t0); // this makes more sense than returning the default value
   }

   unsigned int count = mEnv.size();
   if(count == 0) // 'empty' envelope
      return (t1 - t0) / mDefaultValue;

   t0 -= mOffset;
   t1 -= mOffset;

   double total = 0.0, lastT, lastVal;
   unsigned int i; // this is the next point to check
   if(t0 < mEnv[0].GetT()) // t0 preceding the first point
   {
      if(t1 <= mEnv[0].GetT())
         return (t1 - t0) / mEnv[0].GetVal();
      i = 1;
      lastT = mEnv[0].GetT();
      lastVal = mEnv[0].GetVal();
      total += (lastT - t0) / lastVal;
   }
   else if(t0 >= mEnv[count - 1].GetT()) // t0 at or following the last point
   {
      return (t1 - t0) / mEnv[count - 1].GetVal();
   }
   else // t0 enclosed by points
   {
      // Skip any points that come before t0 using binary search
      int lo, hi;
      BinarySearchForTime(lo, hi, t0);
      lastVal = InterpolatePoints(mEnv[lo].GetVal(), mEnv[hi].GetVal(), (t0 - mEnv[lo].GetT()) / (mEnv[hi].GetT() - mEnv[lo].GetT()), mDB);
      lastT = t0;
      i = hi; // the point immediately after t0.
   }

   // loop through the rest of the envelope points until we get to t1
   while (1)
   {
      if(i >= count) // the requested range extends beyond the last point
      {
         return total + (t1 - lastT) / lastVal;
      }
      else if(mEnv[i].GetT() >= t1) // this point follows the end of the range
      {
         double thisVal = InterpolatePoints(mEnv[i - 1].GetVal(), mEnv[i].GetVal(), (t1 - mEnv[i - 1].GetT()) / (mEnv[i].GetT() - mEnv[i - 1].GetT()), mDB);
         return total + IntegrateInverseInterpolated(lastVal, thisVal, t1 - lastT, mDB);
      }
      else // this point precedes the end of the range
      {
         total += IntegrateInverseInterpolated(lastVal, mEnv[i].GetVal(), mEnv[i].GetT() - lastT, mDB);
         lastT = mEnv[i].GetT();
         lastVal = mEnv[i].GetVal();
         i++;
      }
   }
}

void Envelope::print() const
{
   for( unsigned int i = 0; i < mEnv.size(); i++ )
      wxPrintf( "(%.2f, %.2f)\n", mEnv[i].GetT(), mEnv[i].GetVal() );
}

static void checkResult( int n, double a, double b )
{
   if( (a-b > 0 ? a-b : b-a) > 0.0000001 )
   {
      wxPrintf( "Envelope:  Result #%d is: %f, should be %f\n", n, a, b );
      //exit( -1 );
   }
}
