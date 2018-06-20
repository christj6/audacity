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

#define BUILTIN_EFFECT_PREFIX wxT("Built-in Effect: ")

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

// Helper macros for defining, reading and verifying effect parameters

#define Param(name, type, key, def, min, max, scale) \
   static const wxChar * KEY_ ## name = (key); \
   static const type DEF_ ## name = (def); \
   static const type MIN_ ## name = (min); \
   static const type MAX_ ## name = (max); \
   static const type SCL_ ## name = (scale);

#define PBasic(name, type, key, def) \
   static const wxChar * KEY_ ## name = (key); \
   static const type DEF_ ## name = (def);

#define PRange(name, type, key, def, min, max) \
   PBasic(name, type, key, def); \
   static const type MIN_ ## name = (min); \
   static const type MAX_ ## name = (max);

#define PScale(name, type, key, def, min, max, scale) \
   PRange(name, type, key, def, min, max); \
   static const type SCL_ ## name = (scale);

#define ReadParam(type, name) \
   type name = DEF_ ## name; \
   if (!parms.ReadAndVerify(KEY_ ## name, &name, DEF_ ## name, MIN_ ## name, MAX_ ## name)) \
      return false;

#define ReadBasic(type, name) \
   type name; \
   wxUnusedVar(MIN_ ##name); \
   wxUnusedVar(MAX_ ##name); \
   wxUnusedVar(SCL_ ##name); \
   if (!parms.ReadAndVerify(KEY_ ## name, &name, DEF_ ## name)) \
      return false;

#define ReadAndVerifyEnum(name, list) \
   int name; \
   if (!parms.ReadAndVerify(KEY_ ## name, &name, DEF_ ## name, list)) \
      return false;

#define ReadAndVerifyInt(name) ReadParam(int, name)
#define ReadAndVerifyDouble(name) ReadParam(double, name)
#define ReadAndVerifyFloat(name) ReadParam(float, name)
#define ReadAndVerifyBool(name) ReadBasic(bool, name)
#define ReadAndVerifyString(name) ReadBasic(wxString, name)

#endif
