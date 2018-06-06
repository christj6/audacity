/**********************************************************************

  Audacity: A Digital Audio Editor

  WaveClip.cpp

  ?? Dominic Mazzoni
  ?? Markus Meyer

*******************************************************************//**

\class WaveClip
\brief This allows multiple clips to be a part of one WaveTrack.

*//****************************************************************//**

\class WaveCache
\brief Cache used with WaveClip to cache wave information (for drawing).

*//*******************************************************************/

#include "WaveClip.h"

#include <math.h>
#include "MemoryX.h"
#include <functional>
#include <vector>
#include <wx/log.h>

#include "Sequence.h"
#include "Prefs.h"
#include "Envelope.h"
#include "Resample.h"
#include "Project.h"
#include "WaveTrack.h"
#include "Profiler.h"
#include "InconsistencyException.h"
#include "UserException.h"

#include <wx/listimpl.cpp>

#include "Experimental.h"

#ifdef _OPENMP
#include <omp.h>
#endif

class WaveCache {
public:
   WaveCache()
      : dirty(-1)
      , start(-1)
      , pps(0)
      , rate(-1)
      , where(0)
      , min(0)
      , max(0)
      , rms(0)
      , bl(0)
      , numODPixels(0)
   {
   }

   WaveCache(size_t len_, double pixelsPerSecond, double rate_, double t0, int dirty_)
      : dirty(dirty_)
      , len(len_)
      , start(t0)
      , pps(pixelsPerSecond)
      , rate(rate_)
      , where(1 + len)
      , min(len)
      , max(len)
      , rms(len)
      , bl(len)
      , numODPixels(0)
   {

      //find the number of OD pixels - the only way to do this is by recounting since we've lost some old cache.
      numODPixels = CountODPixels(0, len);
   }

   ~WaveCache()
   {
      ClearInvalidRegions();
   }

   int          dirty;
   const size_t len { 0 }; // counts pixels, not samples
   const double start;
   const double pps;
   const int    rate;
   std::vector<sampleCount> where;
   std::vector<float> min;
   std::vector<float> max;
   std::vector<float> rms;
   std::vector<int> bl;
   int         numODPixels;

   class InvalidRegion
   {
   public:
     InvalidRegion(size_t s, size_t e)
         : start(s), end(e)
     {}
     //start and end pixel count.  (not samples)
     size_t start;
     size_t end;
   };


   //Thread safe call to add a NEW region to invalidate.  If it overlaps with other regions, it unions the them.
   void AddInvalidRegion(sampleCount sampleStart, sampleCount sampleEnd)
   {
      //use pps to figure out where we are.  (pixels per second)
      if(pps ==0)
         return;
      double samplesPerPixel = rate/pps;
      //rate is SR, start is first time of the waveform (in second) on cache
      long invalStart = (sampleStart.as_double() - start*rate) / samplesPerPixel ;

      long invalEnd = (sampleEnd.as_double() - start*rate)/samplesPerPixel +1; //we should cover the end..

      //if they are both off the cache boundary in the same direction, the cache is missed,
      //so we are safe, and don't need to track this one.
      if((invalStart<0 && invalEnd <0) || (invalStart>=(long)len && invalEnd >= (long)len))
         return;

      //in all other cases, we need to clip the boundries so they make sense with the cache.
      //for some reason, the cache is set up to access up to array[len], not array[len-1]
      if(invalStart <0)
         invalStart =0;
      else if(invalStart > (long)len)
         invalStart = len;

      if(invalEnd <0)
         invalEnd =0;
      else if(invalEnd > (long)len)
         invalEnd = len;


      ODLocker locker(&mRegionsMutex);

      //look thru the region array for a place to insert.  We could make this more spiffy than a linear search
      //but right now it is not needed since there will usually only be one region (which grows) for OD loading.
      bool added=false;
      if(mRegions.size())
      {
         for(size_t i=0;i<mRegions.size();i++)
         {
            //if the regions intersect OR are pixel adjacent
            InvalidRegion &region = mRegions[i];
            if((long)region.start <= (invalEnd+1)
               && ((long)region.end + 1) >= invalStart)
            {
               //take the union region
               if((long)region.start > invalStart)
                  region.start = invalStart;
               if((long)region.end < invalEnd)
                  region.end = invalEnd;
               added=true;
               break;
            }

            //this bit doesn't make sense because it assumes we add in order - now we go backwards after the initial OD finishes
//            //this array is sorted by start/end points and has no overlaps.   If we've passed all possible intersections, insert.  The array will remain sorted.
//            if(region.end < invalStart)
//            {
//               mRegions.insert(
//                  mRegions.begin() + i,
//                  InvalidRegion{ invalStart, invalEnd }
//               );
//               break;
//            }
         }
      }

      if(!added)
      {
         InvalidRegion newRegion(invalStart, invalEnd);
         mRegions.insert(mRegions.begin(), newRegion);
      }


      //now we must go and patch up all the regions that overlap.  Overlapping regions will be adjacent.
      for(size_t i=1;i<mRegions.size();i++)
      {
         //if the regions intersect OR are pixel adjacent
         InvalidRegion &region = mRegions[i];
         InvalidRegion &prevRegion = mRegions[i - 1];
         if(region.start <= prevRegion.end+1
            && region.end + 1 >= prevRegion.start)
         {
            //take the union region
            if(region.start > prevRegion.start)
               region.start = prevRegion.start;
            if(region.end < prevRegion.end)
               region.end = prevRegion.end;

            mRegions.erase(mRegions.begin()+i-1);
               //musn't forget to reset cursor
               i--;
         }

         //if we are past the end of the region we added, we are past the area of regions that might be oversecting.
         if(invalEnd < 0 || (long)region.start > invalEnd)
         {
            break;
         }
      }
   }

