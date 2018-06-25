/**********************************************************************

  Audacity: A Digital Audio Editor

  Mix.cpp

  Dominic Mazzoni
  Markus Meyer
  Vaughan Johnson

*******************************************************************//**

\class Mixer
\brief Functions for doing the mixdown of the tracks.

*//****************************************************************//**

\class MixerSpec
\brief Class used with Mixer.

*//*******************************************************************/


#include "Audacity.h"
#include "Mix.h"

#include <math.h>

#include <wx/textctrl.h>
#include <wx/progdlg.h>
#include <wx/timer.h>
#include <wx/intl.h>

#include "WaveTrack.h"
#include "DirManager.h"
#include "Internat.h"
#include "Prefs.h"
#include "Project.h"
#include "Resample.h"
#include "float_cast.h"

Mixer::Mixer(const WaveTrackConstArray &inputTracks,
             bool mayThrow,
             double startTime, double stopTime,
             unsigned numOutChannels, size_t outBufferSize, bool outInterleaved,
             double outRate, sampleFormat outFormat,
             bool highQuality, MixerSpec *mixerSpec)
   : mNumInputTracks { inputTracks.size() }

   // This is the number of samples grabbed in one go from a track
   // and placed in a queue, when mixing with resampling.
   // (Should we use WaveTrack::GetBestBlockSize instead?)
   , mQueueMaxLen{ 65536 }
   , mSampleQueue{ mNumInputTracks, mQueueMaxLen }

   , mNumChannels{ numOutChannels }
   , mGains{ mNumChannels }

   , mMayThrow{ mayThrow }
{
   mHighQuality = highQuality;
   mInputTrack.reinit(mNumInputTracks);

   // mSamplePos holds for each track the next sample position not
   // yet processed.
   mSamplePos.reinit(mNumInputTracks);
   for(size_t i=0; i<mNumInputTracks; i++) {
      mInputTrack[i].SetTrack(inputTracks[i]);
      mSamplePos[i] = inputTracks[i]->TimeToLongSamples(startTime);
   }
   mT0 = startTime;
   mT1 = stopTime;
   mTime = startTime;
   mBufferSize = outBufferSize;
   mInterleaved = outInterleaved;
   mRate = outRate;
   mSpeed = 1.0;
   mFormat = outFormat;
   mApplyTrackGains = true;
   if( mixerSpec && mixerSpec->GetNumChannels() == mNumChannels &&
         mixerSpec->GetNumTracks() == mNumInputTracks )
      mMixerSpec = mixerSpec;
   else
      mMixerSpec = NULL;

   if (mInterleaved) {
      mNumBuffers = 1;
      mInterleavedBufferSize = mBufferSize * mNumChannels;
   }
   else {
      mNumBuffers = mNumChannels;
      mInterleavedBufferSize = mBufferSize;
   }

   mBuffer.reinit(mNumBuffers);
   mTemp.reinit(mNumBuffers);
   for (unsigned int c = 0; c < mNumBuffers; c++) {
      mBuffer[c].Allocate(mInterleavedBufferSize, mFormat);
      mTemp[c].Allocate(mInterleavedBufferSize, floatSample);
   }
   mFloatBuffer = Floats{ mInterleavedBufferSize };

   // But cut the queue into blocks of this finer size
   // for variable rate resampling.  Each block is resampled at some
   // constant rate.
   mProcessLen = 1024;

   // Position in each queue of the start of the next block to resample.
   mQueueStart.reinit(mNumInputTracks);

   // For each queue, the number of available samples after the queue start.
   mQueueLen.reinit(mNumInputTracks);
   mResample.reinit(mNumInputTracks);
   for(size_t i=0; i<mNumInputTracks; i++) {
      double factor = (mRate / mInputTrack[i].GetTrack()->GetRate());
      double minFactor, maxFactor;
      minFactor = maxFactor = factor;

      mResample[i] = std::make_unique<Resample>(mHighQuality, minFactor, maxFactor);
      mQueueStart[i] = 0;
      mQueueLen[i] = 0;
   }

   const auto envLen = std::max(mQueueMaxLen, mInterleavedBufferSize);
   mEnvValues.reinit(envLen);
}

Mixer::~Mixer()
{
}

void Mixer::ApplyTrackGains(bool apply)
{
   mApplyTrackGains = apply;
}

void Mixer::Clear()
{
   for (unsigned int c = 0; c < mNumBuffers; c++) {
      memset(mTemp[c].ptr(), 0, mInterleavedBufferSize * SAMPLE_SIZE(floatSample));
   }
}

