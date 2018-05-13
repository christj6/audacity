/**********************************************************************

  Audacity: A Digital Audio Editor

  Experimental.h

  Dominic Mazzoni
  James Crook

  Used for #includes and #defines for experimental features.

  When the features become mainstream the #include files will
  move out of here and into the files which need them.  The
  #defines will then be retired.

  JKC: This file solves a problem of how to avoid forking the
  code base when working on NEW features e.g:
    - Additional displays in Audacity
    - Modular architecture.
  Add #defines in here for the NEW features, and make your code
  conditional on those #defines.

  All the #defines are positive, i.e., when defined,
  they enable the feature.

**********************************************************************/

#ifndef __EXPERIMENTAL__
#define __EXPERIMENTAL__

// These CFG macros allow easy distinction between Audacity and DA defaults.
#define CFG_A( x ) x
#define CFG_DA( x ) 

// Paul Licameli (PRL) 31 May 2015
// Zero-padding factor for spectrograms can smooth the display of spectrograms by
// interpolating in frequency domain.
#define EXPERIMENTAL_ZERO_PADDED_SPECTROGRAMS

// PRL 5 Jan 2018
// Easy change of keystroke bindings for menu items
#define EXPERIMENTAL_EASY_CHANGE_KEY_BINDINGS

// PRL 17 Mar 2018
// Hoping to commit to use of this branch before 2.3.0 is out.
// Don't use our own RingBuffer class, but reuse PortAudio's which includes
// proper memory fences.
#undef EXPERIMENTAL_REWRITE_RING_BUFFER

#endif