   //lock before calling these in a section.  unlock after finished.
   int GetNumInvalidRegions() const {return mRegions.size();}
   size_t GetInvalidRegionStart(int i) const {return mRegions[i].start;}
   size_t GetInvalidRegionEnd(int i) const {return mRegions[i].end;}

   void ClearInvalidRegions()
   {
      mRegions.clear();
   }

   void LoadInvalidRegion(int ii, Sequence *sequence, bool updateODCount)
   {
      const auto invStart = GetInvalidRegionStart(ii);
      const auto invEnd = GetInvalidRegionEnd(ii);

      //before check number of ODPixels
      int regionODPixels = 0;
      if (updateODCount)
         regionODPixels = CountODPixels(invStart, invEnd);

      sequence->GetWaveDisplay(&min[invStart],
         &max[invStart],
         &rms[invStart],
         &bl[invStart],
         invEnd - invStart,
         &where[invStart]);

      //after check number of ODPixels
      if (updateODCount)
      {
         const int regionODPixelsAfter = CountODPixels(invStart, invEnd);
         numODPixels -= (regionODPixels - regionODPixelsAfter);
      }
   }

   void LoadInvalidRegions(Sequence *sequence, bool updateODCount)
   {
      //invalid regions are kept in a sorted array.
      for (int i = 0; i < GetNumInvalidRegions(); i++)
         LoadInvalidRegion(i, sequence, updateODCount);
   }

   int CountODPixels(size_t start, size_t end)
   {
      using namespace std;
      const int *begin = &bl[0];
      return count_if(begin + start, begin + end, bind2nd(less<int>(), 0));
   }

protected:
   std::vector<InvalidRegion> mRegions;
   ODLock mRegionsMutex;

};

WaveClip::WaveClip(const std::shared_ptr<DirManager> &projDirManager,
                   sampleFormat format, int rate, int colourIndex)
{
   mRate = rate;
   mColourIndex = colourIndex;
   mSequence = std::make_unique<Sequence>(projDirManager, format);

   mEnvelope = std::make_unique<Envelope>(true, 1e-7, 2.0, 1.0);

   mWaveCache = std::make_unique<WaveCache>();
   mSpecCache = std::make_unique<SpecCache>();
}

WaveClip::WaveClip(const WaveClip& orig,
                   const std::shared_ptr<DirManager> &projDirManager,
                   bool copyCutlines)
{
   // essentially a copy constructor - but you must pass in the
   // current project's DirManager, because we might be copying
   // from one project to another

   mOffset = orig.mOffset;
   mRate = orig.mRate;
   mColourIndex = orig.mColourIndex;
   mSequence = std::make_unique<Sequence>(*orig.mSequence, projDirManager);

   mEnvelope = std::make_unique<Envelope>(*orig.mEnvelope);

   mWaveCache = std::make_unique<WaveCache>();
   mSpecCache = std::make_unique<SpecCache>();

   if ( copyCutlines )
      for (const auto &clip: orig.mCutLines)
         mCutLines.push_back
            ( make_movable<WaveClip>( *clip, projDirManager, true ) );

   mIsPlaceholder = orig.GetIsPlaceholder();
}

WaveClip::WaveClip(const WaveClip& orig,
                   const std::shared_ptr<DirManager> &projDirManager,
                   bool copyCutlines,
                   double t0, double t1)
{
   // Copy only a range of the other WaveClip

   mOffset = orig.mOffset;
   mRate = orig.mRate;
   mColourIndex = orig.mColourIndex;

   mWaveCache = std::make_unique<WaveCache>();
   mSpecCache = std::make_unique<SpecCache>();

   mIsPlaceholder = orig.GetIsPlaceholder();

   sampleCount s0, s1;

   orig.TimeToSamplesClip(t0, &s0);
   orig.TimeToSamplesClip(t1, &s1);

   mSequence = orig.mSequence->Copy(s0, s1);

   mEnvelope = std::make_unique<Envelope>(
      *orig.mEnvelope,
      mOffset + s0.as_double()/mRate,
      mOffset + s1.as_double()/mRate
   );

   if ( copyCutlines )
      // Copy cutline clips that fall in the range
      for (const auto &ppClip : orig.mCutLines)
      {
         const WaveClip* clip = ppClip.get();
         double cutlinePosition = orig.mOffset + clip->GetOffset();
         if (cutlinePosition >= t0 && cutlinePosition <= t1)
         {
            auto newCutLine =
               make_movable< WaveClip >( *clip, projDirManager, true );
            newCutLine->SetOffset( cutlinePosition - t0 );
            mCutLines.push_back(std::move(newCutLine));
         }
      }
}