void MixBuffers(unsigned numChannels, int *channelFlags, float *gains,
                samplePtr src, SampleBuffer *dests,
                int len, bool interleaved)
{
   for (unsigned int c = 0; c < numChannels; c++) {
      if (!channelFlags[c])
         continue;

      samplePtr destPtr;
      unsigned skip;

      if (interleaved) {
         destPtr = dests[0].ptr() + c*SAMPLE_SIZE(floatSample);
         skip = numChannels;
      } else {
         destPtr = dests[c].ptr();
         skip = 1;
      }

      float gain = gains[c];
      float *dest = (float *)destPtr;
      float *temp = (float *)src;
      for (int j = 0; j < len; j++) {
         *dest += temp[j] * gain;   // the actual mixing process
         dest += skip;
      }
   }
}

size_t Mixer::MixSameRate(int *channelFlags, WaveTrackCache &cache,
                               sampleCount *pos)
{
   const WaveTrack *const track = cache.GetTrack();
   const double t = ( *pos ).as_double() / track->GetRate();
   const double trackEndTime = track->GetEndTime();
   const double trackStartTime = track->GetStartTime();
   const bool backwards = (mT1 < mT0);
   const double tEnd = backwards
      ? std::max(trackStartTime, mT1)
      : std::min(trackEndTime, mT1);

   //don't process if we're at the end of the selection or track.
   if ((backwards ? t <= tEnd : t >= tEnd))
      return 0;
   //if we're about to approach the end of the track or selection, figure out how much we need to grab
   auto slen = limitSampleBufferSize(
      mMaxOut,
      // PRL: maybe t and tEnd should be given as sampleCount instead to
      // avoid trouble subtracting one large value from another for a small
      // difference
      sampleCount{ (backwards ? t - tEnd : tEnd - t) * track->GetRate() + 0.5 }
   );

   if (backwards) { // Is it even possible to play something backwards anymore? Consider removing this branch.
      auto results = cache.Get(floatSample, *pos - (slen - 1), slen, mMayThrow);
      if (results)
         memcpy(mFloatBuffer.get(), results, sizeof(float) * slen);
      else
         memset(mFloatBuffer.get(), 0, sizeof(float) * slen);
      ReverseSamples((samplePtr)mFloatBuffer.get(), floatSample, 0, slen);

      *pos -= slen;
   }
   else { // forwards (probably the only case)
      auto results = cache.Get(floatSample, *pos, slen, mMayThrow);
      if (results)
         memcpy(mFloatBuffer.get(), results, sizeof(float) * slen);
      else
         memset(mFloatBuffer.get(), 0, sizeof(float) * slen);
      *pos += slen;
   }

   for(size_t c=0; c<mNumChannels; c++)
      if (mApplyTrackGains)
         mGains[c] = track->GetChannelGain(c);
      else
         mGains[c] = 1.0;

   MixBuffers(mNumChannels, channelFlags, mGains.get(),
              (samplePtr)mFloatBuffer.get(), mTemp.get(), slen, mInterleaved);

   return slen;
}

size_t Mixer::Process(size_t maxToProcess)
{
   // MB: this is wrong! mT represented warped time, and mTime is too inaccurate to use
   // it here. It's also unnecessary I think.
   //if (mT >= mT1)
   //   return 0;

   decltype(Process(0)) maxOut = 0;
   ArrayOf<int> channelFlags{ mNumChannels };

   mMaxOut = maxToProcess;

   Clear();
   for(size_t i=0; i<mNumInputTracks; i++) {
      const WaveTrack *const track = mInputTrack[i].GetTrack();
      for(size_t j=0; j<mNumChannels; j++)
         channelFlags[j] = 0;

      if( mMixerSpec ) {
         //ignore left and right when downmixing is not required
         for(size_t j = 0; j < mNumChannels; j++ )
            channelFlags[ j ] = mMixerSpec->mMap[ i ][ j ] ? 1 : 0;
      }
      else {
         switch(track->GetChannel()) {
         case Track::MonoChannel:
         default:
            for(size_t j=0; j<mNumChannels; j++)
               channelFlags[j] = 1;
            break;
         case Track::LeftChannel:
            channelFlags[0] = 1;
            break;
         case Track::RightChannel:
            if (mNumChannels >= 2)
               channelFlags[1] = 1;
            else
               channelFlags[0] = 1;
            break;
         }
      
	  }
      maxOut = std::max(maxOut,
            MixSameRate(channelFlags.get(), mInputTrack[i], &mSamplePos[i]));

      double t = mSamplePos[i].as_double() / (double)track->GetRate();
      if (mT0 > mT1)
         // backwards (as possibly in scrubbing)
         mTime = std::max(std::min(t, mTime), mT1);
      else
         // forwards (the usual)
         mTime = std::min(std::max(t, mTime), mT1);
   }
   if(mInterleaved) {
      for(size_t c=0; c<mNumChannels; c++) {
         CopySamples(mTemp[0].ptr() + (c * SAMPLE_SIZE(floatSample)),
            floatSample,
            mBuffer[0].ptr() + (c * SAMPLE_SIZE(mFormat)),
            mFormat,
            maxOut,
            mHighQuality,
            mNumChannels,
            mNumChannels);
      }
   }
   else {
      for(size_t c=0; c<mNumBuffers; c++) {
         CopySamples(mTemp[c].ptr(),
            floatSample,
            mBuffer[c].ptr(),
            mFormat,
            maxOut,
            mHighQuality);
      }
   }
   // MB: this doesn't take warping into account, replaced with code based on mSamplePos
   //mT += (maxOut / mRate);

   return maxOut;
}

