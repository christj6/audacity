/**********************************************************************

  Audacity: A Digital Audio Editor

  Effect.cpp

  Dominic Mazzoni
  Vaughan Johnson
  Martyn Shaw

*******************************************************************//**

\class Effect
\brief Base class for many of the effects in Audacity.

*//*******************************************************************/

#include "../Audacity.h"
#include "Effect.h"

#include <algorithm>

#include <wx/defs.h>
#include <wx/hashmap.h>
#include <wx/sizer.h>
#include <wx/stockitem.h>
#include <wx/string.h>
#include <wx/tglbtn.h>
#include <wx/timer.h>
#include <wx/utils.h>
#include <wx/log.h>

#include "../AudacityException.h"
#include "../AudioIO.h"
#include "../Mix.h"
#include "../Prefs.h"
#include "../Project.h"
#include "../ShuttleGui.h"
#include "../Shuttle.h"
#include "../WaveTrack.h"
#include "../toolbars/ControlToolBar.h"
#include "../widgets/AButton.h"
#include "../widgets/ProgressDialog.h"
#include "../ondemand/ODManager.h"
#include "../widgets/HelpSystem.h"
#include "../widgets/LinkingHtmlWindow.h"
#include "../widgets/ErrorDialog.h"
#include "../FileNames.h"
#include "../commands/AudacityCommand.h"
#include "../commands/CommandContext.h"

#if defined(__WXMAC__)
#include <Cocoa/Cocoa.h>
#endif

#include "../Experimental.h"

#ifndef __AUDACITY_OLD_STD__
#include <unordered_map>
#endif

static const int kDummyID = 20000;
static const int kSaveAsID = 20001;
static const int kImportID = 20002;
static const int kExportID = 20003;
static const int kDefaultsID = 20004;
static const int kOptionsID = 20005;
static const int kUserPresetsDummyID = 20006;
static const int kDeletePresetDummyID = 20007;
static const int kMenuID = 20100;
static const int kEnableID = 20101;
static const int kPlayID = 20102;
static const int kRewindID = 20103;
static const int kFFwdID = 20104;
static const int kPlaybackID = 20105;
static const int kCaptureID = 20106;
static const int kUserPresetsID = 21000;
static const int kDeletePresetID = 22000;
static const int kFactoryPresetsID = 23000;