WaveClip::~WaveClip()
{
}

void WaveClip::SetOffset(double offset)
// NOFAIL-GUARANTEE
{
    mOffset = offset;
}

bool WaveClip::GetSamples(samplePtr buffer, sampleFormat format,
                   sampleCount start, size_t len, bool mayThrow) const
{
   return mSequence->Get(buffer, format, start, len, mayThrow);
}

void WaveClip::SetSamples(samplePtr buffer, sampleFormat format,
                   sampleCount start, size_t len)
// STRONG-GUARANTEE
{
   // use STRONG-GUARANTEE
   mSequence->SetSamples(buffer, format, start, len);

   // use NOFAIL-GUARANTEE
   MarkChanged();
}

BlockArray* WaveClip::GetSequenceBlockArray()
{
   return &mSequence->GetBlockArray();
}

double WaveClip::GetStartTime() const
{
   // JS: mOffset is the minimum value and it is returned; no clipping to 0
   return mOffset;
}

double WaveClip::GetEndTime() const
{
   auto numSamples = mSequence->GetNumSamples();

   double maxLen = mOffset + (numSamples+mAppendBufferLen).as_double()/mRate;
   // JS: calculated value is not the length;
   // it is a maximum value and can be negative; no clipping to 0

   return maxLen;
}

sampleCount WaveClip::GetStartSample() const
{
   return sampleCount( floor(mOffset * mRate + 0.5) );
}

sampleCount WaveClip::GetEndSample() const
{
   return GetStartSample() + mSequence->GetNumSamples();
}

sampleCount WaveClip::GetNumSamples() const
{
   return mSequence->GetNumSamples();
}

bool WaveClip::WithinClip(double t) const
{
   auto ts = (sampleCount)floor(t * mRate + 0.5);
   return ts > GetStartSample() && ts < GetEndSample() + mAppendBufferLen;
}

bool WaveClip::BeforeClip(double t) const
{
   auto ts = (sampleCount)floor(t * mRate + 0.5);
   return ts <= GetStartSample();
}

bool WaveClip::AfterClip(double t) const
{
   auto ts = (sampleCount)floor(t * mRate + 0.5);
   return ts >= GetEndSample() + mAppendBufferLen;
}

///Delete the wave cache - force redraw.  Thread-safe
void WaveClip::ClearWaveCache()
{
   ODLocker locker(&mWaveCacheMutex);
   mWaveCache = std::make_unique<WaveCache>();
}

///Adds an invalid region to the wavecache so it redraws that portion only.
void WaveClip::AddInvalidRegion(sampleCount startSample, sampleCount endSample)
{
   ODLocker locker(&mWaveCacheMutex);
   if(mWaveCache!=NULL)
      mWaveCache->AddInvalidRegion(startSample,endSample);
}

namespace {

inline
void findCorrection(const std::vector<sampleCount> &oldWhere, size_t oldLen,
         size_t newLen,
         double t0, double rate, double samplesPerPixel,
         int &oldX0, double &correction)
{
   // Mitigate the accumulation of location errors
   // in copies of copies of ... of caches.
   // Look at the loop that populates "where" below to understand this.

   // Find the sample position that is the origin in the old cache.
   const double oldWhere0 = oldWhere[1].as_double() - samplesPerPixel;
   const double oldWhereLast = oldWhere0 + oldLen * samplesPerPixel;
   // Find the length in samples of the old cache.
   const double denom = oldWhereLast - oldWhere0;

   // What sample would go in where[0] with no correction?
   const double guessWhere0 = t0 * rate;

   if ( // Skip if old and NEW are disjoint:
      oldWhereLast <= guessWhere0 ||
      guessWhere0 + newLen * samplesPerPixel <= oldWhere0 ||
      // Skip unless denom rounds off to at least 1.
      denom < 0.5)
   {
      // The computation of oldX0 in the other branch
      // may underflow and the assertion would be violated.
      oldX0 =  oldLen;
      correction = 0.0;
   }
   else
   {
      // What integer position in the old cache array does that map to?
      // (even if it is out of bounds)
      oldX0 = floor(0.5 + oldLen * (guessWhere0 - oldWhere0) / denom);
      // What sample count would the old cache have put there?
      const double where0 = oldWhere0 + double(oldX0) * samplesPerPixel;
      // What correction is needed to align the NEW cache with the old?
      const double correction0 = where0 - guessWhere0;
      correction = std::max(-samplesPerPixel, std::min(samplesPerPixel, correction0));
      wxASSERT(correction == correction0);
   }
}

inline void
fillWhere(std::vector<sampleCount> &where, size_t len, double bias, double correction,
          double t0, double rate, double samplesPerPixel)
{
   // Be careful to make the first value non-negative
   const double w0 = 0.5 + correction + bias + t0 * rate;
   where[0] = sampleCount( std::max(0.0, floor(w0)) );
   for (decltype(len) x = 1; x < len + 1; x++)
      where[x] = sampleCount( floor(w0 + double(x) * samplesPerPixel) );
}

}