samplePtr Mixer::GetBuffer()
{
   return mBuffer[0].ptr();
}

samplePtr Mixer::GetBuffer(int channel)
{
   return mBuffer[channel].ptr();
}

void Mixer::Restart()
{
   mTime = mT0;

   for(size_t i=0; i<mNumInputTracks; i++)
      mSamplePos[i] = mInputTrack[i].GetTrack()->TimeToLongSamples(mT0);

   for(size_t i=0; i<mNumInputTracks; i++) {
      mQueueStart[i] = 0;
      mQueueLen[i] = 0;
   }
}

void Mixer::Reposition(double t)
{
   mTime = t;
   const bool backwards = (mT1 < mT0);
   if (backwards)
      mTime = std::max(mT1, (std::min(mT0, mTime)));
   else
      mTime = std::max(mT0, (std::min(mT1, mTime)));

   for(size_t i=0; i<mNumInputTracks; i++) {
      mSamplePos[i] = mInputTrack[i].GetTrack()->TimeToLongSamples(mTime);
      mQueueStart[i] = 0;
      mQueueLen[i] = 0;
   }
}

void Mixer::SetTimesAndSpeed(double t0, double t1, double speed)
{
   wxASSERT(std::isfinite(speed));
   mT0 = t0;
   mT1 = t1;
   mSpeed = fabs(speed);
   Reposition(t0);
}

MixerSpec::MixerSpec( unsigned numTracks, unsigned maxNumChannels )
{
   mNumTracks = mNumChannels = numTracks;
   mMaxNumChannels = maxNumChannels;

   if( mNumChannels > mMaxNumChannels )
         mNumChannels = mMaxNumChannels;

   Alloc();

   for( unsigned int i = 0; i < mNumTracks; i++ )
      for( unsigned int j = 0; j < mNumChannels; j++ )
         mMap[ i ][ j ] = ( i == j );
}

MixerSpec::MixerSpec( const MixerSpec &mixerSpec )
{
   mNumTracks = mixerSpec.mNumTracks;
   mMaxNumChannels = mixerSpec.mMaxNumChannels;
   mNumChannels = mixerSpec.mNumChannels;

   Alloc();

   for( unsigned int i = 0; i < mNumTracks; i++ )
      for( unsigned int j = 0; j < mNumChannels; j++ )
         mMap[ i ][ j ] = mixerSpec.mMap[ i ][ j ];
}

void MixerSpec::Alloc()
{
   mMap.reinit(mNumTracks, mMaxNumChannels);
}

MixerSpec::~MixerSpec()
{
}

bool MixerSpec::SetNumChannels( unsigned newNumChannels )
{
   if( mNumChannels == newNumChannels )
      return true;

   if( newNumChannels > mMaxNumChannels )
      return false;

   for( unsigned int i = 0; i < mNumTracks; i++ )
   {
      for( unsigned int j = newNumChannels; j < mNumChannels; j++ )
         mMap[ i ][ j ] = false;

      for( unsigned int j = mNumChannels; j < newNumChannels; j++ )
         mMap[ i ][ j ] = false;
   }

   mNumChannels = newNumChannels;
   return true;
}

MixerSpec& MixerSpec::operator=( const MixerSpec &mixerSpec )
{
   mNumTracks = mixerSpec.mNumTracks;
   mNumChannels = mixerSpec.mNumChannels;
   mMaxNumChannels = mixerSpec.mMaxNumChannels;

   Alloc();

   for( unsigned int i = 0; i < mNumTracks; i++ )
      for( unsigned int j = 0; j < mNumChannels; j++ )
         mMap[ i ][ j ] = mixerSpec.mMap[ i ][ j ];

   return *this;
}

