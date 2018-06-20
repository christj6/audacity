/**********************************************************************

  Audacity: A Digital Audio Editor

  Effect.h

  Dominic Mazzoni
  Vaughan Johnson

**********************************************************************/

#ifndef __AUDACITY_EFFECT__
#define __AUDACITY_EFFECT__

#include "../Audacity.h"
#include "../MemoryX.h"
#include <set>

#include "../MemoryX.h"
#include <wx/bmpbuttn.h>
#include <wx/defs.h>
#include <wx/intl.h>
#include <wx/string.h>
#include <wx/tglbtn.h>
#include <wx/event.h> // for idle event.

class wxCheckBox;
class wxChoice;
class wxListBox;
class wxWindow;

#include "audacity/EffectInterface.h"

#include "../Experimental.h"
#include "../SampleFormat.h"
#include "../SelectedRegion.h"
#include "../Shuttle.h"
#include "../Internat.h"
#include "../widgets/ProgressDialog.h"

#include "../Track.h"

class ShuttleGui;
class AudacityCommand;

class AudacityProject;
class SelectedRegion;
class Track;
class TrackList;
class TrackFactory;
class WaveTrack;

// FIXME:
// FIXME:  Remove this once all effects are using the NEW dialog
// FIXME:

#define ID_EFFECT_PREVIEW ePreviewID

#endif