//
// Getting high-level data from the track for screen display and
// clipping calculations
//

bool WaveClip::GetWaveDisplay(WaveDisplay &display, double t0,
                               double pixelsPerSecond, bool &isLoadingOD) const
{
   const bool allocated = (display.where != 0);

   const size_t numPixels = (int)display.width;

   size_t p0 = 0;         // least column requiring computation
   size_t p1 = numPixels; // greatest column requiring computation, plus one

   float *min;
   float *max;
   float *rms;
   int *bl;
   std::vector<sampleCount> *pWhere;

   if (allocated) {
      // assume ownWhere is filled.
      min = &display.min[0];
      max = &display.max[0];
      rms = &display.rms[0];
      bl = &display.bl[0];
      pWhere = &display.ownWhere;
   }
   else {
      // Lock the list of invalid regions
      ODLocker locker(&mWaveCacheMutex);

      const double tstep = 1.0 / pixelsPerSecond;
      const double samplesPerPixel = mRate * tstep;

      // Make a tolerant comparison of the pps values in this wise:
      // accumulated difference of times over the number of pixels is less than
      // a sample period.
      const bool ppsMatch = mWaveCache &&
         (fabs(tstep - 1.0 / mWaveCache->pps) * numPixels < (1.0 / mRate));

      const bool match =
         mWaveCache &&
         ppsMatch &&
         mWaveCache->len > 0 &&
         mWaveCache->dirty == mDirty;

      if (match &&
         mWaveCache->start == t0 &&
         mWaveCache->len >= numPixels) {
         mWaveCache->LoadInvalidRegions(mSequence.get(), true);
         mWaveCache->ClearInvalidRegions();

         // Satisfy the request completely from the cache
         display.min = &mWaveCache->min[0];
         display.max = &mWaveCache->max[0];
         display.rms = &mWaveCache->rms[0];
         display.bl = &mWaveCache->bl[0];
         display.where = &mWaveCache->where[0];
         isLoadingOD = mWaveCache->numODPixels > 0;
         return true;
      }

      std::unique_ptr<WaveCache> oldCache(std::move(mWaveCache));

      int oldX0 = 0;
      double correction = 0.0;
      size_t copyBegin = 0, copyEnd = 0;
      if (match) {
         findCorrection(oldCache->where, oldCache->len, numPixels,
            t0, mRate, samplesPerPixel,
            oldX0, correction);
         // Remember our first pixel maps to oldX0 in the old cache,
         // possibly out of bounds.
         // For what range of pixels can data be copied?
         copyBegin = std::min<size_t>(numPixels, std::max(0, -oldX0));
         copyEnd = std::min<size_t>(numPixels, std::max(0,
            (int)oldCache->len - oldX0
         ));
      }
      if (!(copyEnd > copyBegin))
         oldCache.reset(0);

      mWaveCache = std::make_unique<WaveCache>(numPixels, pixelsPerSecond, mRate, t0, mDirty);
      min = &mWaveCache->min[0];
      max = &mWaveCache->max[0];
      rms = &mWaveCache->rms[0];
      bl = &mWaveCache->bl[0];
      pWhere = &mWaveCache->where;

      fillWhere(*pWhere, numPixels, 0.0, correction,
         t0, mRate, samplesPerPixel);

      // The range of pixels we must fetch from the Sequence:
      p0 = (copyBegin > 0) ? 0 : copyEnd;
      p1 = (copyEnd >= numPixels) ? copyBegin : numPixels;

      // Optimization: if the old cache is good and overlaps
      // with the current one, re-use as much of the cache as
      // possible

      if (oldCache) {

         //TODO: only load inval regions if
         //necessary.  (usually is the case, so no rush.)
         //also, we should be updating the NEW cache, but here we are patching the old one up.
         oldCache->LoadInvalidRegions(mSequence.get(), false);
         oldCache->ClearInvalidRegions();

         // Copy what we can from the old cache.
         const int length = copyEnd - copyBegin;
         const size_t sizeFloats = length * sizeof(float);
         const int srcIdx = (int)copyBegin + oldX0;
         memcpy(&min[copyBegin], &oldCache->min[srcIdx], sizeFloats);
         memcpy(&max[copyBegin], &oldCache->max[srcIdx], sizeFloats);
         memcpy(&rms[copyBegin], &oldCache->rms[srcIdx], sizeFloats);
         memcpy(&bl[copyBegin], &oldCache->bl[srcIdx], length * sizeof(int));
      }
   }

   if (p1 > p0) {
      // Cache was not used or did not satisfy the whole request
      std::vector<sampleCount> &where = *pWhere;

      /* handle values in the append buffer */

      auto numSamples = mSequence->GetNumSamples();
      auto a = p0;

      // Not all of the required columns might be in the sequence.
      // Some might be in the append buffer.
      for (; a < p1; ++a) {
         if (where[a + 1] > numSamples)
            break;
      }

      // Handle the columns that land in the append buffer.
      //compute the values that are outside the overlap from scratch.
      if (a < p1) {
         sampleFormat seqFormat = mSequence->GetSampleFormat();
         bool didUpdate = false;
         for(auto i = a; i < p1; i++) {
            auto left = std::max(sampleCount{ 0 },
                                 where[i] - numSamples);
            auto right = std::min(sampleCount{ mAppendBufferLen },
                                  where[i + 1] - numSamples);

            //wxCriticalSectionLocker locker(mAppendCriticalSection);

            if (right > left) {
               Floats b;
               float *pb{};
               // left is nonnegative and at most mAppendBufferLen:
               auto sLeft = left.as_size_t();
               // The difference is at most mAppendBufferLen:
               size_t len = ( right - left ).as_size_t();

               if (seqFormat == floatSample)
                  pb = &((float *)mAppendBuffer.ptr())[sLeft];
               else {
                  b.reinit(len);
                  pb = b.get();
                  CopySamples(mAppendBuffer.ptr() + sLeft * SAMPLE_SIZE(seqFormat),
                              seqFormat,
                              (samplePtr)pb, floatSample, len);
               }

               float theMax, theMin, sumsq;
               {
                  const float val = pb[0];
                  theMax = theMin = val;
                  sumsq = val * val;
               }
               for(decltype(len) j = 1; j < len; j++) {
                  const float val = pb[j];
                  theMax = std::max(theMax, val);
                  theMin = std::min(theMin, val);
                  sumsq += val * val;
               }

               min[i] = theMin;
               max[i] = theMax;
               rms[i] = (float)sqrt(sumsq / len);
               bl[i] = 1; //for now just fake it.

               didUpdate=true;
            }
         }

         // Shrink the right end of the range to fetch from Sequence
         if(didUpdate)
            p1 = a;
      }

      // Done with append buffer, now fetch the rest of the cache miss
      // from the sequence
      if (p1 > p0) {
         if (!mSequence->GetWaveDisplay(&min[p0],
                                        &max[p0],
                                        &rms[p0],
                                        &bl[p0],
                                        p1-p0,
                                        &where[p0]))
         {
            isLoadingOD=false;
            return false;
         }
      }
   }

   //find the number of OD pixels - the only way to do this is by recounting
   if (!allocated) {
      // Now report the results
      display.min = min;
      display.max = max;
      display.rms = rms;
      display.bl = bl;
      display.where = &(*pWhere)[0];
      isLoadingOD = mWaveCache->numODPixels > 0;
   }
   else {
      using namespace std;
      isLoadingOD =
         count_if(display.ownBl.begin(), display.ownBl.end(),
                  bind2nd(less<int>(), 0)) > 0;
   }

   return true;
}

