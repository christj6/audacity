/*
*     Program: REALFFTF.C
*      Author: Philip Van Baren
*        Date: 2 September 1993
*
* Description: These routines perform an FFT on real data to get a conjugate-symmetric
*              output, and an inverse FFT on conjugate-symmetric input to get a real
*              output sequence.
*
*              This code is for floating point data.
*
*              Modified 8/19/1998 by Philip Van Baren
*                 - made the InitializeFFT and EndFFT routines take a structure
*                   holding the length and pointers to the BitReversed and SinTable
*                   tables.
*              Modified 5/23/2009 by Philip Van Baren
*                 - Added GetFFT and ReleaseFFT routines to retain common SinTable
*                   and BitReversed tables so they don't need to be reallocated
*                   and recomputed on every call.
*                 - Added Reorder* functions to undo the bit-reversal
*
*  Copyright (C) 2009  Philip VanBaren
*
*  This program is free software; you can redistribute it and/or modify
*  it under the terms of the GNU General Public License as published by
*  the Free Software Foundation; either version 2 of the License, or
*  (at your option) any later version.
*
*  This program is distributed in the hope that it will be useful,
*  but WITHOUT ANY WARRANTY; without even the implied warranty of
*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*  GNU General Public License for more details.
*
*  You should have received a copy of the GNU General Public License
*  along with this program; if not, write to the Free Software
*  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#include "Audacity.h"
#include <vector>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include "Experimental.h"

#include <wx/thread.h>

#include "RealFFTf.h"

enum : size_t { MAX_HFFT = 10 };

// Maintain a pool:
static std::vector< movable_ptr<FFTParam> > hFFTArray(MAX_HFFT);
wxCriticalSection getFFTMutex;

/* Release a previously requested handle to the FFT tables */
void FFTDeleter::operator() (FFTParam *hFFT) const
{
   wxCriticalSectionLocker locker{ getFFTMutex };

   auto it = hFFTArray.begin(), end = hFFTArray.end();
   while (it != end && it->get() != hFFT)
      ++it;
   if ( it != end )
      ;
   else
      delete hFFT;
}
