/**********************************************************************

  Audacity: A Digital Audio Editor

  SampleFormat.h

  Dominic Mazzoni

**********************************************************************/

#ifndef __AUDACITY_SAMPLE_FORMAT__
#define __AUDACITY_SAMPLE_FORMAT__

#include "Audacity.h"
#include "MemoryX.h"
#include <wx/defs.h>

#include "audacity/Types.h"

//
// Definitions / Meta-Information
//

// Used to determine how to fill in empty areas of audio.
typedef enum {
   fillZero = 0,
   fillTwo = 2
}fillFormat;

/** \brief Return the size on disk of one uncompressed sample (bytes) */
#define SAMPLE_SIZE_DISK(SampleFormat) (((SampleFormat) == int24Sample) ? \
   size_t{ 3 } : SAMPLE_SIZE(SampleFormat) )

const wxChar *GetSampleFormatStr(sampleFormat format);

//
// Allocating/Freeing Samples
//

class SampleBuffer {

public:
   SampleBuffer()
      : mPtr(0)
   {}
   SampleBuffer(size_t count, sampleFormat format)
      : mPtr((samplePtr)malloc(count * SAMPLE_SIZE(format)))
   {}
   ~SampleBuffer()
   {
      Free();
   }

   // WARNING!  May not preserve contents.
   SampleBuffer &Allocate(size_t count, sampleFormat format)
   {
      Free();
      mPtr = (samplePtr)malloc(count * SAMPLE_SIZE(format));
      return *this;
   }


   void Free()
   {
      free(mPtr);
      mPtr = 0;
   }

   samplePtr ptr() const { return mPtr; }


private:
   samplePtr mPtr;
};

class GrowableSampleBuffer : private SampleBuffer
{
public:
   GrowableSampleBuffer()
      : SampleBuffer()
      , mCount(0)
   {}

   GrowableSampleBuffer(size_t count, sampleFormat format)
      : SampleBuffer(count, format)
      , mCount(count)
   {}

   GrowableSampleBuffer &Resize(size_t count, sampleFormat format)
   {
      if (!ptr() || mCount < count) {
         Allocate(count, format);
         mCount = count;
      }
      return *this;
   }

   void Free()
   {
      SampleBuffer::Free();
      mCount = 0;
   }

   using SampleBuffer::ptr;

private:
   size_t mCount;
};

//
// Copying, Converting and Clearing Samples
//

void      CopySamples(samplePtr src, sampleFormat srcFormat,
                      samplePtr dst, sampleFormat dstFormat,
                      unsigned int len, bool highQuality=true,
                      unsigned int srcStride=1,
                      unsigned int dstStride=1);

void      CopySamplesNoDither(samplePtr src, sampleFormat srcFormat,
                      samplePtr dst, sampleFormat dstFormat,
                      unsigned int len,
                      unsigned int srcStride=1,
                      unsigned int dstStride=1);

void      ClearSamples(samplePtr buffer, sampleFormat format,
                       size_t start, size_t len);

//
// This must be called on startup and everytime NEW ditherers
// are set in preferences.
//

void      InitDitherers();

// These are so commonly done for processing samples in floating point form in memory,
// let's have abbeviations.
using Floats = ArrayOf<float>;
using FloatBuffers = ArraysOf<float>;
using Doubles = ArrayOf<double>;

#endif