namespace {

}

std::pair<float, float> WaveClip::GetMinMax(
   double t0, double t1, bool mayThrow) const
{
   if (t0 > t1) {
      if (mayThrow)
         THROW_INCONSISTENCY_EXCEPTION;
      return {
         0.f,  // harmless, but unused since Sequence::GetMinMax does not use these values
         0.f   // harmless, but unused since Sequence::GetMinMax does not use these values
      };
   }

   if (t0 == t1)
      return{ 0.f, 0.f };

   sampleCount s0, s1;

   TimeToSamplesClip(t0, &s0);
   TimeToSamplesClip(t1, &s1);

   return mSequence->GetMinMax(s0, s1-s0, mayThrow);
}

float WaveClip::GetRMS(double t0, double t1, bool mayThrow) const
{
   if (t0 > t1) {
      if (mayThrow)
         THROW_INCONSISTENCY_EXCEPTION;
      return 0.f;
   }

   if (t0 == t1)
      return 0.f;

   sampleCount s0, s1;

   TimeToSamplesClip(t0, &s0);
   TimeToSamplesClip(t1, &s1);

   return mSequence->GetRMS(s0, s1-s0, mayThrow);
}

void WaveClip::ConvertToSampleFormat(sampleFormat format)
{
   // Note:  it is not necessary to do this recursively to cutlines.
   // They get converted as needed when they are expanded.

   auto bChanged = mSequence->ConvertToSampleFormat(format);
   if (bChanged)
      MarkChanged();
}

void WaveClip::TimeToSamplesClip(double t0, sampleCount *s0) const
{
   if (t0 < mOffset)
      *s0 = 0;
   else if (t0 > mOffset + mSequence->GetNumSamples().as_double()/mRate)
      *s0 = mSequence->GetNumSamples();
   else
      *s0 = sampleCount( floor(((t0 - mOffset) * mRate) + 0.5) );
}

void WaveClip::ClearDisplayRect() const
{
   mDisplayRect.x = mDisplayRect.y = -1;
   mDisplayRect.width = mDisplayRect.height = -1;
}

void WaveClip::SetDisplayRect(const wxRect& r) const
{
   mDisplayRect = r;
}

void WaveClip::GetDisplayRect(wxRect* r)
{
   *r = mDisplayRect;
}

void WaveClip::Append(samplePtr buffer, sampleFormat format,
                      size_t len, unsigned int stride /* = 1 */)
