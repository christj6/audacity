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