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

// LLL, 09 Nov 2013:
// Allow all WASAPI devices, not just loopback
#define EXPERIMENTAL_FULL_WASAPI

// JKC (effect by Norm C, 02 Oct 2013)
#define EXPERIMENTAL_SCIENCE_FILTERS

// LLL, 01 Oct 2013:
// NEW key assignment view for preferences
#define EXPERIMENTAL_KEY_VIEW

// These CFG macros allow easy distinction between Audacity and DA defaults.
#define CFG_A( x ) x
#define CFG_DA( x ) 

// Define this so that sync-lock tiles shine through spectrogram.
// The spectrogram pastes a bitmap over the tiles.
// This makes it use alpha blending, most transparent where least intense.
#define EXPERIMENTAL_SPECTROGRAM_OVERLAY

// Define this so that sync-lock tiles shine through note/MIDI track.
// The note track then relies on the same code for drawing background as
// Wavetrack, and draws its notes and lines over the top.
#define EXPERIMENTAL_NOTETRACK_OVERLAY

// Define this, and the option to zoom to half wave is added in the VZoom menu.
// Also we go to half wave on collapse, full wave on restore.
#define EXPERIMENTAL_HALF_WAVE

// EXPERIMENTAL_THEMING is mostly mainstream now.
// the define is still present to mark out old code before theming, that we might
// conceivably need.
// TODO: Agree on and then tidy this code.
#define EXPERIMENTAL_THEMING

// This shows the zoom toggle button on the edit toolbar.
#define EXPERIMENTAL_ZOOM_TOGGLE_BUTTON

// Effect categorisation. Adds support for arranging effects in categories
// and displaying those categories as submenus in the Effect menu.
// This was a 2008 GSoC project that was making good progress at the half-way point
// but then the student didn't contribute after that.  It needs a bit of work to finish it off.
// As a minimum, if this is turned on for a release,
// it should have an easy mechanism to disable it at run-time, such as a menu item or a pref,
// preferrably disabled until other work is done.  Martyn 22/12/2008.
// 

// JKC Apr 2015, Menu item to manage effects.
#define EXPERIMENTAL_EFFECT_MANAGEMENT

#ifdef EXPERIMENTAL_NYQUIST_INSPECTOR
   #include "NyquistAdapter.h"
#endif

// Module prefs provides a panel in prefs where users can choose which modules
// to enable.
#define EXPERIMENTAL_MODULE_PREFS

// Define to allow realtime processing in Audacity effects that have been converted.
#define EXPERIMENTAL_REALTIME_AUDACITY_EFFECTS

// Define for NEW noise reduction effect from Paul Licameli.
#define EXPERIMENTAL_NOISE_REDUCTION

// Define to enable Nyquist audio clip boundary control (Steve Daulton Dec 2014)
#define EXPERIMENTAL_NYQUIST_SPLIT_CONTROL

// Paul Licameli (PRL) 24 May 2015
// Allow scrolling up to one half of a screenful beyond either end of the project,
// if you turn on the appropriate Tracks preference.
// This allows smooth-scrolling scrub to work more reasonably at the ends.
#define EXPERIMENTAL_SCROLLING_LIMITS

// Paul Licameli (PRL) 28 May 2015
// Draw negative numbers on the time ruler in a different color, when
// scrolling past zero is enabled. Perhaps that lessens confusion.
#define EXPERIMENTAL_TWO_TONE_TIME_RULER

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