// PARTIAL-GUARANTEE in case of exceptions:
// Some prefix (maybe none) of the buffer is appended, and no content already
// flushed to disk is lost.
{
   //wxLogDebug(wxT("Append: len=%lli"), (long long) len);

   auto maxBlockSize = mSequence->GetMaxBlockSize();
   auto blockSize = mSequence->GetIdealAppendLen();
   sampleFormat seqFormat = mSequence->GetSampleFormat();

   if (!mAppendBuffer.ptr())
      mAppendBuffer.Allocate(maxBlockSize, seqFormat);

   auto cleanup = finally( [&] {
      // use NOFAIL-GUARANTEE
      MarkChanged();
   } );

   for(;;) {
      if (mAppendBufferLen >= blockSize) {
         // flush some previously appended contents
         // use STRONG-GUARANTEE
         mSequence->Append(mAppendBuffer.ptr(), seqFormat, blockSize);

         // use NOFAIL-GUARANTEE for rest of this "if"
         memmove(mAppendBuffer.ptr(),
                 mAppendBuffer.ptr() + blockSize * SAMPLE_SIZE(seqFormat),
                 (mAppendBufferLen - blockSize) * SAMPLE_SIZE(seqFormat));
         mAppendBufferLen -= blockSize;
         blockSize = mSequence->GetIdealAppendLen();
      }

      if (len == 0)
         break;

      // use NOFAIL-GUARANTEE for rest of this "for"
      wxASSERT(mAppendBufferLen <= maxBlockSize);
      auto toCopy = std::min(len, maxBlockSize - mAppendBufferLen);

      CopySamples(buffer, format,
                  mAppendBuffer.ptr() + mAppendBufferLen * SAMPLE_SIZE(seqFormat),
                  seqFormat,
                  toCopy,
                  true, // high quality
                  stride);

      mAppendBufferLen += toCopy;
      buffer += toCopy * SAMPLE_SIZE(format) * stride;
      len -= toCopy;
   }
}

void WaveClip::AppendAlias(const wxString &fName, sampleCount start,
                            size_t len, int channel,bool useOD)
// STRONG-GUARANTEE
{
   // use STRONG-GUARANTEE
   mSequence->AppendAlias(fName, start, len, channel,useOD);

   // use NOFAIL-GUARANTEE
   MarkChanged();
}

void WaveClip::AppendCoded(const wxString &fName, sampleCount start,
                            size_t len, int channel, int decodeType)
// STRONG-GUARANTEE
{
   // use STRONG-GUARANTEE
   mSequence->AppendCoded(fName, start, len, channel, decodeType);

   // use NOFAIL-GUARANTEE
   MarkChanged();
}

void WaveClip::Flush()
// NOFAIL-GUARANTEE that the clip will be in a flushed state.
// PARTIAL-GUARANTEE in case of exceptions:
// Some initial portion (maybe none) of the append buffer of the
// clip gets appended; no previously flushed contents are lost.
{
   //wxLogDebug(wxT("WaveClip::Flush"));
   //wxLogDebug(wxT("   mAppendBufferLen=%lli"), (long long) mAppendBufferLen);
   //wxLogDebug(wxT("   previous sample count %lli"), (long long) mSequence->GetNumSamples());

   if (mAppendBufferLen > 0) {

      auto cleanup = finally( [&] {
         // Blow away the append buffer even in case of failure.  May lose some
         // data but don't leave the track in an un-flushed state.

         // Use NOFAIL-GUARANTEE of these steps.
         mAppendBufferLen = 0;
         MarkChanged();
      } );

      mSequence->Append(mAppendBuffer.ptr(), mSequence->GetSampleFormat(),
         mAppendBufferLen);
   }

   //wxLogDebug(wxT("now sample count %lli"), (long long) mSequence->GetNumSamples());
}

void WaveClip::Paste(double t0, const WaveClip* other)
// STRONG-GUARANTEE
{
   const bool clipNeedsResampling = other->mRate != mRate;
   const bool clipNeedsNewFormat =
      other->mSequence->GetSampleFormat() != mSequence->GetSampleFormat();
   std::unique_ptr<WaveClip> newClip;
   const WaveClip* pastedClip;

   if (clipNeedsResampling || clipNeedsNewFormat)
   {
      newClip =
         std::make_unique<WaveClip>(*other, mSequence->GetDirManager(), true);
      if (clipNeedsResampling)
         // The other clip's rate is different from ours, so resample
         newClip->Resample(mRate);
      if (clipNeedsNewFormat)
         // Force sample formats to match.
         newClip->ConvertToSampleFormat(mSequence->GetSampleFormat());
      pastedClip = newClip.get();
   }
   else
   {
      // No resampling or format change needed, just use original clip without making a copy
      pastedClip = other;
   }

   // Paste cut lines contained in pasted clip
   WaveClipHolders newCutlines;
   for (const auto &cutline: pastedClip->mCutLines)
   {
      newCutlines.push_back(
         make_movable<WaveClip>
            ( *cutline, mSequence->GetDirManager(),
              // Recursively copy cutlines of cutlines.  They don't need
              // their offsets adjusted.
              true));
      newCutlines.back()->Offset(t0 - mOffset);
   }

   sampleCount s0;
   TimeToSamplesClip(t0, &s0);

   // Assume STRONG-GUARANTEE from Sequence::Paste
   mSequence->Paste(s0, pastedClip->mSequence.get());

   // Assume NOFAIL-GUARANTEE in the remaining
   MarkChanged();
   OffsetCutLines(t0, pastedClip->GetEndTime() - pastedClip->GetStartTime());

   for (auto &holder : newCutlines)
      mCutLines.push_back(std::move(holder));
}

