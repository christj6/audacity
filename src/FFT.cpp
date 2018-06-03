/**********************************************************************

  FFT.cpp

  Dominic Mazzoni

  September 2000

*******************************************************************//*!

\file FFT.cpp
\brief Fast Fourier Transform routines.

  This file contains a few FFT routines, including a real-FFT
  routine that is almost twice as fast as a normal complex FFT,
  and a power spectrum routine when you know you don't care
  about phase information.

  Some of this code was based on a free implementation of an FFT
  by Don Cross, available on the web at:

    http://www.intersrv.com/~dcross/fft.html

  The basic algorithm for his code was based on Numerican Recipes
  in Fortran.  I optimized his code further by reducing array
  accesses, caching the bit reversal table, and eliminating
  float-to-double conversions, and I added the routines to
  calculate a real FFT and a real power spectrum.

*//*******************************************************************/
/*
  Salvo Ventura - November 2006
  Added more window functions:
    * 4: Blackman
    * 5: Blackman-Harris
    * 6: Welch
    * 7: Gaussian(a=2.5)
    * 8: Gaussian(a=3.5)
    * 9: Gaussian(a=4.5)
*/

#include "Audacity.h"
#include "Internat.h"

#include "FFT.h"
#include "MemoryX.h"
#include "SampleFormat.h"

#include <wx/intl.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

#include "RealFFTf.h"
#include "Experimental.h"

static ArraysOf<int> gFFTBitTable;
static const size_t MaxFastBits = 16;

/* Declare Static functions */
static void InitFFT();

static bool IsPowerOfTwo(size_t x)
{
   if (x < 2)
      return false;

   if (x & (x - 1))             /* Thanks to 'byang' for this cute trick! */
      return false;

   return true;
}

static size_t NumberOfBitsNeeded(size_t PowerOfTwo)
{
   if (PowerOfTwo < 2) {
      wxFprintf(stderr, "Error: FFT called with size %ld\n", PowerOfTwo);
      exit(1);
   }

   size_t i = 0;
   while (PowerOfTwo > 1)
      PowerOfTwo >>= 1, ++i;

   return i;
}

int ReverseBits(size_t index, size_t NumBits)
{
   size_t i, rev;

   for (i = rev = 0; i < NumBits; i++) {
      rev = (rev << 1) | (index & 1);
      index >>= 1;
   }

   return rev;
}

void InitFFT()
{
   gFFTBitTable.reinit(MaxFastBits);

   size_t len = 2;
   for (size_t b = 1; b <= MaxFastBits; b++) {
      auto &array = gFFTBitTable[b - 1];
      array.reinit(len);
      for (size_t i = 0; i < len; i++)
         array[i] = ReverseBits(i, b);

      len <<= 1;
   }
}

void DeinitFFT()
{
   gFFTBitTable.reset();
}

static inline size_t FastReverseBits(size_t i, size_t NumBits)
{
   if (NumBits <= MaxFastBits)
      return gFFTBitTable[NumBits - 1][i];
   else
      return ReverseBits(i, NumBits);
}

/*
 * Complex Fast Fourier Transform
 */

void FFT(size_t NumSamples,
         bool InverseTransform,
         const float *RealIn, const float *ImagIn,
	 float *RealOut, float *ImagOut)
{
   double angle_numerator = 2.0 * M_PI;
   double tr, ti;                /* temp real, temp imaginary */

   if (!IsPowerOfTwo(NumSamples)) {
      wxFprintf(stderr, "%ld is not a power of two\n", NumSamples);
      exit(1);
   }

   if (!gFFTBitTable)
      InitFFT();

   if (!InverseTransform)
      angle_numerator = -angle_numerator;

   /* Number of bits needed to store indices */
   auto NumBits = NumberOfBitsNeeded(NumSamples);

   /*
    **   Do simultaneous data copy and bit-reversal ordering into outputs...
    */

   for (size_t i = 0; i < NumSamples; i++) {
      auto j = FastReverseBits(i, NumBits);
      RealOut[j] = RealIn[i];
      ImagOut[j] = (ImagIn == NULL) ? 0.0 : ImagIn[i];
   }

   /*
    **   Do the FFT itself...
    */

   size_t BlockEnd = 1;
   for (size_t BlockSize = 2; BlockSize <= NumSamples; BlockSize <<= 1) {

      double delta_angle = angle_numerator / (double) BlockSize;

      double sm2 = sin(-2 * delta_angle);
      double sm1 = sin(-delta_angle);
      double cm2 = cos(-2 * delta_angle);
      double cm1 = cos(-delta_angle);
      double w = 2 * cm1;
      double ar0, ar1, ar2, ai0, ai1, ai2;

      for (size_t i = 0; i < NumSamples; i += BlockSize) {
         ar2 = cm2;
         ar1 = cm1;

         ai2 = sm2;
         ai1 = sm1;

         for (size_t j = i, n = 0; n < BlockEnd; j++, n++) {
            ar0 = w * ar1 - ar2;
            ar2 = ar1;
            ar1 = ar0;

            ai0 = w * ai1 - ai2;
            ai2 = ai1;
            ai1 = ai0;

            size_t k = j + BlockEnd;
            tr = ar0 * RealOut[k] - ai0 * ImagOut[k];
            ti = ar0 * ImagOut[k] + ai0 * RealOut[k];

            RealOut[k] = RealOut[j] - tr;
            ImagOut[k] = ImagOut[j] - ti;

            RealOut[j] += tr;
            ImagOut[j] += ti;
         }
      }

      BlockEnd = BlockSize;
   }

   /*
      **   Need to normalize if inverse transform...
    */

   if (InverseTransform) {
      float denom = (float) NumSamples;

      for (size_t i = 0; i < NumSamples; i++) {
         RealOut[i] /= denom;
         ImagOut[i] /= denom;
      }
   }
}