void WaveClip::InsertSilence( double t, double len, double *pEnvelopeValue )
// STRONG-GUARANTEE
{
   sampleCount s0;
   TimeToSamplesClip(t, &s0);
   auto slen = (sampleCount)floor(len * mRate + 0.5);

   // use STRONG-GUARANTEE
   GetSequence()->InsertSilence(s0, slen);

   // use NOFAIL-GUARANTEE
   OffsetCutLines(t, len);

   auto pEnvelope = GetEnvelope();
   if ( pEnvelopeValue ) {
   }
   else
      pEnvelope->InsertSpace( t, len );

   MarkChanged();
}

void WaveClip::AppendSilence( double len, double envelopeValue )
// STRONG-GUARANTEE
{
   auto t = GetEndTime();
   InsertSilence( t, len, &envelopeValue );
}

void WaveClip::Clear(double t0, double t1)
// STRONG-GUARANTEE
{
   sampleCount s0, s1;

   TimeToSamplesClip(t0, &s0);
   TimeToSamplesClip(t1, &s1);

   // use STRONG-GUARANTEE
   GetSequence()->Delete(s0, s1-s0);

   // use NOFAIL-GUARANTEE in the remaining

   // msmeyer
   //
   // Delete all cutlines that are within the given area, if any.
   //
   // Note that when cutlines are active, two functions are used:
   // Clear() and ClearAndAddCutLine(). ClearAndAddCutLine() is called
   // whenever the user directly calls a command that removes some audio, e.g.
   // "Cut" or "Clear" from the menu. This command takes care about recursive
   // preserving of cutlines within clips. Clear() is called when internal
   // operations want to remove audio. In the latter case, it is the right
   // thing to just remove all cutlines within the area.
   //
   double clip_t0 = t0;
   double clip_t1 = t1;
   if (clip_t0 < GetStartTime())
      clip_t0 = GetStartTime();
   if (clip_t1 > GetEndTime())
      clip_t1 = GetEndTime();

   // May DELETE as we iterate, so don't use range-for
   for (auto it = mCutLines.begin(); it != mCutLines.end();)
   {
      WaveClip* clip = it->get();
      double cutlinePosition = mOffset + clip->GetOffset();
      if (cutlinePosition >= t0 && cutlinePosition <= t1)
      {
         // This cutline is within the area, DELETE it
         it = mCutLines.erase(it);
      }
      else
      {
         if (cutlinePosition >= t1)
         {
            clip->Offset(clip_t0 - clip_t1);
         }
         ++it;
      }
   }

   if (t0 < GetStartTime())
      Offset(-(GetStartTime() - t0));

   MarkChanged();
}

void WaveClip::ClearAndAddCutLine(double t0, double t1)
// WEAK-GUARANTEE
// this WaveClip remains destructible in case of AudacityException.
// But some cutlines may be deleted
{
   if (t0 > GetEndTime() || t1 < GetStartTime())
      return; // time out of bounds

   const double clip_t0 = std::max( t0, GetStartTime() );
   const double clip_t1 = std::min( t1, GetEndTime() );

   auto newClip = make_movable< WaveClip >
      (*this, mSequence->GetDirManager(), true, clip_t0, clip_t1);

   newClip->SetOffset( clip_t0 - mOffset );

   // Remove cutlines from this clip that were in the selection, shift
   // left those that were after the selection
   // May DELETE as we iterate, so don't use range-for
   for (auto it = mCutLines.begin(); it != mCutLines.end();)
   {
      WaveClip* clip = it->get();
      double cutlinePosition = mOffset + clip->GetOffset();
      if (cutlinePosition >= t0 && cutlinePosition <= t1)
         it = mCutLines.erase(it);
      else
      {
         if (cutlinePosition >= t1)
         {
            clip->Offset(clip_t0 - clip_t1);
         }
         ++it;
      }
   }

   // Clear actual audio data
   sampleCount s0, s1;

   TimeToSamplesClip(t0, &s0);
   TimeToSamplesClip(t1, &s1);

   // use WEAK-GUARANTEE
   GetSequence()->Delete(s0, s1-s0);

   if (t0 < GetStartTime())
      Offset(-(GetStartTime() - t0));

   MarkChanged();

   mCutLines.push_back(std::move(newClip));
}

bool WaveClip::FindCutLine(double cutLinePosition,
                           double* cutlineStart /* = NULL */,
                           double* cutlineEnd /* = NULL */) const
{
   for (const auto &cutline: mCutLines)
   {
      if (fabs(mOffset + cutline->GetOffset() - cutLinePosition) < 0.0001)
      {
         if (cutlineStart)
            *cutlineStart = mOffset+cutline->GetStartTime();
         if (cutlineEnd)
            *cutlineEnd = mOffset+cutline->GetEndTime();
         return true;
      }
   }

   return false;
}

void WaveClip::ExpandCutLine(double cutLinePosition)
// STRONG-GUARANTEE
{
   auto end = mCutLines.end();
   auto it = std::find_if( mCutLines.begin(), end,
      [&](const WaveClipHolder &cutline) {
         return fabs(mOffset + cutline->GetOffset() - cutLinePosition) < 0.0001;
      } );

   if ( it != end ) {
      auto cutline = it->get();
      // assume STRONG-GUARANTEE from Paste

      Paste(mOffset+cutline->GetOffset(), cutline);
      // Now erase the cutline,
      // but be careful to find it again, because Paste above may
      // have modified the array of cutlines (if our cutline contained
      // another cutline!), invalidating the iterator we had.
      end = mCutLines.end();
      it = std::find_if(mCutLines.begin(), end,
         [=](const WaveClipHolder &p) { return p.get() == cutline; });
      if (it != end)
         mCutLines.erase(it); // deletes cutline!
      else {
         wxASSERT(false);
      }
   }
}

bool WaveClip::RemoveCutLine(double cutLinePosition)
{
   for (auto it = mCutLines.begin(); it != mCutLines.end(); ++it)
   {
      const auto &cutline = *it;
      if (fabs(mOffset + cutline->GetOffset() - cutLinePosition) < 0.0001)
      {
         mCutLines.erase(it); // deletes cutline!
         return true;
      }
   }

   return false;
}

void WaveClip::OffsetCutLines(double t0, double len)
// NOFAIL-GUARANTEE
{
   for (const auto &cutLine : mCutLines)
   {
      if (mOffset + cutLine->GetOffset() >= t0)
         cutLine->Offset(len);
   }
}

void WaveClip::Lock()
{
   GetSequence()->Lock();
   for (const auto &cutline: mCutLines)
      cutline->Lock();
}

void WaveClip::CloseLock()
{
   GetSequence()->CloseLock();
   for (const auto &cutline: mCutLines)
      cutline->CloseLock();
}

void WaveClip::Unlock()
{
   GetSequence()->Unlock();
   for (const auto &cutline: mCutLines)
      cutline->Unlock();
}

void WaveClip::Resample(int rate, ProgressDialog *progress)
// STRONG-GUARANTEE
{
   // Note:  it is not necessary to do this recursively to cutlines.
   // They get resampled as needed when they are expanded.

   if (rate == mRate)
      return; // Nothing to do

   double factor = (double)rate / (double)mRate;
   ::Resample resample(true, factor, factor); // constant rate resampling

   const size_t bufsize = 65536;
   Floats inBuffer{ bufsize };
   Floats outBuffer{ bufsize };
   sampleCount pos = 0;
   bool error = false;
   int outGenerated = 0;
   auto numSamples = mSequence->GetNumSamples();

   auto newSequence =
      std::make_unique<Sequence>(mSequence->GetDirManager(), mSequence->GetSampleFormat());

   /**
    * We want to keep going as long as we have something to feed the resampler
    * with OR as long as the resampler spews out samples (which could continue
    * for a few iterations after we stop feeding it)
    */
   while (pos < numSamples || outGenerated > 0)
   {
      const auto inLen = limitSampleBufferSize( bufsize, numSamples - pos );

      bool isLast = ((pos + inLen) == numSamples);

      if (!mSequence->Get((samplePtr)inBuffer.get(), floatSample, pos, inLen, true))
      {
         error = true;
         break;
      }

      const auto results = resample.Process(factor, inBuffer.get(), inLen, isLast,
                                            outBuffer.get(), bufsize);
      outGenerated = results.second;

      pos += results.first;

      if (outGenerated < 0)
      {
         error = true;
         break;
      }

      newSequence->Append((samplePtr)outBuffer.get(), floatSample,
                          outGenerated);

      if (progress)
      {
         auto updateResult = progress->Update(
            pos.as_long_long(),
            numSamples.as_long_long()
         );
         error = (updateResult != ProgressResult::Success);
         if (error)
            throw UserException{};
      }
   }

   if (error)
      throw SimpleMessageBoxException{
         _("Resampling failed.")
      };
   else
   {
      // Use NOFAIL-GUARANTEE in these steps

      // Invalidate wave display cache
      mWaveCache = std::make_unique<WaveCache>();
      // Invalidate the spectrum display cache
      mSpecCache = std::make_unique<SpecCache>();

      mSequence = std::move(newSequence);
      mRate = rate;
   }
}

// Used by commands which interact with clips using the keyboard.
// When two clips are immediately next to each other, the GetEndTime()
// of the first clip and the GetStartTime() of the second clip may not
// be exactly equal due to rounding errors.
bool WaveClip::SharesBoundaryWithNextClip(const WaveClip* next) const
{
   double endThis = GetRate() * GetOffset() + GetNumSamples().as_double();
   double startNext = next->GetRate() * next->GetOffset();

   // given that a double has about 15 significant digits, using a criterion
   // of half a sample should be safe in all normal usage.
   return fabs(startNext - endThis) < 0.5;
}
