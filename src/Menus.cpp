/**********************************************************************

  Audacity: A Digital Audio Editor

  Menus.cpp

  Dominic Mazzoni
  Brian Gunlogson
  et al.

*******************************************************************//**

\file Menus.cpp
\brief All AudacityProject functions that provide the menus.
Implements AudacityProjectCommandFunctor.

  This file implements the method that creates the menu bar, plus
  all of the methods that get called when you select an item
  from a menu.

  All of the menu bar handling is part of the class AudacityProject,
  but the event handlers for all of the menu items have been moved
  to Menus.h and Menus.cpp for clarity.

*//****************************************************************//**

\class AudacityProjectCommandFunctor
\brief AudacityProjectCommandFunctor, derived from CommandFunctor,
simplifies construction of menu items.

*//*******************************************************************/

#include "Audacity.h"
#include "Project.h"

#include <cfloat>
#include <iterator>
#include <algorithm>
#include <limits>
#include <math.h>


#include <wx/defs.h>
#include <wx/docview.h>
#include <wx/filedlg.h>
#include <wx/textfile.h>
#include <wx/textdlg.h>
#include <wx/progdlg.h>
#include <wx/scrolbar.h>
#include <wx/ffile.h>
#include <wx/statusbr.h>
#include <wx/utils.h>

#include "TrackPanel.h"

#include "effects/EffectManager.h"

#include "AudacityApp.h"
#include "AudacityLogger.h"
#include "AudioIO.h"
#include "Dependencies.h"
#include "float_cast.h"

#include "import/ImportRaw.h"
#include "export/Export.h"
#include "export/ExportMultiple.h"
#include "prefs/PrefsDialog.h"
#include "prefs/PlaybackPrefs.h"
#include "ShuttleGui.h"
#include "HistoryWindow.h"
#include "Internat.h"
#include "FileFormats.h"
#include "ModuleManager.h"
#include "PluginManager.h"
#include "Prefs.h"
#include "Tags.h"
#include "Mix.h"
#include "AboutDialog.h"
#include "Benchmark.h"
#include "ondemand/ODManager.h"

#include "BatchProcessDialog.h"
#include "BatchCommands.h"
#include "prefs/BatchPrefs.h"

#include "toolbars/ToolManager.h"
#include "toolbars/ControlToolBar.h"
#include "toolbars/EditToolBar.h"
#include "toolbars/DeviceToolBar.h"

#include "tracks/ui/SelectHandle.h"

#include "widgets/LinkingHtmlWindow.h"

#include "Experimental.h"
#include "PlatformCompatibility.h"
#include "FileNames.h"
#include "TimeDialog.h"
#include "SoundActivatedRecord.h"

#include "SplashDialog.h"
#include "widgets/HelpSystem.h"
#include "DeviceManager.h"

#include "UndoManager.h"
#include "WaveTrack.h"

#include "prefs/TracksPrefs.h"

#include "widgets/Meter.h"
#include "widgets/ErrorDialog.h"
#include "./commands/AudacityCommand.h"
#include "commands/CommandContext.h"

enum {
   kAlignStartZero = 0,
   kAlignStartSelStart,
   kAlignStartSelEnd,
   kAlignEndSelStart,
   kAlignEndSelEnd,
   // The next two are only in one subMenu, so more easily handled at the end.
   kAlignEndToEnd,
   kAlignTogether
};

#include "commands/CommandContext.h"

#include "BatchCommands.h"

/// CreateMenusAndCommands builds the menus, and also rebuilds them after
/// changes in configured preferences - for example changes in key-bindings
/// affect the short-cut key legend that appears beside each command,

// To supply the "finder" argument in AddItem calls
static CommandHandlerObject &ident(AudacityProject &project) { return project; }

#define FN(X) ident, static_cast<CommandFunctorPointer>(& AudacityProject :: X)
#define XXO(X) _(X), wxString{X}.Contains("...")

void AudacityProject::CreateMenusAndCommands()
{
   CommandManager *c = &mCommandManager;
   wxArrayString names;
   std::vector<int> indices;

   // The list of defaults to exclude depends on
   // preference wxT("/GUI/Shortcuts/FullDefaults"), which may have changed.
   c->SetMaxList();

   {
      auto menubar = c->AddMenuBar(wxT("appmenu"));
      wxASSERT(menubar);
      c->SetOccultCommands( false );

      /////////////////////////////////////////////////////////////////////////////
      // File menu
      /////////////////////////////////////////////////////////////////////////////

      c->BeginMenu(_("&File"));
      c->SetDefaultFlags(AudioIONotBusyFlag, AudioIONotBusyFlag);

      /*i18n-hint: "New" is an action (verb) to create a NEW project*/
      c->AddItem(wxT("New"), XXO("&New"), FN(OnNew), wxT("Ctrl+N"),
         AudioIONotBusyFlag,
         AudioIONotBusyFlag);

      /*i18n-hint: (verb)*/
      c->AddItem(wxT("Open"), XXO("&Open..."), FN(OnOpen), wxT("Ctrl+O"),
         AudioIONotBusyFlag,
         AudioIONotBusyFlag);

      /////////////////////////////////////////////////////////////////////////////

      CreateRecentFilesMenu(c);

      /////////////////////////////////////////////////////////////////////////////

      c->AddSeparator();

      c->AddItem(wxT("Close"), XXO("&Close"), FN(OnClose), wxT("Ctrl+W"));

      c->AddItem(wxT("Save"), XXO("&Save Project"), FN(OnSave), wxT("Ctrl+S"),
         AudioIONotBusyFlag | UnsavedChangesFlag,
         AudioIONotBusyFlag | UnsavedChangesFlag);
      c->AddItem(wxT("SaveAs"), XXO("Save Project &As..."), FN(OnSaveAs));

      c->BeginSubMenu( _("&Export") );

      // Enable Export audio commands only when there are audio tracks.
      c->AddItem(wxT("ExportMp3"), XXO("Export as MP&3"), FN(OnExportMp3), wxT(""),
         AudioIONotBusyFlag | WaveTracksExistFlag,
         AudioIONotBusyFlag | WaveTracksExistFlag);

      c->AddItem(wxT("ExportWav"), XXO("Export as &WAV"), FN(OnExportWav), wxT(""),
         AudioIONotBusyFlag | WaveTracksExistFlag,
         AudioIONotBusyFlag | WaveTracksExistFlag);

      c->AddItem(wxT("ExportOgg"), XXO("Export as &OGG"), FN(OnExportOgg), wxT(""),
         AudioIONotBusyFlag | WaveTracksExistFlag,
         AudioIONotBusyFlag | WaveTracksExistFlag);

      c->AddItem(wxT("Export"), XXO("&Export Audio..."), FN(OnExportAudio), wxT("Ctrl+Shift+E"),
         AudioIONotBusyFlag | WaveTracksExistFlag,
         AudioIONotBusyFlag | WaveTracksExistFlag);

      // Enable Export Selection commands only when there's a selection.
      c->AddItem(wxT("ExportSel"), XXO("Expo&rt Selected Audio..."), FN(OnExportSelection),
         AudioIONotBusyFlag | TimeSelectedFlag | WaveTracksSelectedFlag,
         AudioIONotBusyFlag | TimeSelectedFlag | WaveTracksSelectedFlag);

      // Enable Export audio commands only when there are audio tracks.
      c->AddItem(wxT("ExportMultiple"), XXO("Export &Multiple..."), FN(OnExportMultiple), wxT("Ctrl+Shift+L"),
         AudioIONotBusyFlag | WaveTracksExistFlag,
         AudioIONotBusyFlag | WaveTracksExistFlag);
      c->EndSubMenu();
      c->AddSeparator();
      c->BeginSubMenu(_("&Import"));

      c->AddItem(wxT("ImportAudio"), XXO("&Audio..."), FN(OnImport), wxT("Ctrl+Shift+I"));
      c->AddItem(wxT("ImportRaw"), XXO("&Raw Data..."), FN(OnImportRaw));

      c->EndSubMenu();
      c->AddSeparator();

      // On the Mac, the Exit item doesn't actually go here...wxMac will pull it out
      // and put it in the Audacity menu for us based on its ID.
      /* i18n-hint: (verb) It's item on a menu. */
      c->AddItem(wxT("Exit"), XXO("E&xit"), FN(OnExit), wxT("Ctrl+Q"),
         AlwaysEnabledFlag,
         AlwaysEnabledFlag);

      c->EndMenu();

      /////////////////////////////////////////////////////////////////////////////
      // Edit Menu
      /////////////////////////////////////////////////////////////////////////////

      c->BeginMenu(_("&Edit"));

      c->SetDefaultFlags(AudioIONotBusyFlag | TimeSelectedFlag | TracksSelectedFlag,
         AudioIONotBusyFlag | TimeSelectedFlag | TracksSelectedFlag);

      c->AddItem(wxT("Undo"), XXO("&Undo"), FN(OnUndo), wxT("Ctrl+Z"),
         AudioIONotBusyFlag | UndoAvailableFlag,
         AudioIONotBusyFlag | UndoAvailableFlag);

      // The default shortcut key for Redo is different on different platforms.
      wxString key =
#ifdef __WXMSW__
         wxT("Ctrl+Y");
#else
         wxT("Ctrl+Shift+Z");
#endif

      c->AddItem(wxT("Redo"), XXO("&Redo"), FN(OnRedo), key,
         AudioIONotBusyFlag | RedoAvailableFlag,
         AudioIONotBusyFlag | RedoAvailableFlag);

      ModifyUndoMenuItems();

      c->AddSeparator();

      // Basic Edit coomands
      /* i18n-hint: (verb)*/
      c->AddItem(wxT("Cut"), XXO("Cu&t"), FN(OnCut), wxT("Ctrl+X"),
         AudioIONotBusyFlag | CutCopyAvailableFlag | NoAutoSelect,
         AudioIONotBusyFlag | CutCopyAvailableFlag);
      c->AddItem(wxT("Delete"), XXO("&Delete"), FN(OnDelete), wxT("Ctrl+K"),
         AudioIONotBusyFlag | NoAutoSelect,
         AudioIONotBusyFlag );
      /* i18n-hint: (verb)*/
      c->AddItem(wxT("Copy"), XXO("&Copy"), FN(OnCopy), wxT("Ctrl+C"),
         AudioIONotBusyFlag | CutCopyAvailableFlag,
         AudioIONotBusyFlag | CutCopyAvailableFlag);
      /* i18n-hint: (verb)*/
      c->AddItem(wxT("Paste"), XXO("&Paste"), FN(OnPaste), wxT("Ctrl+V"),
         AudioIONotBusyFlag,
         AudioIONotBusyFlag);

      c->AddSeparator();

      /////////////////////////////////////////////////////////////////////////////

#ifndef __WXMAC__
      c->AddSeparator();
#endif

      // The default shortcut key for Preferences is different on different platforms.
      key =
#ifdef __WXMAC__
         wxT("Ctrl+,");
#else
         wxT("Ctrl+P");
#endif

      c->AddItem(wxT("Preferences"), XXO("Pre&ferences..."), FN(OnPreferences), key,
         AudioIONotBusyFlag,
         AudioIONotBusyFlag);

      c->EndMenu();

      /////////////////////////////////////////////////////////////////////////////
      // Select Menu
      /////////////////////////////////////////////////////////////////////////////

      /* i18n-hint: (verb) It's an item on a menu. */
      c->BeginMenu(_("&Select"));
      c->SetDefaultFlags(TracksExistFlag, TracksExistFlag);

      c->SetLongName( _("Select All"))->AddItem(wxT("SelectAll"), XXO("&All"), FN(OnSelectAll), wxT("Ctrl+A"));
      c->SetLongName( _("Select None"))->AddItem(wxT("SelectNone"), XXO("&None"), FN(OnSelectNone), wxT("Ctrl+Shift+A"));

      /////////////////////////////////////////////////////////////////////////////

      c->SetDefaultFlags(TracksSelectedFlag, TracksSelectedFlag);

      c->BeginSubMenu(_("&Tracks"));
      c->AddItem(wxT("SelAllTracks"), XXO("In All &Tracks"), FN(OnSelectAllTracks),
         wxT("Ctrl+Shift+K"),
         TracksExistFlag, TracksExistFlag);

      c->EndSubMenu();

      c->SetDefaultFlags(TracksExistFlag, TracksExistFlag);

      /////////////////////////////////////////////////////////////////////////////

      c->BeginSubMenu(_("R&egion"));

      c->SetLongName( _("Set Selection Left at Play Position"))->AddItem(wxT("SetLeftSelection"), XXO("&Left at Playback Position"), FN(OnSetLeftSelection), wxT("["));
      c->SetLongName( _("Set Selection Right at Play Position"))->AddItem(wxT("SetRightSelection"), XXO("&Right at Playback Position"), FN(OnSetRightSelection), wxT("]"));
      c->SetDefaultFlags(TracksSelectedFlag, TracksSelectedFlag);
      c->SetLongName( _("Select Track Start to Cursor"))->AddItem(wxT("SelTrackStartToCursor"), XXO("Track &Start to Cursor"), FN(OnSelectStartCursor), wxT("Shift+J"),AlwaysEnabledFlag,AlwaysEnabledFlag);
      c->SetLongName( _("Select Cursor to Track End"))->AddItem(wxT("SelCursorToTrackEnd"), XXO("Cursor to Track &End"), FN(OnSelectCursorEnd), wxT("Shift+K"),AlwaysEnabledFlag,AlwaysEnabledFlag);
      c->SetLongName( _("Select Track Start to End"))->AddItem(wxT("SelTrackStartToEnd"), XXO("Track Start to En&d"), FN(OnSelectTrackStartToEnd), wxT(""),AlwaysEnabledFlag,AlwaysEnabledFlag);
      c->EndSubMenu();

      /////////////////////////////////////////////////////////////////////////////

      c->SetDefaultFlags(TracksSelectedFlag, TracksSelectedFlag);

      c->BeginSubMenu(_("Clip B&oundaries"));
      c->AddItem(wxT("SelPrevClipBoundaryToCursor"), XXO("Pre&vious Clip Boundary to Cursor"),
         FN(OnSelectPrevClipBoundaryToCursor), wxT(""),
         WaveTracksExistFlag, WaveTracksExistFlag);
      c->AddItem(wxT("SelCursorToNextClipBoundary"), XXO("Cursor to Ne&xt Clip Boundary"),
         FN(OnSelectCursorToNextClipBoundary), wxT(""),
         WaveTracksExistFlag, WaveTracksExistFlag);
      c->SetLongName( _("Select Previous Clip"))->AddItem(wxT("SelPrevClip"), XXO("Previo&us Clip"), FN(OnSelectPrevClip), wxT("Alt+P"),
         WaveTracksExistFlag, WaveTracksExistFlag);
      c->SetLongName( _("Select Next Clip"))->AddItem(wxT("SelNextClip"), XXO("N&ext Clip"), FN(OnSelectNextClip), wxT("Alt+N"),
         WaveTracksExistFlag, WaveTracksExistFlag);

      c->EndSubMenu();
      /////////////////////////////////////////////////////////////////////////////

      c->AddSeparator();

      c->SetLongName( _("Select Cursor to Stored"))->AddItem(wxT("SelCursorStoredCursor"), XXO("Cursor to Stored &Cursor Position"), FN(OnSelectCursorStoredCursor),
         wxT(""), TracksExistFlag, TracksExistFlag);

      c->AddItem(wxT("StoreCursorPosition"), XXO("Store Cursor Pos&ition"), FN(OnCursorPositionStore),
         WaveTracksExistFlag,
         WaveTracksExistFlag);
      // Save cursor position is used in some selections.
      // Maybe there should be a restore for it?

      c->AddSeparator();

      c->SetLongName( _("Select Zero Crossing"))->AddItem(wxT("ZeroCross"), XXO("At &Zero Crossings"), FN(OnZeroCrossing), wxT("Z"));

      c->EndMenu();

      /////////////////////////////////////////////////////////////////////////////
      // View Menu
      /////////////////////////////////////////////////////////////////////////////

      c->BeginMenu(_("&View"));
      c->SetDefaultFlags(TracksExistFlag, TracksExistFlag);
      c->BeginSubMenu(_("&Zoom"));

      c->AddItem(wxT("ZoomIn"), XXO("Zoom &In"), FN(OnZoomIn), wxT("Ctrl+1"),
         ZoomInAvailableFlag,
         ZoomInAvailableFlag);
      c->AddItem(wxT("ZoomNormal"), XXO("Zoom &Normal"), FN(OnZoomNormal), wxT("Ctrl+2"));
      c->AddItem(wxT("ZoomOut"), XXO("Zoom &Out"), FN(OnZoomOut), wxT("Ctrl+3"),
         ZoomOutAvailableFlag,
         ZoomOutAvailableFlag);
      c->AddItem(wxT("ZoomSel"), XXO("&Zoom to Selection"), FN(OnZoomSel), wxT("Ctrl+E"),
         TimeSelectedFlag,
         TimeSelectedFlag);
      c->AddItem(wxT("ZoomToggle"), XXO("Zoom &Toggle"), FN(OnZoomToggle), wxT("Shift+Z"),
         TracksExistFlag,
         TracksExistFlag);
      c->EndSubMenu();

      c->BeginSubMenu(_("T&rack Size"));
      c->AddItem(wxT("FitInWindow"), XXO("&Fit to Width"), FN(OnZoomFit), wxT("Ctrl+F"));
      c->AddItem(wxT("FitV"), XXO("Fit to &Height"), FN(OnZoomFitV), wxT("Ctrl+Shift+F"));
      c->AddItem(wxT("CollapseAllTracks"), XXO("&Collapse All Tracks"), FN(OnCollapseAllTracks), wxT("Ctrl+Shift+C"));
      c->AddItem(wxT("ExpandAllTracks"), XXO("E&xpand Collapsed Tracks"), FN(OnExpandAllTracks), wxT("Ctrl+Shift+X"));
      c->EndSubMenu();

      c->BeginSubMenu(_("Sk&ip to"));
      c->SetLongName( _("Skip to Selection Start"))->AddItem(wxT("SkipSelStart"), XXO("Selection Sta&rt"), FN(OnGoSelStart), wxT("Ctrl+["),
                 TimeSelectedFlag, TimeSelectedFlag);
      c->SetLongName( _("Skip to Selection End"))->AddItem(wxT("SkipSelEnd"), XXO("Selection En&d"), FN(OnGoSelEnd), wxT("Ctrl+]"),
                 TimeSelectedFlag, TimeSelectedFlag);
      c->EndSubMenu();

      c->AddSeparator();

      // History window should be available either for UndoAvailableFlag or RedoAvailableFlag,
      // but we can't make the AddItem flags and mask have both, because they'd both have to be true for the
      // command to be enabled.
      //    If user has Undone the entire stack, RedoAvailableFlag is on but UndoAvailableFlag is off.
      //    If user has done things but not Undone anything, RedoAvailableFlag is off but UndoAvailableFlag is on.
      // So in either of those cases, (AudioIONotBusyFlag | UndoAvailableFlag | RedoAvailableFlag) mask
      // would fail.
      // The only way to fix this in the current architecture is to hack in special cases for RedoAvailableFlag
      // in AudacityProject::UpdateMenus() (ugly) and CommandManager::HandleCommandEntry() (*really* ugly --
      // shouldn't know about particular command names and flags).
      // Here's the hack that would be necessary in AudacityProject::UpdateMenus(), if somebody decides to do it:
      //    // Because EnableUsingFlags requires all the flag bits match the corresponding mask bits,
      //    // "UndoHistory" specifies only AudioIONotBusyFlag | UndoAvailableFlag, because that
      //    // covers the majority of cases where it should be enabled.
      //    // If history is not empty but we've Undone the whole stack, we also want to enable,
      //    // to show the Redo's on stack.
      //    // "UndoHistory" might already be enabled, but add this check for RedoAvailableFlag.
      //    if (flags & RedoAvailableFlag)
      //       mCommandManager.Enable(wxT("UndoHistory"), true);
      // So for now, enable the command regardless of stack. It will just show empty sometimes.
      // FOR REDESIGN, clearly there are some limitations with the flags/mask bitmaps.

      /* i18n-hint: Clicking this menu item shows the various editing steps that have been taken.*/
      c->AddItem(wxT("UndoHistory"), XXO("&History..."), FN(OnHistory),
         AudioIONotBusyFlag,
         AudioIONotBusyFlag);

      c->AddSeparator();

      /////////////////////////////////////////////////////////////////////////////

      c->BeginSubMenu(_("&Toolbars"));

      /* i18n-hint: (verb)*/
      c->AddItem(wxT("ResetToolbars"), XXO("Reset Toolb&ars"), FN(OnResetToolBars), 0, AlwaysEnabledFlag, AlwaysEnabledFlag);
      c->AddSeparator();

      /* i18n-hint: Clicking this menu item shows the toolbar with the big buttons on it (play record etc)*/
      c->AddCheck(wxT("ShowTransportTB"), XXO("&Transport Toolbar"), FN(OnShowTransportToolBar), 0, AlwaysEnabledFlag, AlwaysEnabledFlag);
      /* i18n-hint: Clicking this menu item shows the toolbar with the recording level meters*/
      c->AddCheck(wxT("ShowRecordMeterTB"), XXO("&Recording Meter Toolbar"), FN(OnShowRecordMeterToolBar), 0, AlwaysEnabledFlag, AlwaysEnabledFlag);
      /* i18n-hint: Clicking this menu item shows the toolbar with the playback level meter*/
      c->AddCheck(wxT("ShowPlayMeterTB"), XXO("&Playback Meter Toolbar"), FN(OnShowPlayMeterToolBar), 0, AlwaysEnabledFlag, AlwaysEnabledFlag);
      /* --i18n-hint: Clicking this menu item shows the toolbar which has sound level meters*/
      //c->AddCheck(wxT("ShowMeterTB"), XXO("Co&mbined Meter Toolbar"), FN(OnShowMeterToolBar), 0, AlwaysEnabledFlag, AlwaysEnabledFlag);
      /* i18n-hint: Clicking this menu item shows the toolbar for editing*/
      c->AddCheck(wxT("ShowEditTB"), XXO("&Edit Toolbar"), FN(OnShowEditToolBar), 0, AlwaysEnabledFlag, AlwaysEnabledFlag);
      /* i18n-hint: Clicking this menu item shows the toolbar that manages devices*/
      c->AddCheck(wxT("ShowDeviceTB"), XXO("&Device Toolbar"), FN(OnShowDeviceToolBar), 0, AlwaysEnabledFlag, AlwaysEnabledFlag);
      /* i18n-hint: Clicking this menu item shows the toolbar for selecting a time range of audio*/
      c->AddCheck(wxT("ShowSelectionTB"), XXO("&Selection Toolbar"), FN(OnShowSelectionToolBar), 0, AlwaysEnabledFlag, AlwaysEnabledFlag);

      c->EndSubMenu();

      c->AddSeparator();

      c->AddCheck(wxT("ShowExtraMenus"), XXO("&Extra Menus (on/off)"), FN(OnShowExtraMenus),
         gPrefs->Read(wxT("/GUI/ShowExtraMenus"), 0L), AlwaysEnabledFlag, AlwaysEnabledFlag);
      c->AddCheck(wxT("ShowClipping"), XXO("&Show Clipping (on/off)"), FN(OnShowClipping),
         gPrefs->Read(wxT("/GUI/ShowClipping"), 0L), AlwaysEnabledFlag, AlwaysEnabledFlag);


      c->EndMenu();

      /////////////////////////////////////////////////////////////////////////////
      // Transport Menu
      /////////////////////////////////////////////////////////////////////////////

      /*i18n-hint: 'Transport' is the name given to the set of controls that
      play, record, pause etc. */
      c->BeginMenu(_("T&ransport"));
      c->SetDefaultFlags(CanStopAudioStreamFlag, CanStopAudioStreamFlag);
      c->BeginSubMenu(_("Pl&ay"));
      /* i18n-hint: (verb) Start or Stop audio playback*/
      c->AddItem(wxT("PlayStop"), XXO("Pl&ay/Stop"), FN(OnPlayStop), wxT("Space"));
      c->AddItem(wxT("PlayStopSelect"), XXO("Play/Stop and &Set Cursor"), FN(OnPlayStopSelect), wxT("X"));
      c->AddItem(wxT("PlayLooped"), XXO("&Loop Play"), FN(OnPlayLooped), wxT("Shift+Space"),
         CanStopAudioStreamFlag,
         CanStopAudioStreamFlag);
      c->AddItem(wxT("Pause"), XXO("&Pause"), FN(OnPause), wxT("P"));
      c->EndSubMenu();

      c->BeginSubMenu( _("&Record"));
      c->SetDefaultFlags(AudioIONotBusyFlag | CanStopAudioStreamFlag,
                         AudioIONotBusyFlag | CanStopAudioStreamFlag);
      /* i18n-hint: (verb)*/
      c->AddItem(wxT("Record1stChoice"), XXO("&Record"), FN(OnRecord), wxT("R"));
      // The OnRecord2ndChoice function is: if normal record records beside,
      // it records below, if normal record records below, it records beside.
      // TODO: Do 'the right thing' with other options like TimerRecord.
      bool bPreferNewTrack;
      gPrefs->Read("/GUI/PreferNewTrackRecord",&bPreferNewTrack, false);
      c->AddItem(  wxT("Record2ndChoice"),
         // Our first choice is bound to R (by default) and gets the prime position.
         // We supply the name for the 'other one' here.  It should be bound to Shift+R
         (bPreferNewTrack ? _("&Append Record") : _("Record &New Track")), false,
         FN(OnRecord2ndChoice),
         wxT("Shift+R")
      );

      // JKC: I decided to duplicate this between play and record, rather than put it
      // at the top level.  AddItem can now cope with simple duplicated items.
      // PRL:  This second registration of wxT("Pause"), with unspecified flags,
      // in fact will use the same flags as in the previous registration.
      c->AddItem(wxT("Pause"), XXO("&Pause"), FN(OnPause), wxT("P"));
      c->EndSubMenu();

      // JKC: ANSWER-ME: How is 'cursor to' different to 'Skip To' and how is it useful?
      // GA: 'Skip to' moves the viewpoint to center of the track and preserves the
      // selection. 'Cursor to' does neither. 'Center at' might describe it better than 'Skip'.
      c->BeginSubMenu(_("&Cursor to"));

      c->SetLongName( _("Cursor to Selection Start"))->AddItem(wxT("CursSelStart"), XXO("Selection Star&t"), FN(OnCursorSelStart),
                 TimeSelectedFlag, TimeSelectedFlag);
      c->SetLongName( _("Cursor to Selection End"))->AddItem(wxT("CursSelEnd"), XXO("Selection En&d"), FN(OnCursorSelEnd),
                 TimeSelectedFlag, TimeSelectedFlag);

      c->SetLongName( _("Cursor to Track Start"))->AddItem(wxT("CursTrackStart"), XXO("Track &Start"), FN(OnCursorTrackStart), wxT("J"),
         TracksSelectedFlag, TracksSelectedFlag);
      c->SetLongName( _("Cursor to Track End"))->AddItem(wxT("CursTrackEnd"), XXO("Track &End"), FN(OnCursorTrackEnd), wxT("K"),
         TracksSelectedFlag, TracksSelectedFlag);

      c->SetLongName( _("Cursor to Prev Clip Boundary"))->AddItem(wxT("CursPrevClipBoundary"), XXO("Pre&vious Clip Boundary"), FN(OnCursorPrevClipBoundary), wxT(""),
         WaveTracksExistFlag, WaveTracksExistFlag);
      c->SetLongName( _("Cursor to Next Clip Boundary"))->AddItem(wxT("CursNextClipBoundary"), XXO("Ne&xt Clip Boundary"), FN(OnCursorNextClipBoundary), wxT(""),
         WaveTracksExistFlag, WaveTracksExistFlag);

      c->SetLongName( _("Cursor to Project Start"))->AddItem(wxT("CursProjectStart"), XXO("&Project Start"), FN(OnSkipStart), wxT("Home"));
      c->SetLongName( _("Cursor to Project End"))->AddItem(wxT("CursProjectEnd"), XXO("Project E&nd"), FN(OnSkipEnd), wxT("End"));

      c->EndSubMenu();

      c->AddSeparator();

      /////////////////////////////////////////////////////////////////////////////

      c->BeginSubMenu(_("Pla&y Region"));

      c->AddItem(wxT("LockPlayRegion"), XXO("&Lock"), FN(OnLockPlayRegion),
         PlayRegionNotLockedFlag,
         PlayRegionNotLockedFlag);
      c->AddItem(wxT("UnlockPlayRegion"), XXO("&Unlock"), FN(OnUnlockPlayRegion),
         PlayRegionLockedFlag,
         PlayRegionLockedFlag);

      c->EndSubMenu();

      c->AddSeparator();

      c->AddItem(wxT("RescanDevices"), XXO("R&escan Audio Devices"), FN(OnRescanDevices),
                 AudioIONotBusyFlag | CanStopAudioStreamFlag,
                 AudioIONotBusyFlag | CanStopAudioStreamFlag);

      c->BeginSubMenu(_("Transport &Options"));
      // Sound Activated recording options
      c->AddItem(wxT("SoundActivationLevel"), XXO("Sound Activation Le&vel..."), FN(OnSoundActivated),
                 AudioIONotBusyFlag | CanStopAudioStreamFlag,
                 AudioIONotBusyFlag | CanStopAudioStreamFlag);
      c->AddCheck(wxT("SoundActivation"), XXO("Sound A&ctivated Recording (on/off)"), FN(OnToggleSoundActivated), 0,
                  AudioIONotBusyFlag | CanStopAudioStreamFlag,
                  AudioIONotBusyFlag | CanStopAudioStreamFlag);
      c->AddSeparator();

      c->AddCheck(wxT("Duplex"), XXO("&Overdub (on/off)"), FN(OnTogglePlayRecording), 0,
                  AudioIONotBusyFlag | CanStopAudioStreamFlag,
                  AudioIONotBusyFlag | CanStopAudioStreamFlag);
      c->AddCheck(wxT("SWPlaythrough"), XXO("So&ftware Playthrough (on/off)"), FN(OnToggleSWPlaythrough), 0,
                  AudioIONotBusyFlag | CanStopAudioStreamFlag,
                  AudioIONotBusyFlag | CanStopAudioStreamFlag);
      c->EndSubMenu();

      c->EndMenu();

      //////////////////////////////////////////////////////////////////////////
      // Tracks Menu (formerly Project Menu)
      //////////////////////////////////////////////////////////////////////////

      c->BeginMenu(_("&Tracks"));
      c->SetDefaultFlags(AudioIONotBusyFlag, AudioIONotBusyFlag);

      //////////////////////////////////////////////////////////////////////////

      c->BeginSubMenu(_("Add &New"));

      c->AddItem(wxT("NewStereoTrack"), XXO("&Stereo Track"), FN(OnNewStereoTrack));

      c->EndSubMenu();

      //////////////////////////////////////////////////////////////////////////

      c->AddSeparator();

      c->AddItem(wxT("Resample"), XXO("&Resample..."), FN(OnResample),
         AudioIONotBusyFlag | WaveTracksSelectedFlag,
         AudioIONotBusyFlag | WaveTracksSelectedFlag);

      c->AddSeparator();

      c->AddItem(wxT("RemoveTracks"), XXO("Remo&ve Tracks"), FN(OnRemoveTracks),
         AudioIONotBusyFlag | TracksSelectedFlag,
         AudioIONotBusyFlag | TracksSelectedFlag);

      c->EndMenu();

      // All of this is a bit hacky until we can get more things connected into
      // the plugin manager...sorry! :-(

      wxArrayString defaults;

      //////////////////////////////////////////////////////////////////////////
      // Tools Menu
      //////////////////////////////////////////////////////////////////////////

      c->BeginMenu(_("T&ools"));

      c->AddItem(wxT("ManageMacros"), XXO("&Macros..."), FN(OnManageMacros));

      c->BeginSubMenu(_("&Apply Macro"));
      c->AddItem(wxT("ApplyMacrosPalette"), XXO("&Palette..."), FN(OnApplyMacrosPalette));
      c->AddSeparator();
      PopulateMacrosMenu( c, AudioIONotBusyFlag );
      c->EndSubMenu();
      c->AddSeparator();

// PRL: team consensus for 2.2.0 was, we let end users have this diagnostic,
// as they used to in 1.3.x
//#ifdef IS_ALPHA
      // TODO: What should we do here?  Make benchmark a plug-in?
      // Easy enough to do.  We'd call it mod-self-test.
      c->AddItem(wxT("Benchmark"), XXO("&Run Benchmark..."), FN(OnBenchmark));
//#endif

#ifdef IS_ALPHA
      c->AddCheck(wxT("DetectUpstreamDropouts"),
                  XXO("Detect Upstream Dropouts"),
                  FN(OnDetectUpstreamDropouts),
                  gAudioIO->mDetectUpstreamDropouts);
#endif

      c->EndMenu();


#ifdef __WXMAC__
      /////////////////////////////////////////////////////////////////////////////
      // poor imitation of the Mac Windows Menu
      /////////////////////////////////////////////////////////////////////////////

      {
      c->BeginMenu(_("&Window"));
      /* i18n-hint: Standard Macintosh Window menu item:  Make (the current
       * window) shrink to an icon on the dock */
      c->AddItem(wxT("MacMinimize"), XXO("&Minimize"), FN(OnMacMinimize),
                 wxT("Ctrl+M"), NotMinimizedFlag, NotMinimizedFlag);
      /* i18n-hint: Standard Macintosh Window menu item:  Make (the current
       * window) full sized */
      c->AddItem(wxT("MacZoom"), XXO("&Zoom"), FN(OnMacZoom),
                 wxT(""), NotMinimizedFlag, NotMinimizedFlag);
      c->AddSeparator();
      /* i18n-hint: Standard Macintosh Window menu item:  Make all project
       * windows un-hidden */
      c->AddItem(wxT("MacBringAllToFront"),
                 XXO("&Bring All to Front"), FN(OnMacBringAllToFront),
                 wxT(""), AlwaysEnabledFlag, AlwaysEnabledFlag);
      c->EndMenu();
      }
#endif

      /////////////////////////////////////////////////////////////////////////////
      // Help Menu
      /////////////////////////////////////////////////////////////////////////////

#ifdef __WXMAC__
      wxGetApp().s_macHelpMenuTitleName = _("&Help");
#endif

      c->BeginMenu(_("&Help"));
      c->SetDefaultFlags(AlwaysEnabledFlag, AlwaysEnabledFlag);

      c->AddItem(wxT("QuickHelp"), XXO("&Quick Help..."), FN(OnQuickHelp));
      c->AddItem(wxT("Manual"), XXO("&Manual..."), FN(OnManual));
      c->AddSeparator();

      c->BeginSubMenu(_("&Diagnostics"));
      c->AddItem(wxT("DeviceInfo"), XXO("Au&dio Device Info..."), FN(OnAudioDeviceInfo),
         AudioIONotBusyFlag,
         AudioIONotBusyFlag);

      c->AddItem(wxT("Log"), XXO("Show &Log..."), FN(OnShowLog));

      c->AddItem(wxT("CheckDeps"), XXO("Chec&k Dependencies..."), FN(OnCheckDependencies),
                 AudioIONotBusyFlag, AudioIONotBusyFlag);
      c->EndSubMenu();

#ifndef __WXMAC__
      c->AddSeparator();
#endif

      c->AddItem(wxT("About"), XXO("&About Audacity..."), FN(OnAbout));

      c->EndMenu();

      /////////////////////////////////////////////////////////////////////////////


      c->SetDefaultFlags(AlwaysEnabledFlag, AlwaysEnabledFlag);

      bool bShowExtraMenus;
      gPrefs->Read(wxT("/GUI/ShowExtraMenus"), &bShowExtraMenus, false);
      std::unique_ptr<wxMenuBar> menubar2;
      if( !bShowExtraMenus )
      {
         menubar2 = c->AddMenuBar(wxT("ext-menu"));
         c->SetOccultCommands(true);
      }

      /////////////////////////////////////////////////////////////////////////////
      // Ext-Menu
      /////////////////////////////////////////////////////////////////////////////

      // i18n-hint: Extra is a menu with extra commands
      c->BeginMenu(_("E&xtra"));

      //////////////////////////////////////////////////////////////////////////

      c->SetDefaultFlags(AlwaysEnabledFlag, AlwaysEnabledFlag);
      c->BeginSubMenu(_("T&ransport"));

      // PlayStop is already in the menus.
      /* i18n-hint: (verb) Start playing audio*/
      c->AddItem(wxT("Play"), XXO("Pl&ay"), FN(OnPlayStop),
         WaveTracksExistFlag | AudioIONotBusyFlag,
         WaveTracksExistFlag | AudioIONotBusyFlag);
      /* i18n-hint: (verb) Stop playing audio*/
      c->AddItem(wxT("Stop"), XXO("Sto&p"), FN(OnStop),
         AudioIOBusyFlag | CanStopAudioStreamFlag,
         AudioIOBusyFlag | CanStopAudioStreamFlag);

      c->SetDefaultFlags(CaptureNotBusyFlag, CaptureNotBusyFlag);

      c->AddItem(wxT("PlayOneSec"), XXO("Play &One Second"), FN(OnPlayOneSecond), wxT("1"),
         CaptureNotBusyFlag,
         CaptureNotBusyFlag);
      c->AddItem(wxT("PlayToSelection"), XXO("Play to &Selection"), FN(OnPlayToSelection), wxT("B"),
         CaptureNotBusyFlag,
         CaptureNotBusyFlag);
      c->AddItem(wxT("PlayBeforeSelectionStart"), XXO("Play &Before Selection Start"), FN(OnPlayBeforeSelectionStart), wxT("Shift+F5"));
      c->AddItem(wxT("PlayAfterSelectionStart"), XXO("Play Af&ter Selection Start"), FN(OnPlayAfterSelectionStart), wxT("Shift+F6"));
      c->AddItem(wxT("PlayBeforeSelectionEnd"), XXO("Play Be&fore Selection End"), FN(OnPlayBeforeSelectionEnd), wxT("Shift+F7"));
      c->AddItem(wxT("PlayAfterSelectionEnd"), XXO("Play Aft&er Selection End"), FN(OnPlayAfterSelectionEnd), wxT("Shift+F8"));
      c->AddItem(wxT("PlayBeforeAndAfterSelectionStart"), XXO("Play Before a&nd After Selection Start"), FN(OnPlayBeforeAndAfterSelectionStart), wxT("Ctrl+Shift+F5"));
      c->AddItem(wxT("PlayBeforeAndAfterSelectionEnd"), XXO("Play Before an&d After Selection End"), FN(OnPlayBeforeAndAfterSelectionEnd), wxT("Ctrl+Shift+F7"));
      c->AddItem(wxT("PlayCutPreview"), XXO("Play C&ut Preview"), FN(OnPlayCutPreview), wxT("C"),
         CaptureNotBusyFlag,
         CaptureNotBusyFlag);
      c->EndSubMenu();

      //////////////////////////////////////////////////////////////////////////

      c->SetDefaultFlags(AlwaysEnabledFlag, AlwaysEnabledFlag);
      c->BeginSubMenu(_("&Edit"));

      c->AddItem(wxT("DeleteKey"), XXO("&Delete Key"), FN(OnDelete), wxT("Backspace"),
         AudioIONotBusyFlag | TracksSelectedFlag | TimeSelectedFlag | NoAutoSelect,
         AudioIONotBusyFlag | TracksSelectedFlag | TimeSelectedFlag);

      c->AddItem(wxT("DeleteKey2"), XXO("Delete Key&2"), FN(OnDelete), wxT("Delete"),
         AudioIONotBusyFlag | TracksSelectedFlag | TimeSelectedFlag | NoAutoSelect,
         AudioIONotBusyFlag | TracksSelectedFlag | TimeSelectedFlag);
      c->EndSubMenu();

      //////////////////////////////////////////////////////////////////////////

      c->SetDefaultFlags(AudioIOBusyFlag, AudioIOBusyFlag);
      c->BeginSubMenu(_("See&k"));

      c->AddItem(wxT("SeekLeftShort"), XXO("Short Seek &Left During Playback"), FN(OnSeekLeftShort), wxT("Left\tallowDup"));
      c->AddItem(wxT("SeekRightShort"), XXO("Short Seek &Right During Playback"), FN(OnSeekRightShort), wxT("Right\tallowDup"));
      c->AddItem(wxT("SeekLeftLong"), XXO("Long Seek Le&ft During Playback"), FN(OnSeekLeftLong), wxT("Shift+Left\tallowDup"));
      c->AddItem(wxT("SeekRightLong"), XXO("Long Seek Rig&ht During Playback"), FN(OnSeekRightLong), wxT("Shift+Right\tallowDup"));
      c->EndSubMenu();

      //////////////////////////////////////////////////////////////////////////

      c->SetDefaultFlags(AlwaysEnabledFlag, AlwaysEnabledFlag);
      c->BeginSubMenu(_("De&vice"));

      c->AddItem(wxT("InputDevice"), XXO("Change &Recording Device..."), FN(OnInputDevice), wxT("Shift+I"),
         AudioIONotBusyFlag,
         AudioIONotBusyFlag);
      c->AddItem(wxT("OutputDevice"), XXO("Change &Playback Device..."), FN(OnOutputDevice), wxT("Shift+O"),
         AudioIONotBusyFlag,
         AudioIONotBusyFlag);
      c->AddItem(wxT("AudioHost"), XXO("Change Audio &Host..."), FN(OnAudioHost), wxT("Shift+H"),
         AudioIONotBusyFlag,
         AudioIONotBusyFlag);
      c->AddItem(wxT("InputChannels"), XXO("Change Recording Cha&nnels..."), FN(OnInputChannels), wxT("Shift+N"),
         AudioIONotBusyFlag,
         AudioIONotBusyFlag);
      c->EndSubMenu();

      //////////////////////////////////////////////////////////////////////////

      c->SetDefaultFlags(AlwaysEnabledFlag, AlwaysEnabledFlag);
      c->BeginSubMenu(_("&Selection"));

      c->AddItem(wxT("SelStart"), XXO("Selection to &Start"), FN(OnSelToStart), wxT("Shift+Home"));
      c->AddItem(wxT("SelEnd"), XXO("Selection to En&d"), FN(OnSelToEnd), wxT("Shift+End"));
      c->AddItem(wxT("SelExtLeft"), XXO("Selection Extend &Left"), FN(OnSelExtendLeft), wxT("Shift+Left\twantKeyup\tallowDup"),
                 TracksExistFlag | TrackPanelHasFocus,
                 TracksExistFlag | TrackPanelHasFocus);
      c->AddItem(wxT("SelExtRight"), XXO("Selection Extend &Right"), FN(OnSelExtendRight), wxT("Shift+Right\twantKeyup\tallowDup"),
                 TracksExistFlag | TrackPanelHasFocus,
                 TracksExistFlag | TrackPanelHasFocus);

      c->AddItem(wxT("SelSetExtLeft"), XXO("Set (or Extend) Le&ft Selection"), FN(OnSelSetExtendLeft),
                 TracksExistFlag | TrackPanelHasFocus,
                 TracksExistFlag | TrackPanelHasFocus);
      c->AddItem(wxT("SelSetExtRight"), XXO("Set (or Extend) Rig&ht Selection"), FN(OnSelSetExtendRight),
                 TracksExistFlag | TrackPanelHasFocus,
                 TracksExistFlag | TrackPanelHasFocus);

      c->AddItem(wxT("SelCntrLeft"), XXO("Selection Contract L&eft"), FN(OnSelContractLeft), wxT("Ctrl+Shift+Right\twantKeyup"),
                 TracksExistFlag | TrackPanelHasFocus,
                 TracksExistFlag | TrackPanelHasFocus);
      c->AddItem(wxT("SelCntrRight"), XXO("Selection Contract R&ight"), FN(OnSelContractRight), wxT("Ctrl+Shift+Left\twantKeyup"),
                 TracksExistFlag | TrackPanelHasFocus,
                 TracksExistFlag | TrackPanelHasFocus);

      c->EndSubMenu();


      c->SetDefaultFlags(AlwaysEnabledFlag, AlwaysEnabledFlag);
      c->AddSeparator();

      c->AddGlobalCommand(wxT("PrevWindow"), XXO("Move Backward Through Active Windows"), FN(PrevWindow), wxT("Alt+Shift+F6"));
      c->AddGlobalCommand(wxT("NextWindow"), XXO("Move Forward Through Active Windows"), FN(NextWindow), wxT("Alt+F6"));

      //////////////////////////////////////////////////////////////////////////

      c->SetDefaultFlags(AlwaysEnabledFlag, AlwaysEnabledFlag);
      c->BeginSubMenu(_("F&ocus"));

      c->AddItem(wxT("PrevFrame"), XXO("Move &Backward from Toolbars to Tracks"), FN(PrevFrame), wxT("Ctrl+Shift+F6"));
      c->AddItem(wxT("NextFrame"), XXO("Move F&orward from Toolbars to Tracks"), FN(NextFrame), wxT("Ctrl+F6"));

      c->SetDefaultFlags(TracksExistFlag | TrackPanelHasFocus,
         TracksExistFlag | TrackPanelHasFocus);
      c->AddItem(wxT("PrevTrack"), XXO("Move Focus to &Previous Track"), FN(OnCursorUp), wxT("Up"));
      c->AddItem(wxT("NextTrack"), XXO("Move Focus to &Next Track"), FN(OnCursorDown), wxT("Down"));
      c->AddItem(wxT("FirstTrack"), XXO("Move Focus to &First Track"), FN(OnFirstTrack), wxT("Ctrl+Home"));
      c->AddItem(wxT("LastTrack"), XXO("Move Focus to &Last Track"), FN(OnLastTrack), wxT("Ctrl+End"));

      c->AddItem(wxT("ShiftUp"), XXO("Move Focus to P&revious and Select"), FN(OnShiftUp), wxT("Shift+Up"));
      c->AddItem(wxT("ShiftDown"), XXO("Move Focus to N&ext and Select"), FN(OnShiftDown), wxT("Shift+Down"));

      c->AddItem(wxT("Toggle"), XXO("&Toggle Focused Track"), FN(OnToggle), wxT("Return"));
      c->AddItem(wxT("ToggleAlt"), XXO("Toggle Focuse&d Track"), FN(OnToggle), wxT("NUMPAD_ENTER"));
      c->EndSubMenu();

      //////////////////////////////////////////////////////////////////////////

      c->SetDefaultFlags(TracksExistFlag, TracksExistFlag );
      c->BeginSubMenu(_("&Cursor"));

      c->AddItem(wxT("CursorLeft"), XXO("Cursor &Left"), FN(OnCursorLeft), wxT("Left\twantKeyup\tallowDup"),
                 TracksExistFlag | TrackPanelHasFocus,
                 TracksExistFlag | TrackPanelHasFocus);
      c->AddItem(wxT("CursorRight"), XXO("Cursor &Right"), FN(OnCursorRight), wxT("Right\twantKeyup\tallowDup"),
                 TracksExistFlag | TrackPanelHasFocus,
                 TracksExistFlag | TrackPanelHasFocus);
      c->AddItem(wxT("CursorShortJumpLeft"), XXO("Cursor Sh&ort Jump Left"), FN(OnCursorShortJumpLeft), wxT(","),
                 TracksExistFlag | TrackPanelHasFocus,
                 TracksExistFlag | TrackPanelHasFocus);
      c->AddItem(wxT("CursorShortJumpRight"), XXO("Cursor Shor&t Jump Right"), FN(OnCursorShortJumpRight), wxT("."),
                 TracksExistFlag | TrackPanelHasFocus,
                 TracksExistFlag | TrackPanelHasFocus);
      c->AddItem(wxT("CursorLongJumpLeft"), XXO("Cursor Long J&ump Left"), FN(OnCursorLongJumpLeft), wxT("Shift+,"),
                 TracksExistFlag | TrackPanelHasFocus,
                 TracksExistFlag | TrackPanelHasFocus);
      c->AddItem(wxT("CursorLongJumpRight"), XXO("Cursor Long Ju&mp Right"), FN(OnCursorLongJumpRight), wxT("Shift+."),
                 TracksExistFlag | TrackPanelHasFocus,
                 TracksExistFlag | TrackPanelHasFocus);

      c->EndSubMenu();

      //////////////////////////////////////////////////////////////////////////

      c->SetDefaultFlags(AlwaysEnabledFlag, AlwaysEnabledFlag);
      c->BeginSubMenu(_("&Track"));
      c->AddItem(wxT("TrackMenu"), XXO("Op&en Menu on Focused Track..."), FN(OnTrackMenu), wxT("Shift+M\tskipKeydown"),
                 TracksExistFlag | TrackPanelHasFocus,
                 TracksExistFlag | TrackPanelHasFocus);
      c->AddItem(wxT("TrackClose"), XXO("&Close Focused Track"), FN(OnTrackClose), wxT("Shift+C"),
                 AudioIONotBusyFlag | TrackPanelHasFocus | TracksExistFlag,
                 AudioIONotBusyFlag | TrackPanelHasFocus | TracksExistFlag);
      c->EndSubMenu();

      // These are the more useful to VI user Scriptables.
      // i18n-hint: Scriptables are commands normally used from Python, Perl etc.
      c->BeginSubMenu(_("&Scriptables I"));

      // Note that the PLUGIN_SYMBOL must have a space between words, 
      // whereas the short-form used here must not.
      // (So if you did write "CompareAudio" for the PLUGIN_SYMBOL name, then
      // you would have to use "Compareaudio" here.)

      c->AddItem(wxT("SelectTime"), XXO("Select Time..."), FN(OnAudacityCommand),
         AudioIONotBusyFlag,  AudioIONotBusyFlag);
      c->AddItem(wxT("SelectFrequencies"), XXO("Select Frequencies..."), FN(OnAudacityCommand),
         AudioIONotBusyFlag,  AudioIONotBusyFlag);
      c->AddItem(wxT("SelectTracks"), XXO("Select Tracks..."), FN(OnAudacityCommand),
         AudioIONotBusyFlag,  AudioIONotBusyFlag);

      c->AddItem(wxT("SetTrackStatus"), XXO("Set Track Status..."), FN(OnAudacityCommand),
         AudioIONotBusyFlag,  AudioIONotBusyFlag);
      c->AddItem(wxT("SetTrackAudio"), XXO("Set Track Audio..."), FN(OnAudacityCommand),
         AudioIONotBusyFlag,  AudioIONotBusyFlag);
      c->AddItem(wxT("SetTrackVisuals"), XXO("Set Track Visuals..."), FN(OnAudacityCommand),
         AudioIONotBusyFlag,  AudioIONotBusyFlag);


      c->AddItem(wxT("GetPreference"), XXO("Get Preference..."), FN(OnAudacityCommand),
         AudioIONotBusyFlag,  AudioIONotBusyFlag);
      c->AddItem(wxT("SetPreference"), XXO("Set Preference..."), FN(OnAudacityCommand),
         AudioIONotBusyFlag,  AudioIONotBusyFlag);
      c->AddItem(wxT("SetClip"), XXO("Set Clip..."), FN(OnAudacityCommand),
         AudioIONotBusyFlag,  AudioIONotBusyFlag);
      c->AddItem(wxT("SetEnvelope"), XXO("Set Envelope..."), FN(OnAudacityCommand),
         AudioIONotBusyFlag,  AudioIONotBusyFlag);
      c->AddItem(wxT("SetLabel"), XXO("Set Label..."), FN(OnAudacityCommand),
         AudioIONotBusyFlag,  AudioIONotBusyFlag);
      c->AddItem(wxT("SetProject"), XXO("Set Project..."), FN(OnAudacityCommand),
         AudioIONotBusyFlag,  AudioIONotBusyFlag);

      c->EndSubMenu();
      // Less useful to VI users.
      c->BeginSubMenu(_("Scripta&bles II"));

      c->AddItem(wxT("Select"), XXO("Select..."), FN(OnAudacityCommand),
         AudioIONotBusyFlag,  AudioIONotBusyFlag);
      c->AddItem(wxT("SetTrack"), XXO("Set Track..."), FN(OnAudacityCommand),
         AudioIONotBusyFlag,  AudioIONotBusyFlag);
      c->AddItem(wxT("GetInfo"), XXO("Get Info..."), FN(OnAudacityCommand),
         AudioIONotBusyFlag,  AudioIONotBusyFlag);
      c->AddItem(wxT("Message"), XXO("Message..."), FN(OnAudacityCommand),
         AudioIONotBusyFlag,  AudioIONotBusyFlag);
      c->AddItem(wxT("Help"), XXO("Help..."), FN(OnAudacityCommand),
         AudioIONotBusyFlag,  AudioIONotBusyFlag);

      c->AddItem(wxT("Import2"), XXO("Import..."), FN(OnAudacityCommand),
         AudioIONotBusyFlag,  AudioIONotBusyFlag);
      c->AddItem(wxT("Export2"), XXO("Export..."), FN(OnAudacityCommand),
         AudioIONotBusyFlag,  AudioIONotBusyFlag);
      c->AddItem(wxT("OpenProject2"), XXO("Open Project..."), FN(OnAudacityCommand),
         AudioIONotBusyFlag,  AudioIONotBusyFlag);
      c->AddItem(wxT("SaveProject2"), XXO("Save Project..."), FN(OnAudacityCommand),
         AudioIONotBusyFlag,  AudioIONotBusyFlag);

      c->AddItem(wxT("Drag"), XXO("Move Mouse..."), FN(OnAudacityCommand),
         AudioIONotBusyFlag,  AudioIONotBusyFlag);
      c->AddItem(wxT("CompareAudio"), XXO("Compare Audio..."), FN(OnAudacityCommand),
         AudioIONotBusyFlag,  AudioIONotBusyFlag);

      c->EndSubMenu();

      // Accel key is not bindable.
      c->AddItem(wxT("FullScreenOnOff"), XXO("&Full Screen (on/off)"), FN(OnFullScreen),
#ifdef __WXMAC__
         wxT("Ctrl+/"),
#else
         wxT("F11"),
#endif
         AlwaysEnabledFlag, AlwaysEnabledFlag,
         wxTopLevelWindow::IsFullScreen() ? 1:0); // Check Mark.

#ifdef __WXMAC__
      /* i18n-hint: Shrink all project windows to icons on the Macintosh tooldock */
      c->AddItem(wxT("MacMinimizeAll"), XXO("Minimize All Projects"),
         FN(OnMacMinimizeAll), wxT("Ctrl+Alt+M"),
         AlwaysEnabledFlag, AlwaysEnabledFlag);
#endif
      
      

      c->EndMenu();


      SetMenuBar(menubar.release());
      // Bug 143 workaround.
      // The bug is in wxWidgets.  For a menu that has scrollers, the
      // scrollers have an ID of 0 (not wxID_NONE which is -3).
      // Therefore wxWidgets attempts to find a help string. See
      // wxFrameBase::ShowMenuHelp(int menuId)
      // It finds a bogus automatic help string of "Recent &Files"
      // from that submenu.
      // So we set the help string for command with Id 0 to empty.
      mRecentFilesMenu->GetParent()->SetHelpString( 0, "" );
   }



   mLastFlags = AlwaysEnabledFlag;

#if defined(__WXDEBUG__)
//   c->CheckDups();
#endif
}

#undef XXO



void AudacityProject::PopulateMacrosMenu( CommandManager* c, CommandFlag flags  )
{
   wxArrayString names = MacroCommands::GetNames();
   int i;

   for (i = 0; i < (int)names.GetCount(); i++) {
      wxString MacroID = ApplyMacroDialog::MacroIdOfName( names[i] );
      c->AddItem(MacroID, names[i], false, FN(OnApplyMacroDirectly),
         flags,
         flags);
   }

}

#undef FN

void AudacityProject::CreateRecentFilesMenu(CommandManager *c)
{
   // Recent Files and Recent Projects menus

#ifdef __WXMAC__
   /* i18n-hint: This is the name of the menu item on Mac OS X only */
   mRecentFilesMenu = c->BeginSubMenu(_("Open Recent"));
#else
   /* i18n-hint: This is the name of the menu item on Windows and Linux */
   mRecentFilesMenu = c->BeginSubMenu(_("Recent &Files"));
#endif

   wxGetApp().GetRecentFiles()->UseMenu(mRecentFilesMenu);
   wxGetApp().GetRecentFiles()->AddFilesToMenu(mRecentFilesMenu);

   c->EndSubMenu();

}

void AudacityProject::ModifyUndoMenuItems()
{
   wxString desc;
   int cur = GetUndoManager()->GetCurrentState();

   if (GetUndoManager()->UndoAvailable()) {
      GetUndoManager()->GetShortDescription(cur, &desc);
      mCommandManager.Modify(wxT("Undo"),
                             wxString::Format(_("&Undo %s"),
                                              desc));
      mCommandManager.Enable(wxT("Undo"), this->UndoAvailable());
   }
   else {
      mCommandManager.Modify(wxT("Undo"),
                             _("&Undo"));
   }

   if (GetUndoManager()->RedoAvailable()) {
      GetUndoManager()->GetShortDescription(cur+1, &desc);
      mCommandManager.Modify(wxT("Redo"),
                             wxString::Format(_("&Redo %s"),
                                              desc));
      mCommandManager.Enable(wxT("Redo"), this->RedoAvailable());
   }
   else {
      mCommandManager.Modify(wxT("Redo"),
                             _("&Redo"));
      mCommandManager.Enable(wxT("Redo"), false);
   }
}

void AudacityProject::RebuildMenuBar()
{
   // On OSX, we can't rebuild the menus while a modal dialog is being shown
   // since the enabled state for menus like Quit and Preference gets out of
   // sync with wxWidgets idea of what it should be.
#if defined(__WXMAC__) && defined(__WXDEBUG__)
   {
      wxDialog *dlg = wxDynamicCast(wxGetTopLevelParent(FindFocus()), wxDialog);
      wxASSERT((!dlg || !dlg->IsModal()));
   }
#endif

   // Allow FileHistory to remove its own menu
   wxGetApp().GetRecentFiles()->RemoveMenu(mRecentFilesMenu);

   // Delete the menus, since we will soon recreate them.
   // Rather oddly, the menus don't vanish as a result of doing this.
   {
      std::unique_ptr<wxMenuBar> menuBar{ GetMenuBar() };
      DetachMenuBar();
      // menuBar gets deleted here
   }

   mCommandManager.PurgeData();

   CreateMenusAndCommands();

   ModuleManager::Get().Dispatch(MenusRebuilt);
}

void AudacityProject::RebuildOtherMenus()
{
}

CommandFlag AudacityProject::GetFocusedFrame()
{
   wxWindow *w = FindFocus();

   while (w && mToolManager && mTrackPanel) {
      if (w == mToolManager->GetTopDock()) {
         return TopDockHasFocus;
      }

      if (w == mRuler)
         return RulerHasFocus;

      if (w == mTrackPanel) {
         return TrackPanelHasFocus;
      }

      if (w == mToolManager->GetBotDock()) {
         return BotDockHasFocus;
      }

      w = w->GetParent();
   }

   return AlwaysEnabledFlag;
}

CommandFlag AudacityProject::GetUpdateFlags(bool checkActive)
{
   // This method determines all of the flags that determine whether
   // certain menu items and commands should be enabled or disabled,
   // and returns them in a bitfield.  Note that if none of the flags
   // have changed, it's not necessary to even check for updates.
   auto flags = AlwaysEnabledFlag;
   // static variable, used to remember flags for next time.
   static auto lastFlags = flags;

   if (auto focus = wxWindow::FindFocus()) {
      while (focus && focus->GetParent())
         focus = focus->GetParent();
      if (focus && !static_cast<wxTopLevelWindow*>(focus)->IsIconized())
         flags |= NotMinimizedFlag;
   }

   // quick 'short-circuit' return.
   if ( checkActive && !IsActive() ){
      // short cirucit return should preserve flags that have not been calculated.
      flags = (lastFlags & ~NotMinimizedFlag) | flags;
      lastFlags = flags;
      return flags;
   }

   if (!gAudioIO->IsAudioTokenActive(GetAudioIOToken()))
      flags |= AudioIONotBusyFlag;
   else
      flags |= AudioIOBusyFlag;

   if( gAudioIO->IsPaused() )
      flags |= PausedFlag;
   else
      flags |= NotPausedFlag;

   if (!mViewInfo.selectedRegion.isPoint())
      flags |= TimeSelectedFlag;

   TrackListIterator iter(GetTracks());
   Track *t = iter.First();
   while (t) {
      flags |= TracksExistFlag;
      if (t->GetKind() == Track::Wave) {
         flags |= WaveTracksExistFlag;
         flags |= PlayableTracksExistFlag;
         if (t->GetSelected()) {
            flags |= TracksSelectedFlag;
            if (t->GetLinked()) {
               flags |= StereoRequiredFlag;
            }
            else {
               flags |= WaveTracksSelectedFlag;
               flags |= AudioTracksSelectedFlag;
            }
         }
         if( t->GetEndTime() > t->GetStartTime() )
            flags |= HasWaveDataFlag;
      }
      t = iter.Next();
   }

   if((msClipT1 - msClipT0) > 0.0)
      flags |= ClipboardFlag;

   if (GetUndoManager()->UnsavedChanges() || !IsProjectSaved())
      flags |= UnsavedChangesFlag;

   if (!mLastEffect.IsEmpty())
      flags |= HasLastEffectFlag;

   if (UndoAvailable())
      flags |= UndoAvailableFlag;

   if (RedoAvailable())
      flags |= RedoAvailableFlag;

   if (ZoomInAvailable() && (flags & TracksExistFlag))
      flags |= ZoomInAvailableFlag;

   if (ZoomOutAvailable() && (flags & TracksExistFlag))
      flags |= ZoomOutAvailableFlag;

   flags |= GetFocusedFrame();

   double start, end;
   GetPlayRegion(&start, &end);
   if (IsPlayRegionLocked())
      flags |= PlayRegionLockedFlag;
   else if (start != end)
      flags |= PlayRegionNotLockedFlag;

   if (flags & AudioIONotBusyFlag) {
      if (flags & TimeSelectedFlag) {
         if (flags & TracksSelectedFlag) {
            flags |= CutCopyAvailableFlag;
         }
      }
   }

   if (wxGetApp().GetRecentFiles()->GetCount() > 0)
      flags |= HaveRecentFiles;

  if (!mIsCapturing)
      flags |= CaptureNotBusyFlag;

   ControlToolBar *bar = GetControlToolBar();
   if (bar->ControlToolBar::CanStopAudioStream())
      flags |= CanStopAudioStreamFlag;

   lastFlags = flags;
   return flags;
}

// Select the full time range, if no
// time range is selected.
void AudacityProject::SelectAllIfNone()
{
   auto flags = GetUpdateFlags();
   if(!(flags & TracksSelectedFlag) ||
      (mViewInfo.selectedRegion.isPoint()))
      OnSelectSomething(*this);
}

// Stop playing or recording, if paused.
void AudacityProject::StopIfPaused()
{
   auto flags = GetUpdateFlags();
   if( flags & PausedFlag )
      OnStop(*this);
}

// checkActive is a temporary hack that should be removed as soon as we
// get multiple effect preview working
void AudacityProject::UpdateMenus(bool checkActive)
{
   //ANSWER-ME: Why UpdateMenus only does active project?
   //JKC: Is this test fixing a bug when multiple projects are open?
   //so that menu states work even when different in different projects?
   if (this != GetActiveProject())
      return;

   auto flags = GetUpdateFlags(checkActive);
   auto flags2 = flags;

   // We can enable some extra items if we have select-all-on-none.
   //EXPLAIN-ME: Why is this here rather than in GetUpdateFlags()?
   //ANSWER: Because flags2 is used in the menu enable/disable.
   //The effect still needs flags to determine whether it will need
   //to actually do the 'select all' to make the command valid.
   if (mWhatIfNoSelection != 0)
   {
      if ((flags & TracksExistFlag))
      {
         flags2 |= TracksSelectedFlag;
         if ((flags & WaveTracksExistFlag))
         {
            flags2 |= TimeSelectedFlag
                   |  WaveTracksSelectedFlag
                   |  CutCopyAvailableFlag;
         }
      }
   }

   if( mStopIfWasPaused )
   {
      if( flags & PausedFlag ){
         flags2 |= AudioIONotBusyFlag;
      }
   }

   // Return from this function if nothing's changed since
   // the last time we were here.
   if (flags == mLastFlags)
      return;
   mLastFlags = flags;

   mCommandManager.EnableUsingFlags(flags2 , NoFlagsSpecifed);

   // With select-all-on-none, some items that we don't want enabled may have
   // been enabled, since we changed the flags.  Here we manually disable them.
   // 0 is grey out, 1 is Autoselect, 2 is Give warnings.
   if (mWhatIfNoSelection != 0)
   {
      if (!(flags & TimeSelectedFlag) | !(flags & TracksSelectedFlag))
      {
         mCommandManager.Enable(wxT("SplitCut"), false);
         mCommandManager.Enable(wxT("SplitDelete"), false);
      }
      if (!(flags & WaveTracksSelectedFlag))
      {
         mCommandManager.Enable(wxT("Split"), false);
      }
      if (!(flags & TimeSelectedFlag) | !(flags & WaveTracksSelectedFlag))
      {
         mCommandManager.Enable(wxT("ExportSel"), false);
         mCommandManager.Enable(wxT("SplitNew"), false);
      }
      if (!(flags & TimeSelectedFlag) | !(flags & AudioTracksSelectedFlag))
      {
         mCommandManager.Enable(wxT("Trim"), false);
      }
   }
}

//
// Audio I/O Commands
//

// TODO: Should all these functions which involve
// the toolbar actually move into ControlToolBar?

/// MakeReadyToPlay stops whatever is currently playing
/// and pops the play button up.  Then, if nothing is now
/// playing, it pushes the play button down and enables
/// the stop button.
bool AudacityProject::MakeReadyToPlay(bool loop, bool cutpreview)
{
   ControlToolBar *toolbar = GetControlToolBar();
   wxCommandEvent evt;

   // If this project is playing, stop playing
   if (gAudioIO->IsStreamActive(GetAudioIOToken())) {
      toolbar->SetPlay(false);        //Pops
      toolbar->SetStop(true);         //Pushes stop down
      toolbar->OnStop(evt);

      ::wxMilliSleep(100);
   }

   // If it didn't stop playing quickly, or if some other
   // project is playing, return
   if (gAudioIO->IsBusy())
      return false;

   ControlToolBar::PlayAppearance appearance =
      cutpreview ? ControlToolBar::PlayAppearance::CutPreview
      : loop ? ControlToolBar::PlayAppearance::Looped
      : ControlToolBar::PlayAppearance::Straight;
   toolbar->SetPlay(true, appearance);
   toolbar->SetStop(false);

   return true;
}

void AudacityProject::OnPlayOneSecond(const CommandContext &WXUNUSED(context) )
{
   if( !MakeReadyToPlay() )
      return;

   double pos = mTrackPanel->GetMostRecentXPos();
   GetControlToolBar()->PlayPlayRegion
      (SelectedRegion(pos - 0.5, pos + 0.5), GetDefaultPlayOptions(),
       PlayMode::oneSecondPlay);
}


/// The idea for this function (and first implementation)
/// was from Juhana Sadeharju.  The function plays the
/// sound between the current mouse position and the
/// nearest selection boundary.  This gives four possible
/// play regions depending on where the current mouse
/// position is relative to the left and right boundaries
/// of the selection region.
void AudacityProject::OnPlayToSelection(const CommandContext &WXUNUSED(context) )
{
   if( !MakeReadyToPlay() )
      return;

   double pos = mTrackPanel->GetMostRecentXPos();

   double t0,t1;
   // check region between pointer and the nearest selection edge
   if (fabs(pos - mViewInfo.selectedRegion.t0()) <
       fabs(pos - mViewInfo.selectedRegion.t1())) {
      t0 = t1 = mViewInfo.selectedRegion.t0();
   } else {
      t0 = t1 = mViewInfo.selectedRegion.t1();
   }
   if( pos < t1)
      t0=pos;
   else
      t1=pos;

   // JKC: oneSecondPlay mode disables auto scrolling
   // On balance I think we should always do this in this function
   // since you are typically interested in the sound EXACTLY
   // where the cursor is.
   // TODO: have 'playing attributes' such as 'with_autoscroll'
   // rather than modes, since that's how we're now using the modes.

   // An alternative, commented out below, is to disable autoscroll
   // only when playing a short region, less than or equal to a second.
//   mLastPlayMode = ((t1-t0) > 1.0) ? normalPlay : oneSecondPlay;

   GetControlToolBar()->PlayPlayRegion
      (SelectedRegion(t0, t1), GetDefaultPlayOptions(), PlayMode::oneSecondPlay);
}

// The next 4 functions provide a limited version of the
// functionality of OnPlayToSelection() for keyboard users

void AudacityProject::OnPlayBeforeSelectionStart(const CommandContext &WXUNUSED(context) )
{
   if( !MakeReadyToPlay() )
      return;

   double t0 = mViewInfo.selectedRegion.t0();
   double beforeLen;
   gPrefs->Read(wxT("/AudioIO/CutPreviewBeforeLen"), &beforeLen, 2.0);

   GetControlToolBar()->PlayPlayRegion(SelectedRegion(t0 - beforeLen, t0), GetDefaultPlayOptions(), PlayMode::oneSecondPlay);
}

void AudacityProject::OnPlayAfterSelectionStart(const CommandContext &WXUNUSED(context) )
{
   if( !MakeReadyToPlay() )
      return;

   double t0 = mViewInfo.selectedRegion.t0();
   double t1 = mViewInfo.selectedRegion.t1();
   double afterLen;
   gPrefs->Read(wxT("/AudioIO/CutPreviewAfterLen"), &afterLen, 1.0);

   if ( t1 - t0 > 0.0 && t1 - t0 < afterLen )
      GetControlToolBar()->PlayPlayRegion(SelectedRegion(t0, t1), GetDefaultPlayOptions(),
                                          PlayMode::oneSecondPlay);
   else
      GetControlToolBar()->PlayPlayRegion(SelectedRegion(t0, t0 + afterLen), GetDefaultPlayOptions(), PlayMode::oneSecondPlay);
}

void AudacityProject::OnPlayBeforeSelectionEnd(const CommandContext &WXUNUSED(context) )
{
   if( !MakeReadyToPlay() )
      return;

   double t0 = mViewInfo.selectedRegion.t0();
   double t1 = mViewInfo.selectedRegion.t1();
   double beforeLen;
   gPrefs->Read(wxT("/AudioIO/CutPreviewBeforeLen"), &beforeLen, 2.0);

   if ( t1 - t0 > 0.0 && t1 - t0 < beforeLen )
      GetControlToolBar()->PlayPlayRegion(SelectedRegion(t0, t1), GetDefaultPlayOptions(),
                                          PlayMode::oneSecondPlay);
   else
      GetControlToolBar()->PlayPlayRegion(SelectedRegion(t1 - beforeLen, t1), GetDefaultPlayOptions(), PlayMode::oneSecondPlay);
}


void AudacityProject::OnPlayAfterSelectionEnd(const CommandContext &WXUNUSED(context) )
{
   if( !MakeReadyToPlay() )
      return;

   double t1 = mViewInfo.selectedRegion.t1();
   double afterLen;
   gPrefs->Read(wxT("/AudioIO/CutPreviewAfterLen"), &afterLen, 1.0);

   GetControlToolBar()->PlayPlayRegion(SelectedRegion(t1, t1 + afterLen), GetDefaultPlayOptions(), PlayMode::oneSecondPlay);
}

void AudacityProject::OnPlayBeforeAndAfterSelectionStart(const CommandContext &WXUNUSED(context) )
{
   if (!MakeReadyToPlay())
      return;

   double t0 = mViewInfo.selectedRegion.t0();
   double t1 = mViewInfo.selectedRegion.t1();
   double beforeLen;
   gPrefs->Read(wxT("/AudioIO/CutPreviewBeforeLen"), &beforeLen, 2.0);
   double afterLen;
   gPrefs->Read(wxT("/AudioIO/CutPreviewAfterLen"), &afterLen, 1.0);

   if ( t1 - t0 > 0.0 && t1 - t0 < afterLen )
      GetControlToolBar()->PlayPlayRegion(SelectedRegion(t0 - beforeLen, t1), GetDefaultPlayOptions(), PlayMode::oneSecondPlay);
   else
      GetControlToolBar()->PlayPlayRegion(SelectedRegion(t0 - beforeLen, t0 + afterLen), GetDefaultPlayOptions(), PlayMode::oneSecondPlay);
}

void AudacityProject::OnPlayBeforeAndAfterSelectionEnd(const CommandContext &WXUNUSED(context) )
{
   if (!MakeReadyToPlay())
      return;

   double t0 = mViewInfo.selectedRegion.t0();
   double t1 = mViewInfo.selectedRegion.t1();
   double beforeLen;
   gPrefs->Read(wxT("/AudioIO/CutPreviewBeforeLen"), &beforeLen, 2.0);
   double afterLen;
   gPrefs->Read(wxT("/AudioIO/CutPreviewAfterLen"), &afterLen, 1.0);

   if ( t1 - t0 > 0.0 && t1 - t0 < beforeLen )
      GetControlToolBar()->PlayPlayRegion(SelectedRegion(t0, t1 + afterLen), GetDefaultPlayOptions(), PlayMode::oneSecondPlay);
   else
      GetControlToolBar()->PlayPlayRegion(SelectedRegion(t1 - beforeLen, t1 + afterLen), GetDefaultPlayOptions(), PlayMode::oneSecondPlay);
}


void AudacityProject::OnPlayLooped(const CommandContext &WXUNUSED(context) )
{
   if( !MakeReadyToPlay(true) )
      return;

   // Now play in a loop
   // Will automatically set mLastPlayMode
   GetControlToolBar()->PlayCurrentRegion(true);
}

void AudacityProject::OnPlayCutPreview(const CommandContext &WXUNUSED(context) )
{
   if ( !MakeReadyToPlay(false, true) )
      return;

   // Play with cut preview
   GetControlToolBar()->PlayCurrentRegion(false, true);
}

void AudacityProject::OnPlayStop(const CommandContext &WXUNUSED(context) )
{
   ControlToolBar *toolbar = GetControlToolBar();

   //If this project is playing, stop playing, make sure everything is unpaused.
   if (gAudioIO->IsStreamActive(GetAudioIOToken())) {
      toolbar->SetPlay(false);        //Pops
      toolbar->SetStop(true);         //Pushes stop down
      toolbar->StopPlaying();
   }
   else if (gAudioIO->IsStreamActive()) {
      //If this project isn't playing, but another one is, stop playing the old and start the NEW.

      //find out which project we need;
      AudacityProject* otherProject = NULL;
      for(unsigned i=0; i<gAudacityProjects.size(); i++) {
         if(gAudioIO->IsStreamActive(gAudacityProjects[i]->GetAudioIOToken())) {
            otherProject=gAudacityProjects[i].get();
            break;
         }
      }

      //stop playing the other project
      if(otherProject) {
         ControlToolBar *otherToolbar = otherProject->GetControlToolBar();
         otherToolbar->SetPlay(false);        //Pops
         otherToolbar->SetStop(true);         //Pushes stop down
         otherToolbar->StopPlaying();
      }

      //play the front project
      if (!gAudioIO->IsBusy()) {
         //update the playing area
         TP_DisplaySelection();
         //Otherwise, start playing (assuming audio I/O isn't busy)
         //toolbar->SetPlay(true); // Not needed as done in PlayPlayRegion.
         toolbar->SetStop(false);

         // Will automatically set mLastPlayMode
         toolbar->PlayCurrentRegion(false);
      }
   }
   else if (!gAudioIO->IsBusy()) {
      //Otherwise, start playing (assuming audio I/O isn't busy)
      //toolbar->SetPlay(true); // Not needed as done in PlayPlayRegion.
      toolbar->SetStop(false);

      // Will automatically set mLastPlayMode
      toolbar->PlayCurrentRegion(false);
   }
}

void AudacityProject::OnStop(const CommandContext &WXUNUSED(context) )
{
   wxCommandEvent evt;

   GetControlToolBar()->OnStop(evt);
}

void AudacityProject::OnPause(const CommandContext &WXUNUSED(context) )
{
   wxCommandEvent evt;

   GetControlToolBar()->OnPause(evt);
}

void AudacityProject::OnRecord(const CommandContext &WXUNUSED(context) )
{
   wxCommandEvent evt;
   evt.SetInt(2); // 0 is default, use 1 to set shift on, 2 to clear it

   GetControlToolBar()->OnRecord(evt);
}

// If first choice is record same track 2nd choice is record NEW track
// and vice versa.
void AudacityProject::OnRecord2ndChoice(const CommandContext &WXUNUSED(context) )
{
   wxCommandEvent evt;
   evt.SetInt(1); // 0 is default, use 1 to set shift on, 2 to clear it

   GetControlToolBar()->OnRecord(evt);
}

// The code for "OnPlayStopSelect" is simply the code of "OnPlayStop" and "OnStopSelect" merged.
void AudacityProject::OnPlayStopSelect(const CommandContext &WXUNUSED(context) )
{
   ControlToolBar *toolbar = GetControlToolBar();
   wxCommandEvent evt;
   if (DoPlayStopSelect(false, false))
      toolbar->OnStop(evt);
   else if (!gAudioIO->IsBusy()) {
      //Otherwise, start playing (assuming audio I/O isn't busy)
      //toolbar->SetPlay(true); // Not needed as set in PlayPlayRegion()
      toolbar->SetStop(false);

      // Will automatically set mLastPlayMode
      toolbar->PlayCurrentRegion(false);
   }
}

bool AudacityProject::DoPlayStopSelect(bool click, bool shift)
{
   ControlToolBar *toolbar = GetControlToolBar();

   //If busy, stop playing, make sure everything is unpaused.
   if (gAudioIO->IsStreamActive(GetAudioIOToken())) {
      toolbar->SetPlay(false);        //Pops
      toolbar->SetStop(true);         //Pushes stop down

      // change the selection
      auto time = gAudioIO->GetStreamTime();
      auto &selection = mViewInfo.selectedRegion;
      if (shift && click) {
         // Change the region selection, as if by shift-click at the play head
         auto t0 = selection.t0(), t1 = selection.t1();
         if (time < t0)
            // Grow selection
            t0 = time;
         else if (time > t1)
            // Grow selection
            t1 = time;
         else {
            // Shrink selection, changing the nearer boundary
            if (fabs(t0 - time) < fabs(t1 - time))
               t0 = time;
            else
               t1 = time;
         }
         selection.setTimes(t0, t1);
      }
      else if (click){
         // avoid a point at negative time.
         time = wxMax( time, 0 );
         // Set a point selection, as if by a click at the play head
         selection.setTimes(time, time);
      } else
         // How stop and set cursor always worked
         // -- change t0, collapsing to point only if t1 was greater
         selection.setT0(time, false);

      ModifyState();
      return true;
   }
   return false;
}

void AudacityProject::OnStopSelect(const CommandContext &WXUNUSED(context) )
{
   wxCommandEvent evt;

   if (gAudioIO->IsStreamActive()) {
      mViewInfo.selectedRegion.setT0(gAudioIO->GetStreamTime(), false);
      GetControlToolBar()->OnStop(evt);
      ModifyState();
   }
}

void AudacityProject::OnToggleSoundActivated(const CommandContext &WXUNUSED(context) )
{
   bool pause;
   gPrefs->Read(wxT("/AudioIO/SoundActivatedRecord"), &pause, false);
   gPrefs->Write(wxT("/AudioIO/SoundActivatedRecord"), !pause);
   gPrefs->Flush();
}

void AudacityProject::OnTogglePlayRecording(const CommandContext &WXUNUSED(context) )
{
   bool Duplex;
   gPrefs->Read(wxT("/AudioIO/Duplex"), &Duplex, true);
   gPrefs->Write(wxT("/AudioIO/Duplex"), !Duplex);
   gPrefs->Flush();
}

void AudacityProject::OnToggleSWPlaythrough(const CommandContext &WXUNUSED(context) )
{
   bool SWPlaythrough;
   gPrefs->Read(wxT("/AudioIO/SWPlaythrough"), &SWPlaythrough, false);
   gPrefs->Write(wxT("/AudioIO/SWPlaythrough"), !SWPlaythrough);
   gPrefs->Flush();
}

double AudacityProject::GetTime(const Track *t)
{
   double stime = 0.0;

   if (t->GetKind() == Track::Wave) {
      WaveTrack *w = (WaveTrack *)t;
      stime = w->GetEndTime();

      WaveClip *c;
      int ndx;
      for (ndx = 0; ndx < w->GetNumClips(); ndx++) {
         c = w->GetClipByIndex(ndx);
         if (c->GetNumSamples() == 0)
            continue;
         if (c->GetStartTime() < stime) {
            stime = c->GetStartTime();
         }
      }
   }

   return stime;
}

//sort based on flags.  see Project.h for sort flags
void AudacityProject::SortTracks(int flags)
{
   size_t ndx = 0;
   int cmpValue;
   // This one place outside of TrackList where we must use undisguised
   // std::list iterators!  Avoid this elsewhere!
   std::vector<TrackNodePointer> arr;
   arr.reserve(mTracks->size());
   bool lastTrackLinked = false;
   //sort by linked tracks. Assumes linked track follows owner in list.

   // First find the permutation.
   for (auto iter = mTracks->ListOfTracks::begin(),
        end = mTracks->ListOfTracks::end(); iter != end; ++iter) {
      const auto &track = *iter;
      if(lastTrackLinked) {
         //insert after the last track since this track should be linked to it.
         ndx++;
      }
      else {
         bool bArrayTrackLinked = false;
         for (ndx = 0; ndx < arr.size(); ++ndx) {
            Track &arrTrack = **arr[ndx].first;
            // Don't insert between channels of a stereo track!
            if( bArrayTrackLinked ){
               bArrayTrackLinked = false;
            }
            else if(flags & kAudacitySortByName) {
               //do case insensitive sort - cmpNoCase returns less than zero if the string is 'less than' its argument
               //also if we have case insensitive equality, then we need to sort by case as well
               //We sort 'b' before 'B' accordingly.  We uncharacteristically use greater than for the case sensitive
               //compare because 'b' is greater than 'B' in ascii.
               cmpValue = track->GetName().CmpNoCase(arrTrack.GetName());
               if (cmpValue < 0 ||
                   (0 == cmpValue && track->GetName().CompareTo(arrTrack.GetName()) > 0) )
                  break;
            }
            //sort by time otherwise
            else if(flags & kAudacitySortByTime) {
               //we have to search each track and all its linked ones to fine the minimum start time.
               double time1, time2, tempTime;
               const Track* tempTrack;
               size_t candidatesLookedAt;

               candidatesLookedAt = 0;
               tempTrack = &*track;
               time1 = time2 = std::numeric_limits<double>::max(); //TODO: find max time value. (I don't think we have one yet)
               while(tempTrack) {
                  tempTime = GetTime(tempTrack);
                  time1 = std::min(time1, tempTime);
                  if(tempTrack->GetLinked())
                     tempTrack = tempTrack->GetLink();
                  else
                     tempTrack = NULL;
               }

               //get candidate's (from sorted array) time
               tempTrack = &arrTrack;
               while(tempTrack) {
                  tempTime = GetTime(tempTrack);
                  time2 = std::min(time2, tempTime);
                  if(tempTrack->GetLinked() && (ndx+candidatesLookedAt < arr.size()-1) ) {
                     candidatesLookedAt++;
                     tempTrack = &**arr[ndx+candidatesLookedAt].first;
                  }
                  else
                     tempTrack = NULL;
               }

               if (time1 < time2)
                  break;

               ndx+=candidatesLookedAt;
            }
            bArrayTrackLinked = arrTrack.GetLinked();
         }
      }
      arr.insert(arr.begin() + ndx, TrackNodePointer{iter, mTracks.get()});

      lastTrackLinked = track->GetLinked();
   }

   // Now apply the permutation
   mTracks->Permute(arr);
}

void AudacityProject::OnSkipStart(const CommandContext &WXUNUSED(context) )
{
   wxCommandEvent evt;

   GetControlToolBar()->OnRewind(evt);
   ModifyState();
}

void AudacityProject::OnSkipEnd(const CommandContext &WXUNUSED(context) )
{
   wxCommandEvent evt;

   GetControlToolBar()->OnFF(evt);
   ModifyState();
}

void AudacityProject::OnSeekLeftShort(const CommandContext &WXUNUSED(context) )
{
   SeekLeftOrRight( DIRECTION_LEFT, CURSOR_MOVE );
}

void AudacityProject::OnSeekRightShort(const CommandContext &WXUNUSED(context) )
{
   SeekLeftOrRight( DIRECTION_RIGHT, CURSOR_MOVE );
}

void AudacityProject::OnSeekLeftLong(const CommandContext &WXUNUSED(context) )
{
   SeekLeftOrRight( DIRECTION_LEFT, SELECTION_EXTEND );
}

void AudacityProject::OnSeekRightLong(const CommandContext &WXUNUSED(context) )
{
   SeekLeftOrRight( DIRECTION_RIGHT, SELECTION_EXTEND );
}

void AudacityProject::OnSelToStart(const CommandContext &WXUNUSED(context) )
{
   Rewind(true);
   ModifyState();
}

void AudacityProject::OnSelToEnd(const CommandContext &WXUNUSED(context) )
{
   SkipEnd(true);
   ModifyState();
}

/// The following method moves to the previous track
/// selecting and unselecting depending if you are on the start of a
/// block or not.

/// \todo Merge related methods, OnPrevTrack and OnNextTrack.
void AudacityProject::OnPrevTrack( bool shift )
{
   TrackListIterator iter( GetTracks() );
   Track* t = mTrackPanel->GetFocusedTrack();
   if( t == NULL )   // if there isn't one, focus on last
   {
      t = iter.Last();
      mTrackPanel->SetFocusedTrack( t );
      mTrackPanel->EnsureVisible( t );
      ModifyState();
      return;
   }

   Track* p = NULL;
   bool tSelected = false;
   bool pSelected = false;
   if( shift )
   {
      p = mTracks->GetPrev( t, true ); // Get previous track
      if( p == NULL )   // On first track
      {
         // JKC: wxBell() is probably for accessibility, so a blind
         // user knows they were at the top track.
         wxBell();
         if( mCircularTrackNavigation )
         {
            TrackListIterator iter( GetTracks() );
            p = iter.Last();
         }
         else
         {
            mTrackPanel->EnsureVisible( t );
            return;
         }
      }
      tSelected = t->GetSelected();
      if (p)
         pSelected = p->GetSelected();
      if( tSelected && pSelected )
      {
         GetSelectionState().SelectTrack
            ( *mTracks, *t, false, false );
         mTrackPanel->SetFocusedTrack( p );   // move focus to next track down
         mTrackPanel->EnsureVisible( p );
         ModifyState();
         return;
      }
      if( tSelected && !pSelected )
      {
         GetSelectionState().SelectTrack
            ( *mTracks, *p, true, false );
         mTrackPanel->SetFocusedTrack( p );   // move focus to next track down
         mTrackPanel->EnsureVisible( p );
         ModifyState();
         return;
      }
      if( !tSelected && pSelected )
      {
         GetSelectionState().SelectTrack
            ( *mTracks, *p, false, false );
         mTrackPanel->SetFocusedTrack( p );   // move focus to next track down
         mTrackPanel->EnsureVisible( p );
         ModifyState();
         return;
      }
      if( !tSelected && !pSelected )
      {
         GetSelectionState().SelectTrack
            ( *mTracks, *t, true, false );
         mTrackPanel->SetFocusedTrack( p );   // move focus to next track down
         mTrackPanel->EnsureVisible( p );
         ModifyState();
         return;
      }
   }
   else
   {
      p = mTracks->GetPrev( t, true ); // Get next track
      if( p == NULL )   // On last track so stay there?
      {
         wxBell();
         if( mCircularTrackNavigation )
         {
            TrackListIterator iter( GetTracks() );
            for( Track *d = iter.First(); d; d = iter.Next( true ) )
            {
               p = d;
            }
            mTrackPanel->SetFocusedTrack( p );   // Wrap to the first track
            mTrackPanel->EnsureVisible( p );
            ModifyState();
            return;
         }
         else
         {
            mTrackPanel->EnsureVisible( t );
            return;
         }
      }
      else
      {
         mTrackPanel->SetFocusedTrack( p );   // move focus to next track down
         mTrackPanel->EnsureVisible( p );
         ModifyState();
         return;
      }
   }
}

/// The following method moves to the next track,
/// selecting and unselecting depending if you are on the start of a
/// block or not.
void AudacityProject::OnNextTrack( bool shift )
{
   Track *t;
   Track *n;
   TrackListIterator iter( GetTracks() );
   bool tSelected,nSelected;

   t = mTrackPanel->GetFocusedTrack();   // Get currently focused track
   if( t == NULL )   // if there isn't one, focus on first
   {
      t = iter.First();
      mTrackPanel->SetFocusedTrack( t );
      mTrackPanel->EnsureVisible( t );
      ModifyState();
      return;
   }

   if( shift )
   {
      n = mTracks->GetNext( t, true ); // Get next track
      if( n == NULL )   // On last track so stay there
      {
         wxBell();
         if( mCircularTrackNavigation )
         {
            TrackListIterator iter( GetTracks() );
            n = iter.First();
         }
         else
         {
            mTrackPanel->EnsureVisible( t );
            return;
         }
      }
      tSelected = t->GetSelected();
      nSelected = n->GetSelected();
      if( tSelected && nSelected )
      {
         GetSelectionState().SelectTrack
            ( *mTracks, *t, false, false );
         mTrackPanel->SetFocusedTrack( n );   // move focus to next track down
         mTrackPanel->EnsureVisible( n );
         ModifyState();
         return;
      }
      if( tSelected && !nSelected )
      {
         GetSelectionState().SelectTrack
            ( *mTracks, *n, true, false );
         mTrackPanel->SetFocusedTrack( n );   // move focus to next track down
         mTrackPanel->EnsureVisible( n );
         ModifyState();
         return;
      }
      if( !tSelected && nSelected )
      {
         GetSelectionState().SelectTrack
            ( *mTracks, *n, false, false );
         mTrackPanel->SetFocusedTrack( n );   // move focus to next track down
         mTrackPanel->EnsureVisible( n );
         ModifyState();
         return;
      }
      if( !tSelected && !nSelected )
      {
         GetSelectionState().SelectTrack
            ( *mTracks, *t, true, false );
         mTrackPanel->SetFocusedTrack( n );   // move focus to next track down
         mTrackPanel->EnsureVisible( n );
         ModifyState();
         return;
      }
   }
   else
   {
      n = mTracks->GetNext( t, true ); // Get next track
      if( n == NULL )   // On last track so stay there
      {
         wxBell();
         if( mCircularTrackNavigation )
         {
            TrackListIterator iter( GetTracks() );
            n = iter.First();
            mTrackPanel->SetFocusedTrack( n );   // Wrap to the first track
            mTrackPanel->EnsureVisible( n );
            ModifyState();
            return;
         }
         else
         {
            mTrackPanel->EnsureVisible( t );
            return;
         }
      }
      else
      {
         mTrackPanel->SetFocusedTrack( n );   // move focus to next track down
         mTrackPanel->EnsureVisible( n );
         ModifyState();
         return;
      }
   }
}

void AudacityProject::OnCursorUp(const CommandContext &WXUNUSED(context) )
{
   OnPrevTrack( false );
}

void AudacityProject::OnCursorDown(const CommandContext &WXUNUSED(context) )
{
   OnNextTrack( false );
}

void AudacityProject::OnFirstTrack(const CommandContext &WXUNUSED(context) )
{
   Track *t = mTrackPanel->GetFocusedTrack();
   if (!t)
      return;

   TrackListIterator iter(GetTracks());
   Track *f = iter.First();
   if (t != f)
   {
      mTrackPanel->SetFocusedTrack(f);
      ModifyState();
   }
   mTrackPanel->EnsureVisible(f);
}

void AudacityProject::OnLastTrack(const CommandContext &WXUNUSED(context) )
{
   Track *t = mTrackPanel->GetFocusedTrack();
   if (!t)
      return;

   TrackListIterator iter(GetTracks());
   Track *l = iter.Last();
   if (t != l)
   {
      mTrackPanel->SetFocusedTrack(l);
      ModifyState();
   }
   mTrackPanel->EnsureVisible(l);
}

void AudacityProject::OnShiftUp(const CommandContext &WXUNUSED(context) )
{
   OnPrevTrack( true );
}

void AudacityProject::OnShiftDown(const CommandContext &WXUNUSED(context) )
{
   OnNextTrack( true );
}

#include "TrackPanelAx.h"
void AudacityProject::OnToggle(const CommandContext &WXUNUSED(context) )
{
   Track *t;

   t = mTrackPanel->GetFocusedTrack();   // Get currently focused track
   if (!t)
      return;

   GetSelectionState().SelectTrack
      ( *mTracks, *t, !t->GetSelected(), true );
   mTrackPanel->EnsureVisible( t );
   ModifyState();

   mTrackPanel->GetAx().Updated();

   return;
}

void AudacityProject::HandleListSelection(Track *t, bool shift, bool ctrl,
                                     bool modifyState)
{
   GetSelectionState().HandleListSelection
      ( *GetTracks(), mViewInfo, *t,
        shift, ctrl );

   if (! ctrl )
      mTrackPanel->SetFocusedTrack(t);
   Refresh(false);
   if (modifyState)
      ModifyState();
}

// If this returns true, then there was a key up, and nothing more to do,
// after this function has completed.
// (at most this function just does a ModifyState for the keyup)
bool AudacityProject::OnlyHandleKeyUp( const CommandContext &context )
{
   auto evt = context.pEvt;
   bool bKeyUp = (evt) && evt->GetEventType() == wxEVT_KEY_UP;

   if( IsAudioActive() )
      return bKeyUp;
   if( !bKeyUp )
      return false;

   ModifyState();
   return true;
}

void AudacityProject::OnCursorLeft(const CommandContext &context)
{
   if( !OnlyHandleKeyUp( context ) )
      SeekLeftOrRight( DIRECTION_LEFT, CURSOR_MOVE);
}

void AudacityProject::OnCursorRight(const CommandContext &context)
{
   if( !OnlyHandleKeyUp( context ) )
      SeekLeftOrRight( DIRECTION_RIGHT, CURSOR_MOVE);
}

void AudacityProject::OnCursorShortJumpLeft(const CommandContext &WXUNUSED(context) )
{
   OnCursorMove( -mSeekShort );
}

void AudacityProject::OnCursorShortJumpRight(const CommandContext &WXUNUSED(context) )
{
   OnCursorMove( mSeekShort );
}

void AudacityProject::OnCursorLongJumpLeft(const CommandContext &WXUNUSED(context) )
{
   OnCursorMove( -mSeekLong );
}

void AudacityProject::OnCursorLongJumpRight(const CommandContext &WXUNUSED(context) )
{
   OnCursorMove( mSeekLong );
}

void AudacityProject::OnSelSetExtendLeft(const CommandContext &WXUNUSED(context) )
{
   OnBoundaryMove( DIRECTION_LEFT);
}

void AudacityProject::OnSelSetExtendRight(const CommandContext &WXUNUSED(context) )
{
   OnBoundaryMove( DIRECTION_RIGHT);
}

void AudacityProject::OnSelExtendLeft(const CommandContext &context)
{
   if( !OnlyHandleKeyUp( context ) )
      SeekLeftOrRight( DIRECTION_LEFT, SELECTION_EXTEND );
}

void AudacityProject::OnSelExtendRight(const CommandContext &context)
{
   if( !OnlyHandleKeyUp( context ) )
      SeekLeftOrRight( DIRECTION_RIGHT, SELECTION_EXTEND );
}

void AudacityProject::OnSelContractLeft(const CommandContext &context)
{
   if( !OnlyHandleKeyUp( context ) )
      SeekLeftOrRight( DIRECTION_LEFT, SELECTION_CONTRACT );
}

void AudacityProject::OnSelContractRight(const CommandContext &context)
{
   if( !OnlyHandleKeyUp( context ) )
      SeekLeftOrRight( DIRECTION_RIGHT, SELECTION_CONTRACT );
}

//this pops up a dialog which allows the left selection to be set.
//If playing/recording is happening, it sets the left selection at
//the current play position.
void AudacityProject::OnSetLeftSelection(const CommandContext &WXUNUSED(context) )
{
   bool bSelChanged = false;
   if ((GetAudioIOToken() > 0) && gAudioIO->IsStreamActive(GetAudioIOToken()))
   {
      double indicator = gAudioIO->GetStreamTime();
      mViewInfo.selectedRegion.setT0(indicator, false);
      bSelChanged = true;
   }
   else
   {
      wxString fmt = GetSelectionFormat();
      TimeDialog dlg(this, _("Set Left Selection Boundary"),
         fmt, mRate, mViewInfo.selectedRegion.t0(), _("Position"));

      if (wxID_OK == dlg.ShowModal())
      {
         //Get the value from the dialog
         mViewInfo.selectedRegion.setT0(
            std::max(0.0, dlg.GetTimeValue()), false);
         bSelChanged = true;
      }
   }

   if (bSelChanged)
   {
      ModifyState();
      mTrackPanel->Refresh(false);
   }
}


void AudacityProject::OnSetRightSelection(const CommandContext &WXUNUSED(context) )
{
   bool bSelChanged = false;
   if ((GetAudioIOToken() > 0) && gAudioIO->IsStreamActive(GetAudioIOToken()))
   {
      double indicator = gAudioIO->GetStreamTime();
      mViewInfo.selectedRegion.setT1(indicator, false);
      bSelChanged = true;
   }
   else
   {
      wxString fmt = GetSelectionFormat();
      TimeDialog dlg(this, _("Set Right Selection Boundary"),
         fmt, mRate, mViewInfo.selectedRegion.t1(), _("Position"));

      if (wxID_OK == dlg.ShowModal())
      {
         //Get the value from the dialog
         mViewInfo.selectedRegion.setT1(
            std::max(0.0, dlg.GetTimeValue()), false);
         bSelChanged = true;
      }
   }

   if (bSelChanged)
   {
      ModifyState();
      mTrackPanel->Refresh(false);
   }
}

void AudacityProject::NextOrPrevFrame(bool forward)
{
   // Focus won't take in a dock unless at least one descendant window
   // accepts focus.  Tell controls to take focus for the duration of this
   // function, only.  Outside of this, they won't steal the focus when
   // clicked.
   // auto temp1 = AButton::TemporarilyAllowFocus();
   auto temp2 = ASlider::TemporarilyAllowFocus();
   auto temp3 = MeterPanel::TemporarilyAllowFocus();


   // Define the set of windows we rotate among.
   static const unsigned rotationSize = 3u;

   wxWindow *const begin [rotationSize] = {
      GetTopPanel(),
      GetTrackPanel(),
      mToolManager->GetBotDock(),
   };

   const auto end = begin + rotationSize;

   // helper functions
   auto IndexOf = [&](wxWindow *pWindow) {
      return std::find(begin, end, pWindow) - begin;
   };

   auto FindAncestor = [&]() {
      wxWindow *pWindow = wxWindow::FindFocus();
      unsigned index = rotationSize;
      while ( pWindow &&
              (rotationSize == (index = IndexOf(pWindow) ) ) )
         pWindow = pWindow->GetParent();
      return index;
   };

   const auto idx = FindAncestor();
   if (idx == rotationSize)
      return;

   auto idx2 = idx;
   auto increment = (forward ? 1 : rotationSize - 1);

   while( idx != (idx2 = (idx2 + increment) % rotationSize) ) {
      wxWindow *toFocus = begin[idx2];
      bool bIsAnEmptyDock=false;
      if( idx2 != 1 )
         bIsAnEmptyDock = ((idx2==0)?mToolManager->GetTopDock() : mToolManager->GetBotDock())->
         GetChildren().GetCount() < 1;

      // Skip docks that are empty (Bug 1564).
      if( !bIsAnEmptyDock ){
         toFocus->SetFocus();
         if ( FindAncestor() == idx2 )
            // The focus took!
            break;
      }
   }
}

void AudacityProject::NextFrame(const CommandContext &WXUNUSED(context) )
{
   NextOrPrevFrame(true);
}

void AudacityProject::PrevFrame(const CommandContext &WXUNUSED(context) )
{
   NextOrPrevFrame(false);
}

void AudacityProject::NextWindow(const CommandContext &WXUNUSED(context) )
{
   wxWindow *w = wxGetTopLevelParent(wxWindow::FindFocus());
   const auto & list = GetChildren();
   auto iter = list.begin(), end = list.end();

   // If the project window has the current focus, start the search with the first child
   if (w == this)
   {
   }
   // Otherwise start the search with the current window's next sibling
   else
   {
      // Find the window in this projects children.  If the window with the
      // focus isn't a child of this project (like when a dialog is created
      // without specifying a parent), then we'll get back NULL here.
      while (iter != end && *iter != w)
         ++iter;
      if (iter != end)
         ++iter;
   }

   // Search for the next toplevel window
   for (; iter != end; ++iter)
   {
      // If it's a toplevel, visible (we have hidden windows) and is enabled,
      // then we're done.  The IsEnabled() prevents us from moving away from
      // a modal dialog because all other toplevel windows will be disabled.
      w = *iter;
      if (w->IsTopLevel() && w->IsShown() && w->IsEnabled())
      {
         break;
      }
   }

   // Ran out of siblings, so make the current project active
   if ((iter == end) && IsEnabled())
   {
      w = this;
   }

   // And make sure it's on top (only for floating windows...project window will not raise)
   // (Really only works on Windows)
   w->Raise();


#if defined(__WXMAC__) || defined(__WXGTK__)
   // bug 868
   // Simulate a TAB key press before continuing, else the cycle of
   // navigation among top level windows stops because the keystrokes don't
   // go to the CommandManager.
   if (dynamic_cast<wxDialog*>(w)) {
      w->SetFocus();
   }
#endif
}

void AudacityProject::PrevWindow(const CommandContext &WXUNUSED(context) )
{
   wxWindow *w = wxGetTopLevelParent(wxWindow::FindFocus());
   const auto & list = GetChildren();
   auto iter = list.rbegin(), end = list.rend();

   // If the project window has the current focus, start the search with the last child
   if (w == this)
   {
   }
   // Otherwise start the search with the current window's previous sibling
   else
   {
      while (iter != end && *iter != w)
         ++iter;
      if (iter != end)
         ++iter;
   }

   // Search for the previous toplevel window
   for (; iter != end; ++iter)
   {
      // If it's a toplevel and is visible (we have come hidden windows), then we're done
      w = *iter;
      if (w->IsTopLevel() && w->IsShown() && IsEnabled())
      {
         break;
      }
   }

   // Ran out of siblings, so make the current project active
   if ((iter == end) && IsEnabled())
   {
      w = this;
   }

   // And make sure it's on top (only for floating windows...project window will not raise)
   // (Really only works on Windows)
   w->Raise();


#if defined(__WXMAC__) || defined(__WXGTK__)
   // bug 868
   // Simulate a TAB key press before continuing, else the cycle of
   // navigation among top level windows stops because the keystrokes don't
   // go to the CommandManager.
   if (dynamic_cast<wxDialog*>(w)) {
      w->SetFocus();
   }
#endif
}

void AudacityProject::OnTrackMenu(const CommandContext &WXUNUSED(context) )
{
   mTrackPanel->OnTrackMenu();
}

void AudacityProject::OnTrackClose(const CommandContext &WXUNUSED(context) )
{
   Track *t = mTrackPanel->GetFocusedTrack();
   if (!t)
      return;

   if (IsAudioActive())
   {
      this->TP_DisplayStatusMessage(_("Can't delete track with active audio"));
      wxBell();
      return;
   }

   RemoveTrack(t);

   GetTrackPanel()->UpdateViewIfNoTracks();
   GetTrackPanel()->Refresh(false);
}

void AudacityProject::OnInputDevice(const CommandContext &WXUNUSED(context) )
{
   DeviceToolBar *tb = GetDeviceToolBar();
   if (tb) {
      tb->ShowInputDialog();
   }
}

void AudacityProject::OnOutputDevice(const CommandContext &WXUNUSED(context) )
{
   DeviceToolBar *tb = GetDeviceToolBar();
   if (tb) {
      tb->ShowOutputDialog();
   }
}

void AudacityProject::OnAudioHost(const CommandContext &WXUNUSED(context) )
{
   DeviceToolBar *tb = GetDeviceToolBar();
   if (tb) {
      tb->ShowHostDialog();
   }
}

void AudacityProject::OnInputChannels(const CommandContext &WXUNUSED(context) )
{
   DeviceToolBar *tb = GetDeviceToolBar();
   if (tb) {
      tb->ShowChannelsDialog();
   }
}

double AudacityProject::NearestZeroCrossing(double t0)
{
   // Window is 1/100th of a second.
   auto windowSize = size_t(std::max(1.0, GetRate() / 100));
   Floats dist{ windowSize, true };

   TrackListIterator iter(GetTracks());
   Track *track = iter.First();
   while (track) {
      if (!track->GetSelected() || track->GetKind() != (Track::Wave)) {
         track = iter.Next();
         continue;
      }
      WaveTrack *one = (WaveTrack *)track;
      auto oneWindowSize = size_t(std::max(1.0, one->GetRate() / 100));
      Floats oneDist{ oneWindowSize };
      auto s = one->TimeToLongSamples(t0);
      // fillTwo to ensure that missing values are treated as 2, and hence do not
      // get used as zero crossings.
      one->Get((samplePtr)oneDist.get(), floatSample,
               s - (int)oneWindowSize/2, oneWindowSize, fillTwo);

      // Start by penalizing downward motion.  We prefer upward
      // zero crossings.
      if (oneDist[1] - oneDist[0] < 0)
         oneDist[0] = oneDist[0]*6 + (oneDist[0] > 0 ? 0.3 : -0.3);
      for(size_t i=1; i<oneWindowSize; i++)
         if (oneDist[i] - oneDist[i-1] < 0)
            oneDist[i] = oneDist[i]*6 + (oneDist[i] > 0 ? 0.3 : -0.3);

      // Taking the absolute value -- apply a tiny LPF so square waves work.
      float newVal, oldVal = oneDist[0];
      oneDist[0] = fabs(.75 * oneDist[0] + .25 * oneDist[1]);
      for(size_t i=1; i + 1 < oneWindowSize; i++)
      {
         newVal = fabs(.25 * oldVal + .5 * oneDist[i] + .25 * oneDist[i+1]);
         oldVal = oneDist[i];
         oneDist[i] = newVal;
      }
      oneDist[oneWindowSize-1] = fabs(.25 * oldVal +
            .75 * oneDist[oneWindowSize-1]);

      // TODO: The mixed rate zero crossing code is broken,
      // if oneWindowSize > windowSize we'll miss out some
      // samples - so they will still be zero, so we'll use them.
      for(size_t i = 0; i < windowSize; i++) {
         size_t j;
         if (windowSize != oneWindowSize)
            j = i * (oneWindowSize-1) / (windowSize-1);
         else
            j = i;

         dist[i] += oneDist[j];
         // Apply a small penalty for distance from the original endpoint
         dist[i] += 0.1 * (abs(int(i) - int(windowSize/2))) / float(windowSize/2);
      }

      track = iter.Next();
   }

   // Find minimum
   int argmin = 0;
   float min = 3.0;
   for(size_t i=0; i<windowSize; i++) {
      if (dist[i] < min) {
         argmin = i;
         min = dist[i];
      }
   }

   return t0 + (argmin - (int)windowSize/2)/GetRate();
}

void AudacityProject::OnZeroCrossing(const CommandContext &WXUNUSED(context) )
{
   const double t0 = NearestZeroCrossing(mViewInfo.selectedRegion.t0());
   if (mViewInfo.selectedRegion.isPoint())
      mViewInfo.selectedRegion.setTimes(t0, t0);
   else {
      const double t1 = NearestZeroCrossing(mViewInfo.selectedRegion.t1());
      mViewInfo.selectedRegion.setTimes(t0, t1);
   }

   ModifyState();

   mTrackPanel->Refresh(false);
}


/// DoAudacityCommand() takes a PluginID and executes the assocated effect.
///
/// At the moment flags are used only to indicate whether to prompt for parameters,
bool AudacityProject::DoAudacityCommand(const PluginID & ID, const CommandContext & context, int flags)
{
   const PluginDescriptor *plug = PluginManager::Get().GetPlugin(ID);
   if (!plug)
      return false;

   if (flags & OnEffectFlags::kConfigured)
   {
      OnStop(*this);
//    SelectAllIfNone();
   }

/*
   if (em.GetSkipStateFlag())
      flags = flags | OnEffectFlags::kSkipState;

   if (!(flags & OnEffectFlags::kSkipState))
   {
      wxString shortDesc = em.GetCommandName(ID);
      wxString longDesc = em.GetCommandDescription(ID);
      PushState(longDesc, shortDesc);
   }
*/
   RedrawProject();
   return true;
}

void AudacityProject::OnEffect(const CommandContext &context)
{
   // DoEffect(context.parameter, context, 0);
}

void AudacityProject::RebuildAllMenuBars(){
   for( size_t i = 0; i < gAudacityProjects.size(); i++ ) {
      AudacityProject *p = gAudacityProjects[i].get();

      p->RebuildMenuBar();
#if defined(__WXGTK__)
      // Workaround for:
      //
      //   http://bugzilla.audacityteam.org/show_bug.cgi?id=458
      //
      // This workaround should be removed when Audacity updates to wxWidgets 3.x which has a fix.
      wxRect r = p->GetRect();
      p->SetSize(wxSize(1,1));
      p->SetSize(r.GetSize());
#endif
   }
}

void AudacityProject::OnAudacityCommand(const CommandContext & ctx)
{
   wxLogDebug( "Command was: %s", ctx.parameter);
   DoAudacityCommand(EffectManager::Get().GetEffectByIdentifier(ctx.parameter),
      ctx,
      OnEffectFlags::kNone);  // Not configured, so prompt user.
}

//
// File Menu
//

void AudacityProject::OnNew(const CommandContext &WXUNUSED(context) )
{
   CreateNewAudacityProject();
}

void AudacityProject::OnOpen(const CommandContext &WXUNUSED(context) )
{
}

void AudacityProject::OnClose(const CommandContext &WXUNUSED(context) )
{
   mMenuClose = true;
   Close();
}

void AudacityProject::OnSave(const CommandContext &WXUNUSED(context) )
{
   Save();
}

void AudacityProject::OnSaveAs(const CommandContext &WXUNUSED(context) )
{
   SaveAs();
}

void AudacityProject::OnCheckDependencies(const CommandContext &WXUNUSED(context) )
{
   ShowDependencyDialogIfNeeded(this, false);
}

void AudacityProject::OnExit(const CommandContext &WXUNUSED(context) )
{
   QuitAudacity();
}

void AudacityProject::OnExport(const wxString & Format )
{
   Exporter e;
   e.SetDefaultFormat( Format );

   wxGetApp().SetMissingAliasedFileWarningShouldShow(true);
   e.Process(this, false, 0.0, mTracks->GetEndTime());
}

void AudacityProject::OnExportAudio(const CommandContext &WXUNUSED(context) ){   OnExport("");}
void AudacityProject::OnExportMp3(const CommandContext &WXUNUSED(context) ){   OnExport("MP3");}
void AudacityProject::OnExportWav(const CommandContext &WXUNUSED(context) ){   OnExport("WAV");}
void AudacityProject::OnExportOgg(const CommandContext &WXUNUSED(context) ){   OnExport("OGG");}

void AudacityProject::OnExportSelection(const CommandContext &WXUNUSED(context) )
{
   Exporter e;

   wxGetApp().SetMissingAliasedFileWarningShouldShow(true);
   e.SetFileDialogTitle( _("Export Selected Audio") );
   e.Process(this, true, mViewInfo.selectedRegion.t0(),
      mViewInfo.selectedRegion.t1());
}

void AudacityProject::OnExportMultiple(const CommandContext &WXUNUSED(context) )
{
   ExportMultiple em(this);

   wxGetApp().SetMissingAliasedFileWarningShouldShow(true);
   em.ShowModal();
}

void AudacityProject::OnPreferences(const CommandContext &WXUNUSED(context) )
{
   GlobalPrefsDialog dialog(this /* parent */ );

   if (!dialog.ShowModal()) {
      // Canceled
      return;
   }

   // LL:  Moved from PrefsDialog since wxWidgets on OSX can't deal with
   //      rebuilding the menus while the PrefsDialog is still in the modal
   //      state.
   for (size_t i = 0; i < gAudacityProjects.size(); i++) {
      AudacityProject *p = gAudacityProjects[i].get();

      p->RebuildMenuBar();
      p->RebuildOtherMenus();
// TODO: The comment below suggests this workaround is obsolete.
#if defined(__WXGTK__)
      // Workaround for:
      //
      //   http://bugzilla.audacityteam.org/show_bug.cgi?id=458
      //
      // This workaround should be removed when Audacity updates to wxWidgets 3.x which has a fix.
      wxRect r = p->GetRect();
      p->SetSize(wxSize(1,1));
      p->SetSize(r.GetSize());
#endif
   }
}


#include "./prefs/SpectrogramSettings.h"
#include "./prefs/WaveformSettings.h"
void AudacityProject::OnReloadPreferences(const CommandContext &WXUNUSED(context) )
{
   {
      SpectrogramSettings::defaults().LoadPrefs();
      WaveformSettings::defaults().LoadPrefs();

      GlobalPrefsDialog dialog(this /* parent */ );
      wxCommandEvent Evt;
      //dialog.Show();
      dialog.OnOK(Evt);
   }

   // LL:  Moved from PrefsDialog since wxWidgets on OSX can't deal with
   //      rebuilding the menus while the PrefsDialog is still in the modal
   //      state.
   for (size_t i = 0; i < gAudacityProjects.size(); i++) {
      AudacityProject *p = gAudacityProjects[i].get();

      p->RebuildMenuBar();
      p->RebuildOtherMenus();
// TODO: The comment below suggests this workaround is obsolete.
#if defined(__WXGTK__)
      // Workaround for:
      //
      //   http://bugzilla.audacityteam.org/show_bug.cgi?id=458
      //
      // This workaround should be removed when Audacity updates to wxWidgets 3.x which has a fix.
      wxRect r = p->GetRect();
      p->SetSize(wxSize(1,1));
      p->SetSize(r.GetSize());
#endif
   }
}

//
// Edit Menu
//

void AudacityProject::OnUndo(const CommandContext &WXUNUSED(context) )
{
   if (!UndoAvailable()) {
      AudacityMessageBox(_("Nothing to undo"));
      return;
   }

   // can't undo while dragging
   if (mTrackPanel->IsMouseCaptured()) {
      return;
   }

   const UndoState &state = GetUndoManager()->Undo(&mViewInfo.selectedRegion);
   PopState(state);

   mTrackPanel->SetFocusedTrack(NULL);
   mTrackPanel->EnsureVisible(mTrackPanel->GetFirstSelectedTrack());

   RedrawProject();

   if (mHistoryWindow)
      mHistoryWindow->UpdateDisplay();

   ModifyUndoMenuItems();
}

void AudacityProject::OnRedo(const CommandContext &WXUNUSED(context) )
{
   if (!RedoAvailable()) {
      AudacityMessageBox(_("Nothing to redo"));
      return;
   }
   // Can't redo whilst dragging
   if (mTrackPanel->IsMouseCaptured()) {
      return;
   }

   const UndoState &state = GetUndoManager()->Redo(&mViewInfo.selectedRegion);
   PopState(state);

   mTrackPanel->SetFocusedTrack(NULL);
   mTrackPanel->EnsureVisible(mTrackPanel->GetFirstSelectedTrack());

   RedrawProject();

   if (mHistoryWindow)
      mHistoryWindow->UpdateDisplay();

   ModifyUndoMenuItems();
}

void AudacityProject::FinishCopy
   (const Track *n, Track *dest)
{
   if (dest) {
      dest->SetChannel(n->GetChannel());
      dest->SetLinked(n->GetLinked());
      dest->SetName(n->GetName());
   }
}

void AudacityProject::FinishCopy
   (const Track *n, Track::Holder &&dest, TrackList &list)
{
   FinishCopy( n, dest.get() );
   if (dest)
      list.Add(std::move(dest));
}

void AudacityProject::OnCut(const CommandContext &WXUNUSED(context) )
{
   TrackListIterator iter(GetTracks());
   Track *n = iter.First();

   // This doesn't handle cutting labels, it handles
   // cutting the _text_ inside of labels, i.e. if you're
   // in the middle of editing the label text and select "Cut".

   ClearClipboard();

   auto pNewClipboard = TrackList::Create();
   auto &newClipboard = *pNewClipboard;

   n = iter.First();
   while (n) {
      if (n->GetSelected()) {
         Track::Holder dest;
            dest = n->Copy(mViewInfo.selectedRegion.t0(),
                    mViewInfo.selectedRegion.t1());

         FinishCopy(n, std::move(dest), newClipboard);
      }
      n = iter.Next();
   }

   // Survived possibility of exceptions.  Commit changes to the clipboard now.
   newClipboard.Swap(*msClipboard);

   // Proceed to change the project.  If this throws, the project will be
   // rolled back by the top level handler.

   n = iter.First();
   while (n) {
      // We clear from selected tracks.
      if (n->GetSelected()) {
         switch (n->GetKind())
         {
            case Track::Wave:
               if (gPrefs->Read(wxT("/GUI/EnableCutLines"), (long)0)) {
                  ((WaveTrack*)n)->ClearAndAddCutLine(
                     mViewInfo.selectedRegion.t0(),
                     mViewInfo.selectedRegion.t1());
                  break;
               }

               // Fall through

            default:
               n->Clear(mViewInfo.selectedRegion.t0(),
                        mViewInfo.selectedRegion.t1());
            break;
         }
      }
      n = iter.Next();
   }

   msClipT0 = mViewInfo.selectedRegion.t0();
   msClipT1 = mViewInfo.selectedRegion.t1();
   msClipProject = this;

   mViewInfo.selectedRegion.collapseToT0();

   PushState(_("Cut to the clipboard"), _("Cut"));

   // Bug 1663
   //mRuler->ClearPlayRegion();
   mRuler->DrawOverlays( true );

   RedrawProject();

   if (mHistoryWindow)
      mHistoryWindow->UpdateDisplay();
}

void AudacityProject::OnCopy(const CommandContext &WXUNUSED(context) )
{

   TrackListIterator iter(GetTracks());

   Track *n = iter.First();

   ClearClipboard();

   auto pNewClipboard = TrackList::Create();
   auto &newClipboard = *pNewClipboard;

   n = iter.First();
   while (n) {
      if (n->GetSelected()) {
         auto dest = n->Copy(mViewInfo.selectedRegion.t0(),
                 mViewInfo.selectedRegion.t1());
         FinishCopy(n, std::move(dest), newClipboard);
      }
      n = iter.Next();
   }

   // Survived possibility of exceptions.  Commit changes to the clipboard now.
   newClipboard.Swap(*msClipboard);

   msClipT0 = mViewInfo.selectedRegion.t0();
   msClipT1 = mViewInfo.selectedRegion.t1();
   msClipProject = this;

   //Make sure the menus/toolbar states get updated
   mTrackPanel->Refresh(false);

   if (mHistoryWindow)
      mHistoryWindow->UpdateDisplay();
}

void AudacityProject::OnPaste(const CommandContext &WXUNUSED(context) )
{
   // Handle text paste (into active label) first.
   if (this->HandlePasteText())
      return;

   // If nothing's selected, we just insert NEW tracks.
   if (this->HandlePasteNothingSelected())
      return;

   // Otherwise, paste into the selected tracks.
   double t0 = mViewInfo.selectedRegion.t0();
   double t1 = mViewInfo.selectedRegion.t1();

   TrackListIterator iter(GetTracks());
   TrackListConstIterator clipIter(msClipboard.get());

   Track *n = iter.First();
   const Track *c = clipIter.First();
   if (c == NULL)
      return;
   Track *ff = NULL;
   const Track *lastClipBeforeMismatch = NULL;
   const Track *mismatchedClip = NULL;
   const Track *prevClip = NULL;

   bool bAdvanceClipboard = true;
   bool bPastedSomething = false;

   while (n && c) {
      if (n->GetSelected()) {
         bAdvanceClipboard = true;
         if (mismatchedClip)
            c = mismatchedClip;
         if (c->GetKind() != n->GetKind()) {
            if (!mismatchedClip) {
               lastClipBeforeMismatch = prevClip;
               mismatchedClip = c;
            }
            bAdvanceClipboard = false;
            c = lastClipBeforeMismatch;

            // If the types still don't match...
            while (c && c->GetKind() != n->GetKind()) {
               prevClip = c;
               c = clipIter.Next();
            }
         }

         // Handle case where the first track in clipboard
         // is of different type than the first selected track
         if (!c) {
            c = mismatchedClip;
            while (n && (c->GetKind() != n->GetKind() || !n->GetSelected()))
            {
               n = iter.Next();
            }
            if (!n)
               c = NULL;
         }

         // The last possible case for cross-type pastes: triggered when we try to
         // paste 1+ tracks from one type into 1+ tracks of another type. If
         // there's a mix of types, this shouldn't run.
         if (!c)
            // Throw, so that any previous changes to the project in this loop
            // are discarded.
            throw SimpleMessageBoxException{
               _("Pasting one type of track into another is not allowed.")
            };

         // When trying to copy from stereo to mono track, show error and exit
         // TODO: Automatically offer user to mix down to mono (unfortunately
         //       this is not easy to implement
         if (c->GetLinked() && !n->GetLinked())
            // Throw, so that any previous changes to the project in this loop
            // are discarded.
            throw SimpleMessageBoxException{
               _("Copying stereo audio into a mono track is not allowed.")
            };

         if (!ff)
            ff = n;

         Maybe<WaveTrack::Locker> locker;
         if (msClipProject != this && c->GetKind() == Track::Wave)
            // Cause duplication of block files on disk, when copy is
            // between projects
            locker.create(static_cast<const WaveTrack*>(c));

         wxASSERT( n && c );
         if (c->GetKind() == Track::Wave && n->GetKind() == Track::Wave)
         {
            bPastedSomething = true;
            ((WaveTrack*)n)->ClearAndPaste(t0, t1, (WaveTrack*)c, true);
         }
         else
         {
            bPastedSomething = true;
            n->Clear(t0, t1);
            n->Paste(t0, c);
         }

         // When copying from mono to stereo track, paste the wave form
         // to both channels
         if (n->GetLinked() && !c->GetLinked())
         {
            n = iter.Next();

            if (n->GetKind() == Track::Wave) {
               bPastedSomething = true;
               ((WaveTrack *)n)->ClearAndPaste(t0, t1, c, true);
            }
            else
            {
               n->Clear(t0, t1);
               bPastedSomething = true;
               n->Paste(t0, c);
            }
         }

         if (bAdvanceClipboard){
            prevClip = c;
            c = clipIter.Next();
         }
      } // if (n->GetSelected())
      n = iter.Next();
   }

   // This block handles the cases where our clipboard is smaller
   // than the amount of selected destination tracks. We take the
   // last wave track, and paste that one into the remaining
   // selected tracks.
   if ( n && !c )
   {
      TrackListOfKindIterator clipWaveIter(Track::Wave, msClipboard.get());
      c = clipWaveIter.Last();

      while (n) {
         if (n->GetSelected() && n->GetKind()==Track::Wave) {
            if (c) {
               wxASSERT(c->GetKind() == Track::Wave);
               bPastedSomething = true;
               ((WaveTrack *)n)->ClearAndPaste(t0, t1, (WaveTrack *)c, true);
            }
            else {
               auto tmp = mTrackFactory->NewWaveTrack( ((WaveTrack*)n)->GetSampleFormat(), ((WaveTrack*)n)->GetRate());
               tmp->InsertSilence(0.0, msClipT1 - msClipT0); // MJS: Is this correct?
               tmp->Flush();

               bPastedSomething = true;
               ((WaveTrack *)n)->ClearAndPaste(t0, t1, tmp.get(), true);
            }
         }
         n = iter.Next();
      }
   }

   // TODO: What if we clicked past the end of the track?

   if (bPastedSomething)
   {
      mViewInfo.selectedRegion.setT1(t0 + msClipT1 - msClipT0);

      PushState(_("Pasted from the clipboard"), _("Paste"));

      RedrawProject();

      if (ff)
         mTrackPanel->EnsureVisible(ff);
   }
}

// Handle text paste (into active label), if any. Return true if did paste.
// (This was formerly the first part of overly-long OnPaste.)
bool AudacityProject::HandlePasteText()
{
   return false;
}

// Return true if nothing selected, regardless of paste result.
// If nothing was selected, create and paste into NEW tracks.
// (This was formerly the second part of overly-long OnPaste.)
bool AudacityProject::HandlePasteNothingSelected()
{
   // First check whether anything's selected.
   bool bAnySelected = false;
   TrackListIterator iterTrack(GetTracks());
   Track* pTrack = iterTrack.First();
   while (pTrack) {
      if (pTrack->GetSelected())
      {
         bAnySelected = true;
         break;
      }
      pTrack = iterTrack.Next();
   }

   if (bAnySelected)
      return false;
   else
   {
      TrackListConstIterator iterClip(msClipboard.get());
      auto pClip = iterClip.First();
      if (!pClip)
         return true; // nothing to paste

      Track* pFirstNewTrack = NULL;
      while (pClip) {
         Maybe<WaveTrack::Locker> locker;
         if ((msClipProject != this) && (pClip->GetKind() == Track::Wave))
            // Cause duplication of block files on disk, when copy is
            // between projects
            locker.create(static_cast<const WaveTrack*>(pClip));

         Track::Holder uNewTrack;
         Track *pNewTrack;
         switch (pClip->GetKind()) {
         case Track::Wave:
            {
               WaveTrack *w = (WaveTrack *)pClip;
               uNewTrack = mTrackFactory->NewWaveTrack(w->GetSampleFormat(), w->GetRate()),
               pNewTrack = uNewTrack.get();
            }
            break;
         default:
            pClip = iterClip.Next();
            continue;
         }
         wxASSERT(pClip);

         pNewTrack->Paste(0.0, pClip);

         if (!pFirstNewTrack)
            pFirstNewTrack = pNewTrack;

         pNewTrack->SetSelected(true);
         if (uNewTrack)
            FinishCopy(pClip, std::move(uNewTrack), *mTracks);
         else
            FinishCopy(pClip, pNewTrack);

         pClip = iterClip.Next();
      }

      // Select some pasted samples, which is probably impossible to get right
      // with various project and track sample rates.
      // So do it at the sample rate of the project
      AudacityProject *p = GetActiveProject();
      double projRate = p->GetRate();
      double quantT0 = QUANTIZED_TIME(msClipT0, projRate);
      double quantT1 = QUANTIZED_TIME(msClipT1, projRate);
      mViewInfo.selectedRegion.setTimes(
         0.0,   // anywhere else and this should be
                // half a sample earlier
         quantT1 - quantT0);

      PushState(_("Pasted from the clipboard"), _("Paste"));

      RedrawProject();

      if (pFirstNewTrack)
         mTrackPanel->EnsureVisible(pFirstNewTrack);

      return true;
   }
}

void AudacityProject::OnPasteOver(const CommandContext &context) // not currently in use it appears
{
   if((msClipT1 - msClipT0) > 0.0)
   {
      mViewInfo.selectedRegion.setT1(
         mViewInfo.selectedRegion.t0() + (msClipT1 - msClipT0));
         // MJS: pointless, given what we do in OnPaste?
   }
   OnPaste(context);

   return;
}

void AudacityProject::OnDelete(const CommandContext &WXUNUSED(context) )
{
   Clear();
}

int AudacityProject::CountSelectedWaveTracks()
{
   TrackListIterator iter(GetTracks());

   int count =0;
   for (Track *t = iter.First(); t; t = iter.Next()) {
      if( (t->GetKind() == Track::Wave) && t->GetSelected() )
         count++;
   }
   return count;
}

int AudacityProject::CountSelectedTracks()
{
   TrackListIterator iter(GetTracks());

   int count =0;
   for (Track *t = iter.First(); t; t = iter.Next()) {
      if( t->GetSelected() )
         count++;
   }
   return count;
}

void AudacityProject::OnSelectTimeAndTracks(bool bAllTime, bool bAllTracks)
{
   if ( bAllTime )
      mViewInfo.selectedRegion.setTimes(
         mTracks->GetMinOffset(), mTracks->GetEndTime());

   if ( bAllTracks ) {
      TrackListIterator iter(GetTracks());
      for (Track *t = iter.First(); t; t = iter.Next())
         t->SetSelected(true);

      ModifyState();
      mTrackPanel->Refresh(false);
   }
}

void AudacityProject::OnSelectAllTime(const CommandContext &WXUNUSED(context) )
{
   OnSelectTimeAndTracks( true, false );
}

void AudacityProject::OnSelectAllTracks(const CommandContext &WXUNUSED(context) )
{
   OnSelectTimeAndTracks( false, true );
}

void AudacityProject::OnSelectAll(const CommandContext &WXUNUSED(context) )
{
   OnSelectTimeAndTracks( true, true );
}

// This function selects all tracks if no tracks selected,
// and all time if no time selected.
// There is an argument for making it just count wave tracks,
// However you could then not select a label and cut it,
// without this function selecting all tracks.
void AudacityProject::OnSelectSomething(const CommandContext &WXUNUSED(context) )
{
   bool bTime = mViewInfo.selectedRegion.isPoint();
   bool bTracks = CountSelectedTracks() == 0;

   if( bTime || bTracks )
      OnSelectTimeAndTracks(bTime,bTracks);
}

void AudacityProject::SelectNone()
{
   TrackListIterator iter(GetTracks());
   Track *t = iter.First();
   while (t) {
      t->SetSelected(false);
      t = iter.Next();
   }
   mTrackPanel->Refresh(false);
}

void AudacityProject::OnSelectNone(const CommandContext &WXUNUSED(context) )
{
   mViewInfo.selectedRegion.collapseToT0();
   SelectNone();
   ModifyState();
}

void AudacityProject::OnSelectCursorEnd(const CommandContext &WXUNUSED(context) )
{
   double kWayOverToLeft = -1000000.0;
   double maxEndOffset = kWayOverToLeft;

   TrackListIterator iter(GetTracks());
   Track *t = iter.First();

   while (t) {
      if (t->GetSelected()) {
         if (t->GetEndTime() > maxEndOffset)
            maxEndOffset = t->GetEndTime();
      }

      t = iter.Next();
   }

   if( maxEndOffset <= (kWayOverToLeft +1))
      return;

   mViewInfo.selectedRegion.setT1(maxEndOffset);

   ModifyState();

   mTrackPanel->Refresh(false);
}

void AudacityProject::OnSelectStartCursor(const CommandContext &WXUNUSED(context) )
{
   double kWayOverToRight = 1000000.0;
   double minOffset = kWayOverToRight;

   TrackListIterator iter(GetTracks());
   Track *t = iter.First();

   while (t) {
      if (t->GetSelected()) {
         if (t->GetStartTime() < minOffset)
            minOffset = t->GetStartTime();
      }

      t = iter.Next();
   }

   if( minOffset >= (kWayOverToRight -1 ))
      return;

   mViewInfo.selectedRegion.setT0(minOffset);

   ModifyState();

   mTrackPanel->Refresh(false);
}

void AudacityProject::OnSelectTrackStartToEnd(const CommandContext &WXUNUSED(context) )
{
   double kWayOverToLeft = -1000000.0;
   double maxEndOffset = kWayOverToLeft;
   double kWayOverToRight = 1000000.0;
   double minOffset = kWayOverToRight;

   TrackListIterator iter(GetTracks());
   Track *t = iter.First();

   while (t) {
      if (t->GetSelected()) {
         if (t->GetEndTime() > maxEndOffset)
            maxEndOffset = t->GetEndTime();
         if (t->GetStartTime() < minOffset)
            minOffset = t->GetStartTime();
      }

      t = iter.Next();
   }

   if( maxEndOffset <= (kWayOverToLeft +1))
      return;
   if( minOffset >= (kWayOverToRight -1 ))
      return;

   mViewInfo.selectedRegion.setTimes( minOffset, maxEndOffset );
   ModifyState();

   mTrackPanel->Refresh(false);
}


void AudacityProject::OnSelectPrevClipBoundaryToCursor(const CommandContext &WXUNUSED(context) )
{
   OnSelectClipBoundary(false);
}

void AudacityProject::OnSelectCursorToNextClipBoundary(const CommandContext &WXUNUSED(context) )
{
   OnSelectClipBoundary(true);
}

void AudacityProject::OnSelectClipBoundary(bool next)
{
   std::vector<FoundClipBoundary> results;
   FindClipBoundaries(next ? mViewInfo.selectedRegion.t1() :
      mViewInfo.selectedRegion.t0(), next, results);

   if (results.size() > 0) {
      // note that if there is more than one result, each has the same time value.
      if (next)
         mViewInfo.selectedRegion.setT1(results[0].time);
      else
         mViewInfo.selectedRegion.setT0(results[0].time);

      ModifyState();
      mTrackPanel->Refresh(false);

      wxString message = ClipBoundaryMessage(results);
      mTrackPanel->MessageForScreenReader(message);
   }
}

AudacityProject::FoundClip AudacityProject::FindNextClip(const WaveTrack* wt, double t0, double t1)
{
   AudacityProject::FoundClip result{};
   result.waveTrack = wt;
   const auto clips = wt->SortedClipArray();

   t0 = AdjustForFindingStartTimes(clips, t0);

   auto p = std::find_if(clips.begin(), clips.end(), [&] (const WaveClip* const& clip) {
      return clip->GetStartTime() == t0; });
   if (p != clips.end() && (*p)->GetEndTime() > t1) {
      result.found = true;
      result.startTime = (*p)->GetStartTime();
      result.endTime = (*p)->GetEndTime();
      result.index = std::distance(clips.begin(), p);
   }
   else {
      auto p = std::find_if(clips.begin(), clips.end(), [&] (const WaveClip* const& clip) {
         return clip->GetStartTime() > t0; });
      if (p != clips.end()) {
         result.found = true;
         result.startTime = (*p)->GetStartTime();
         result.endTime = (*p)->GetEndTime();
         result.index = std::distance(clips.begin(), p);
      }
   }

   return result;
}

AudacityProject::FoundClip AudacityProject::FindPrevClip(const WaveTrack* wt, double t0, double t1)
{
   AudacityProject::FoundClip result{};
   result.waveTrack = wt;
   const auto clips = wt->SortedClipArray();

   t0 = AdjustForFindingStartTimes(clips, t0);

   auto p = std::find_if(clips.begin(), clips.end(), [&] (const WaveClip* const& clip) {
      return clip->GetStartTime() == t0; });
   if (p != clips.end() && (*p)->GetEndTime() < t1) {
      result.found = true;
      result.startTime = (*p)->GetStartTime();
      result.endTime = (*p)->GetEndTime();
      result.index = std::distance(clips.begin(), p);
   }
   else {
      auto p = std::find_if(clips.rbegin(), clips.rend(), [&] (const WaveClip* const& clip) {
         return clip->GetStartTime() < t0; });
      if (p != clips.rend()) {
         result.found = true;
         result.startTime = (*p)->GetStartTime();
         result.endTime = (*p)->GetEndTime();
         result.index = static_cast<int>(clips.size()) - 1 - std::distance(clips.rbegin(), p);
      }
   }

   return result;
}

int AudacityProject::FindClips(double t0, double t1, bool next, std::vector<FoundClip>& finalResults)
{
   const TrackList* tracks = GetTracks();
   finalResults.clear();

   bool anyWaveTracksSelected =
      tracks->end() != std::find_if(tracks->begin(), tracks->end(),
         [] (const Track * t) {
            return t->GetSelected() && t->GetKind() == Track::Wave; });

   // first search the tracks individually

   TrackListIterator iter(GetTracks());
   Track* track = iter.First();
   std::vector<FoundClip> results;

   int nTracksSearched = 0;
   int trackNum = 1;
   while (track) {
      if (track->GetKind() == Track::Wave && (!anyWaveTracksSelected || track->GetSelected())) {
         auto waveTrack = static_cast<const WaveTrack*>(track);
         bool stereoAndDiff = waveTrack->GetLinked() && !ChannelsHaveSameClipBoundaries(waveTrack);

         auto result = next ? FindNextClip(waveTrack, t0, t1) :
            FindPrevClip(waveTrack, t0, t1);
         if (result.found) {
            result.trackNum = trackNum;
            result.channel = stereoAndDiff;
            results.push_back(result);
         }
         if (stereoAndDiff) {
            auto waveTrack2 = static_cast<const WaveTrack*>(track->GetLink());
            auto result = next ? FindNextClip(waveTrack2, t0, t1) :
               FindPrevClip(waveTrack2, t0, t1);
            if (result.found) {
               result.trackNum = trackNum;
               result.channel = stereoAndDiff;
               results.push_back(result);
            }
         }

         nTracksSearched++;
      }

      trackNum++;
      track = iter.Next(true);
   }


   if (results.size() > 0) {
      // if any clips were found,
      // find the clip or clips with the min/max start time
      auto compareStart = [] (const FoundClip& a, const FoundClip& b)
         { return a.startTime < b.startTime; };

      auto p = next ? std::min_element(results.begin(), results.end(), compareStart) :
         std::max_element(results.begin(), results.end(), compareStart);

      std::vector<FoundClip> resultsStartTime;
      for ( auto &r : results )
         if ( r.startTime == (*p).startTime )
            resultsStartTime.push_back( r );

      if (resultsStartTime.size() > 1) {
         // more than one clip with same start time so
         // find the clip or clips with the min/max end time
         auto compareEnd = [] (const FoundClip& a, const FoundClip& b)
            { return a.endTime < b.endTime; };

         auto p = next ? std::min_element(resultsStartTime.begin(),
            resultsStartTime.end(), compareEnd) :
            std::max_element(resultsStartTime.begin(),
            resultsStartTime.end(), compareEnd);

         for ( auto &r : resultsStartTime )
            if ( r.endTime == (*p).endTime )
               finalResults.push_back( r );
      }
      else {
         finalResults = resultsStartTime;
      }
   }

   return nTracksSearched;       // can be used for screen reader messages if required
}

// Whether the two channels of a stereo track have the same clips
bool AudacityProject::ChannelsHaveSameClipBoundaries(const WaveTrack* wt)
{
   bool sameClips = false;

   if (wt->GetLinked() && wt->GetLink()) {
      auto& left = wt->GetClips();
      auto& right = static_cast<const WaveTrack*>(wt->GetLink())->GetClips();
      if (left.size() == right.size()) {
         sameClips = true;
         for (unsigned int i = 0; i < left.size(); i++) {
            if (left[i]->GetStartTime() != right[i]->GetStartTime() ||
               left[i]->GetEndTime() != right[i]->GetEndTime()) {
               sameClips = false;
               break;
            }
         }
      }
   }

   return sameClips;
}

void AudacityProject::OnSelectPrevClip(const CommandContext &WXUNUSED(context) )
{
   OnSelectClip(false);
}

void AudacityProject::OnSelectNextClip(const CommandContext &WXUNUSED(context) )
{
   OnSelectClip(true);
}

void AudacityProject::OnSelectClip(bool next)
{
   std::vector<FoundClip> results;
   FindClips(mViewInfo.selectedRegion.t0(),
      mViewInfo.selectedRegion.t1(), next, results);

   if (results.size() > 0) {
      // note that if there is more than one result, each has the same start
      // and end time
      double t0 = results[0].startTime;
      double t1 = results[0].endTime;
      mViewInfo.selectedRegion.setTimes(t0, t1);
      ModifyState();
      mTrackPanel->ScrollIntoView(mViewInfo.selectedRegion.t0());
      mTrackPanel->Refresh(false);

      // create and send message to screen reader
      wxString message;
      for (auto& result : results) {
         auto longName = result.ComposeTrackName();
         auto nClips = result.waveTrack->GetNumClips();
            /* i18n-hint: in the string after this one,
               first number identifies one of a sequence of clips,
               last number counts the clips,
               string names a track */
         _("dummyStringOnSelectClip");
         auto format = wxPLURAL(
            "%d of %d clip %s",
            "%d of %d clips %s",
            nClips
         );
         auto str = wxString::Format( format, result.index + 1, nClips, longName );

         if (message.empty())
            message = str;
         else
            message = wxString::Format(_("%s, %s"), message, str);
      }
      mTrackPanel->MessageForScreenReader(message);
   }
}

void AudacityProject::OnSelectCursorStoredCursor(const CommandContext &WXUNUSED(context) )
{
   if (mCursorPositionHasBeenStored) {
      double cursorPositionCurrent = IsAudioActive() ? gAudioIO->GetStreamTime() : mViewInfo.selectedRegion.t0();
      mViewInfo.selectedRegion.setTimes(std::min(cursorPositionCurrent, mCursorPositionStored),
         std::max(cursorPositionCurrent, mCursorPositionStored));

      ModifyState();
      mTrackPanel->Refresh(false);
   }
}

//
// View Menu
//

void AudacityProject::OnZoomIn(const CommandContext &WXUNUSED(context) )
{
   ZoomInByFactor( 2.0 );
}

double AudacityProject::GetScreenEndTime() const
{
   return mTrackPanel->GetScreenEndTime();
}

void AudacityProject::ZoomInByFactor( double ZoomFactor )
{
   // LLL: Handling positioning differently when audio is
   // actively playing.  Don't do this if paused.
   if ((gAudioIO->IsStreamActive(GetAudioIOToken()) != 0) && !gAudioIO->IsPaused()){
      ZoomBy(ZoomFactor);
      mTrackPanel->ScrollIntoView(gAudioIO->GetStreamTime());
      mTrackPanel->Refresh(false);
      return;
   }

   // DMM: Here's my attempt to get logical zooming behavior
   // when there's a selection that's currently at least
   // partially on-screen

   const double endTime = GetScreenEndTime();
   const double duration = endTime - mViewInfo.h;

   bool selectionIsOnscreen =
      (mViewInfo.selectedRegion.t0() < endTime) &&
      (mViewInfo.selectedRegion.t1() >= mViewInfo.h);

   bool selectionFillsScreen =
      (mViewInfo.selectedRegion.t0() < mViewInfo.h) &&
      (mViewInfo.selectedRegion.t1() > endTime);

   if (selectionIsOnscreen && !selectionFillsScreen) {
      // Start with the center of the selection
      double selCenter = (mViewInfo.selectedRegion.t0() +
                          mViewInfo.selectedRegion.t1()) / 2;

      // If the selection center is off-screen, pick the
      // center of the part that is on-screen.
      if (selCenter < mViewInfo.h)
         selCenter = mViewInfo.h +
                     (mViewInfo.selectedRegion.t1() - mViewInfo.h) / 2;
      if (selCenter > endTime)
         selCenter = endTime -
            (endTime - mViewInfo.selectedRegion.t0()) / 2;

      // Zoom in
      ZoomBy(ZoomFactor);
      const double newDuration = GetScreenEndTime() - mViewInfo.h;

      // Recenter on selCenter
      TP_ScrollWindow(selCenter - newDuration / 2);
      return;
   }


   double origLeft = mViewInfo.h;
   double origWidth = duration;
   ZoomBy(ZoomFactor);

   const double newDuration = GetScreenEndTime() - mViewInfo.h;
   double newh = origLeft + (origWidth - newDuration) / 2;

   // MM: Commented this out because it was confusing users
   /*
   // make sure that the *right-hand* end of the selection is
   // no further *left* than 1/3 of the way across the screen
   if (mViewInfo.selectedRegion.t1() < newh + mViewInfo.screen / 3)
      newh = mViewInfo.selectedRegion.t1() - mViewInfo.screen / 3;

   // make sure that the *left-hand* end of the selection is
   // no further *right* than 2/3 of the way across the screen
   if (mViewInfo.selectedRegion.t0() > newh + mViewInfo.screen * 2 / 3)
      newh = mViewInfo.selectedRegion.t0() - mViewInfo.screen * 2 / 3;
   */

   TP_ScrollWindow(newh);
}

void AudacityProject::OnZoomOut(const CommandContext &WXUNUSED(context) )
{
   ZoomOutByFactor( 1 /2.0 );
}


void AudacityProject::ZoomOutByFactor( double ZoomFactor )
{
   //Zoom() may change these, so record original values:
   const double origLeft = mViewInfo.h;
   const double origWidth = GetScreenEndTime() - origLeft;

   ZoomBy(ZoomFactor);
   const double newWidth = GetScreenEndTime() - mViewInfo.h;

   const double newh = origLeft + (origWidth - newWidth) / 2;
   // newh = (newh > 0) ? newh : 0;
   TP_ScrollWindow(newh);

}
void AudacityProject::OnZoomToggle(const CommandContext &WXUNUSED(context) )
{
//   const double origLeft = mViewInfo.h;
//   const double origWidth = GetScreenEndTime() - origLeft;

   // Choose the zoom that is most different to the current zoom.
   double Zoom1 = GetZoomOfPref(
      wxT("/GUI/ZoomPreset1"), WaveTrack::kZoomDefault );
   double Zoom2 = GetZoomOfPref(
      wxT("/GUI/ZoomPreset2"), WaveTrack::kZoom4To1 );
   double Z = mViewInfo.GetZoom();// Current Zoom.
   double ChosenZoom = abs(log(Zoom1 / Z)) > abs(log( Z / Zoom2)) ? Zoom1:Zoom2;

   Zoom(ChosenZoom);
   mTrackPanel->Refresh(false);
//   const double newWidth = GetScreenEndTime() - mViewInfo.h;
//   const double newh = origLeft + (origWidth - newWidth) / 2;
//   TP_ScrollWindow(newh);
}


void AudacityProject::OnZoomNormal(const CommandContext &WXUNUSED(context) )
{
   Zoom(ZoomInfo::GetDefaultZoom());
   mTrackPanel->Refresh(false);
}

void AudacityProject::OnZoomFit(const CommandContext &WXUNUSED(context) )
{
   const double start = mViewInfo.bScrollBeyondZero
      ? std::min(mTracks->GetStartTime(), 0.0)
      : 0;

   Zoom( GetZoomOfToFit() );
   TP_ScrollWindow(start);
}

void AudacityProject::DoZoomFitV()
{
   int height, count;

   mTrackPanel->GetTracksUsableArea(NULL, &height);

   height -= 28;

   count = 0;
   TrackListIterator iter(GetTracks());
   Track *t = iter.First();
   while (t) {
      if ((nullptr != dynamic_cast<AudioTrack*>(t)) &&
          !t->GetMinimized())
         count++;
      else
         height -= t->GetHeight();

      t = iter.Next();
   }

   if (count == 0)
      return;

   height = height / count;
   height = std::max( (int)TrackInfo::MinimumTrackHeight(), height );

   TrackListIterator iter2(GetTracks());
   t = iter2.First();
   while (t) {
      if ((nullptr != dynamic_cast<AudioTrack*>(t)) &&
          !t->GetMinimized())
         t->SetHeight(height);
      t = iter2.Next();
   }
}

void AudacityProject::OnZoomFitV(const CommandContext &WXUNUSED(context) )
{
   this->DoZoomFitV();

   mVsbar->SetThumbPosition(0);
   RedrawProject();
   ModifyState();
}

void AudacityProject::OnZoomSel(const CommandContext &WXUNUSED(context) )
{
   Zoom( GetZoomOfSelection() );
   TP_ScrollWindow(mViewInfo.selectedRegion.t0());
}

void AudacityProject::OnGoSelStart(const CommandContext &WXUNUSED(context) )
{
   if (mViewInfo.selectedRegion.isPoint())
      return;

   TP_ScrollWindow(mViewInfo.selectedRegion.t0() - ((GetScreenEndTime() - mViewInfo.h) / 2));
}

void AudacityProject::OnGoSelEnd(const CommandContext &WXUNUSED(context) )
{
   if (mViewInfo.selectedRegion.isPoint())
      return;

   TP_ScrollWindow(mViewInfo.selectedRegion.t1() - ((GetScreenEndTime() - mViewInfo.h) / 2));
}

void AudacityProject::OnShowClipping(const CommandContext &WXUNUSED(context) )
{
   bool checked = !gPrefs->Read(wxT("/GUI/ShowClipping"), 0L);
   gPrefs->Write(wxT("/GUI/ShowClipping"), checked);
   gPrefs->Flush();
   mCommandManager.Check(wxT("ShowClipping"), checked);
   mTrackPanel->UpdatePrefs();
   mTrackPanel->Refresh(false);
}

void AudacityProject::OnShowExtraMenus(const CommandContext &WXUNUSED(context) )
{
   bool checked = !gPrefs->Read(wxT("/GUI/ShowExtraMenus"), 0L);
   gPrefs->Write(wxT("/GUI/ShowExtraMenus"), checked);
   gPrefs->Flush();
   mCommandManager.Check(wxT("ShowExtraMenus"), checked);
   RebuildAllMenuBars();
}

void AudacityProject::OnApplyMacroDirectly(const CommandContext &context )
{
   //wxLogDebug( "Macro was: %s", context.parameter);
   ApplyMacroDialog dlg(this);
   wxString Name = context.parameter;

// We used numbers previously, but macros could get renumbered, making
// macros containing macros unpredictable.
#ifdef MACROS_BY_NUMBERS
   long item=0;
   // Take last three letters (of e.g. Macro007) and convert to a number.
   Name.Mid( Name.Length() - 3 ).ToLong( &item, 10 );
   dlg.ApplyMacroToProject( item, false );
#else
   dlg.ApplyMacroToProject( Name, false );
#endif
   ModifyUndoMenuItems();
}

void AudacityProject::OnApplyMacrosPalette(const CommandContext &WXUNUSED(context) )
{
   const bool bExpanded = false;
   if (!mMacrosWindow)
      mMacrosWindow = safenew MacrosWindow(this, bExpanded);
   mMacrosWindow->Show();
   mMacrosWindow->Raise();
   mMacrosWindow->UpdateDisplay( bExpanded);
}

void AudacityProject::OnManageMacros(const CommandContext &WXUNUSED(context) )
{
   const bool bExpanded = true;
   if (!mMacrosWindow)
      mMacrosWindow = safenew MacrosWindow(this, bExpanded);
   mMacrosWindow->Show();
   mMacrosWindow->Raise();
   mMacrosWindow->UpdateDisplay( bExpanded);
}

void AudacityProject::OnHistory(const CommandContext &WXUNUSED(context) )
{
   if (!mHistoryWindow)
      mHistoryWindow = safenew HistoryWindow(this, GetUndoManager());
   mHistoryWindow->Show();
   mHistoryWindow->Raise();
   mHistoryWindow->UpdateDisplay();
}

void AudacityProject::OnShowTransportToolBar(const CommandContext &WXUNUSED(context) )
{
   mToolManager->ShowHide(TransportBarID);
}

void AudacityProject::OnShowDeviceToolBar(const CommandContext &WXUNUSED(context) )
{
   mToolManager->ShowHide( DeviceBarID );
}

void AudacityProject::OnShowEditToolBar(const CommandContext &WXUNUSED(context) )
{
   mToolManager->ShowHide( EditBarID );
}

void AudacityProject::OnShowMeterToolBar(const CommandContext &WXUNUSED(context) )
{
   if( !mToolManager->IsVisible( MeterBarID ) )
   {
      mToolManager->Expose( PlayMeterBarID, false );
      mToolManager->Expose( RecordMeterBarID, false );
   }
   mToolManager->ShowHide( MeterBarID );
}

void AudacityProject::OnShowRecordMeterToolBar(const CommandContext &WXUNUSED(context) )
{
   if( !mToolManager->IsVisible( RecordMeterBarID ) )
   {
      mToolManager->Expose( MeterBarID, false );
   }
   mToolManager->ShowHide( RecordMeterBarID );
}

void AudacityProject::OnShowPlayMeterToolBar(const CommandContext &WXUNUSED(context) )
{
   if( !mToolManager->IsVisible( PlayMeterBarID ) )
   {
      mToolManager->Expose( MeterBarID, false );
   }
   mToolManager->ShowHide( PlayMeterBarID );
}

void AudacityProject::OnShowSelectionToolBar(const CommandContext &WXUNUSED(context) )
{
   mToolManager->ShowHide( SelectionBarID );
}

void AudacityProject::OnResetToolBars(const CommandContext &WXUNUSED(context) )
{
   mToolManager->Reset();
}

//
// Project Menu
//

void AudacityProject::OnImport(const CommandContext &WXUNUSED(context) )
{
   // An import trigger for the alias missing dialog might not be intuitive, but
   // this serves to track the file if the users zooms in and such.
   wxGetApp().SetMissingAliasedFileWarningShouldShow(true);

   wxArrayString selectedFiles = ShowOpenDialog(wxT(""));
   if (selectedFiles.GetCount() == 0) {
      gPrefs->Write(wxT("/LastOpenType"),wxT(""));
      gPrefs->Flush();
      return;
   }

   // PRL:  This affects FFmpegImportPlugin::Open which resets the preference
   // to false.  Should it also be set to true on other paths that reach
   // AudacityProject::Import ?
   gPrefs->Write(wxT("/NewImportingSession"), true);

   //sort selected files by OD status.  Load non OD first so user can edit asap.
   //first sort selectedFiles.
   selectedFiles.Sort(CompareNoCaseFileName);
   ODManager::Pauser pauser;

   auto cleanup = finally( [&] {
      gPrefs->Write(wxT("/LastOpenType"),wxT(""));

      gPrefs->Flush();

      HandleResize(); // Adjust scrollers for NEW track sizes.
   } );

   for (size_t ff = 0; ff < selectedFiles.GetCount(); ff++) {
      wxString fileName = selectedFiles[ff];

      FileNames::UpdateDefaultPath(FileNames::Operation::Open, fileName);

      Import(fileName);
   }

   ZoomAfterImport(nullptr);
}

void AudacityProject::OnImportRaw(const CommandContext &WXUNUSED(context) )
{
   wxString fileName =
       FileNames::SelectFile(FileNames::Operation::Open,
                    _("Select any uncompressed audio file"),
                    wxEmptyString,     // Path
                    wxT(""),       // Name
                    wxT(""),       // Extension
                    _("All files|*"),
                    wxRESIZE_BORDER,        // Flags
                    this);    // Parent

   if (fileName == wxT(""))
      return;

   TrackHolders newTracks;

   ::ImportRaw(this, fileName, GetTrackFactory(), newTracks);

   if (newTracks.size() <= 0)
      return;

   AddImportedTracks(fileName, std::move(newTracks));
   HandleResize(); // Adjust scrollers for NEW track sizes.
}

void AudacityProject::OnCursorPositionStore(const CommandContext &WXUNUSED(context) )
{
   mCursorPositionStored = IsAudioActive() ? gAudioIO->GetStreamTime() : mViewInfo.selectedRegion.t0();
   mCursorPositionHasBeenStored = true;
}

void AudacityProject::OnCursorTrackStart(const CommandContext &WXUNUSED(context) )
{
   double kWayOverToRight = 1000000.0;
   double minOffset = kWayOverToRight;

   TrackListIterator iter(GetTracks());
   Track *t = iter.First();

   while (t) {
      if (t->GetSelected()) {
         if (t->GetOffset() < minOffset)
            minOffset = t->GetOffset();
      }

      t = iter.Next();
   }

   if( minOffset >= (kWayOverToRight-1) )
      return;

   if (minOffset < 0.0) minOffset = 0.0;
   mViewInfo.selectedRegion.setTimes(minOffset, minOffset);
   ModifyState();
   mTrackPanel->ScrollIntoView(mViewInfo.selectedRegion.t0());
   mTrackPanel->Refresh(false);
}

void AudacityProject::OnCursorTrackEnd(const CommandContext &WXUNUSED(context) )
{
   double kWayOverToLeft = -1000000.0;
   double maxEndOffset = kWayOverToLeft;
   double thisEndOffset = 0.0;

   TrackListIterator iter(GetTracks());
   Track *t = iter.First();

   while (t) {
      if (t->GetSelected()) {
         thisEndOffset = t->GetEndTime();
         if (thisEndOffset > maxEndOffset)
            maxEndOffset = thisEndOffset;
      }

      t = iter.Next();
   }

   if( maxEndOffset < (kWayOverToLeft +1) )
      return;

   mViewInfo.selectedRegion.setTimes(maxEndOffset, maxEndOffset);
   ModifyState();
   mTrackPanel->ScrollIntoView(mViewInfo.selectedRegion.t1());
   mTrackPanel->Refresh(false);
}

void AudacityProject::OnCursorSelStart(const CommandContext &WXUNUSED(context) )
{
   mViewInfo.selectedRegion.collapseToT0();
   ModifyState();
   mTrackPanel->ScrollIntoView(mViewInfo.selectedRegion.t0());
   mTrackPanel->Refresh(false);
}

void AudacityProject::OnCursorSelEnd(const CommandContext &WXUNUSED(context) )
{
   mViewInfo.selectedRegion.collapseToT1();
   ModifyState();
   mTrackPanel->ScrollIntoView(mViewInfo.selectedRegion.t1());
   mTrackPanel->Refresh(false);
}

AudacityProject::FoundClipBoundary AudacityProject::FindNextClipBoundary(const WaveTrack* wt, double time)
{
   AudacityProject::FoundClipBoundary result{};
   result.waveTrack = wt;
   const auto clips = wt->SortedClipArray();
   double timeStart = AdjustForFindingStartTimes(clips, time);
   double timeEnd = AdjustForFindingEndTimes(clips, time);

   auto pStart = std::find_if(clips.begin(), clips.end(), [&] (const WaveClip* const& clip) {
      return clip->GetStartTime() > timeStart; });
   auto pEnd = std::find_if(clips.begin(), clips.end(), [&] (const WaveClip* const& clip) {
      return clip->GetEndTime() > timeEnd; });

   if (pStart != clips.end() && pEnd != clips.end()) {
      if ((*pEnd)->SharesBoundaryWithNextClip(*pStart)) {
         // boundary between two clips which are immediately next to each other.
         result.nFound = 2;
         result.time = (*pEnd)->GetEndTime();
         result.index1 = std::distance(clips.begin(), pEnd);
         result.clipStart1 = false;
         result.index2 = std::distance(clips.begin(), pStart);
         result.clipStart2 = true;
      }
      else if ((*pStart)->GetStartTime() < (*pEnd)->GetEndTime()) {
         result.nFound = 1;
         result.time = (*pStart)->GetStartTime();
         result.index1 = std::distance(clips.begin(), pStart);
         result.clipStart1 = true;
      }
      else  {
         result.nFound = 1;
         result.time = (*pEnd)->GetEndTime();
         result.index1 = std::distance(clips.begin(), pEnd);
         result.clipStart1 = false;
      }
   }
   else if (pEnd != clips.end()) {
      result.nFound = 1;
      result.time = (*pEnd)->GetEndTime();
      result.index1 = std::distance(clips.begin(), pEnd);
      result.clipStart1 = false;
   }

   return result;
}

AudacityProject::FoundClipBoundary AudacityProject::FindPrevClipBoundary(const WaveTrack* wt, double time)
{
   AudacityProject::FoundClipBoundary result{};
   result.waveTrack = wt;
   const auto clips = wt->SortedClipArray();
   double timeStart = AdjustForFindingStartTimes(clips, time);
   double timeEnd = AdjustForFindingEndTimes(clips, time);

   auto pStart = std::find_if(clips.rbegin(), clips.rend(), [&] (const WaveClip* const& clip) {
      return clip->GetStartTime() < timeStart; });
   auto pEnd = std::find_if(clips.rbegin(), clips.rend(), [&] (const WaveClip* const& clip) {
      return clip->GetEndTime() < timeEnd; });

   if (pStart != clips.rend() && pEnd != clips.rend()) {
      if ((*pEnd)->SharesBoundaryWithNextClip(*pStart)) {
         // boundary between two clips which are immediately next to each other.
         result.nFound = 2;
         result.time = (*pStart)->GetStartTime();
         result.index1 = static_cast<int>(clips.size()) - 1 - std::distance(clips.rbegin(), pStart);
         result.clipStart1 = true;
         result.index2 = static_cast<int>(clips.size()) - 1 - std::distance(clips.rbegin(), pEnd);
         result.clipStart2 = false;
      }
      else if ((*pStart)->GetStartTime() > (*pEnd)->GetEndTime()) {
         result.nFound = 1;
         result.time = (*pStart)->GetStartTime();
         result.index1 = static_cast<int>(clips.size()) - 1 - std::distance(clips.rbegin(), pStart);
         result.clipStart1 = true;
      }
      else {
         result.nFound = 1;
         result.time = (*pEnd)->GetEndTime();
         result.index1 = static_cast<int>(clips.size()) - 1 - std::distance(clips.rbegin(), pEnd);
         result.clipStart1 = false;
      }
   }
   else if (pStart != clips.rend()) {
      result.nFound = 1;
      result.time = (*pStart)->GetStartTime();
      result.index1 = static_cast<int>(clips.size()) - 1 - std::distance(clips.rbegin(), pStart);
      result.clipStart1 = true;
   }

   return result;
}

// When two clips are immediately next to each other, the GetEndTime() of the first clip and the
// GetStartTime() of the second clip may not be exactly equal due to rounding errors. When searching
// for the next/prev start time from a given time, the following function adjusts that given time if
// necessary to take this into account. If the given time is the end time of the first of two clips which
// are next to each other, then the given time is changed to the start time of the second clip.
// This ensures that the correct next/prev start time is found.
double AudacityProject::AdjustForFindingStartTimes(const std::vector<const WaveClip*> & clips, double time)
{
   auto q = std::find_if(clips.begin(), clips.end(), [&] (const WaveClip* const& clip) {
      return clip->GetEndTime() == time; });
   if (q != clips.end() && q + 1 != clips.end() &&
      (*q)->SharesBoundaryWithNextClip(*(q+1))) {
      time = (*(q+1))->GetStartTime();
   }

   return time;
}

// When two clips are immediately next to each other, the GetEndTime() of the first clip and the
// GetStartTime() of the second clip may not be exactly equal due to rounding errors. When searching
// for the next/prev end time from a given time, the following function adjusts that given time if
// necessary to take this into account. If the given time is the start time of the second of two clips which
// are next to each other, then the given time is changed to the end time of the first clip.
// This ensures that the correct next/prev end time is found.
double AudacityProject::AdjustForFindingEndTimes(const std::vector<const WaveClip*>& clips, double time)
{
   auto q = std::find_if(clips.begin(), clips.end(), [&] (const WaveClip* const& clip) {
      return clip->GetStartTime() == time; });
   if (q != clips.end() && q != clips.begin() &&
      (*(q - 1))->SharesBoundaryWithNextClip(*q)) {
      time = (*(q-1))->GetEndTime();
   }

   return time;
}

int AudacityProject::FindClipBoundaries(double time, bool next, std::vector<FoundClipBoundary>& finalResults)
{
   const TrackList* tracks = GetTracks();
   finalResults.clear();

   bool anyWaveTracksSelected =
      tracks->end() != std::find_if(tracks->begin(), tracks->end(),
         [] (const Track *t) {
            return t->GetSelected() && t->GetKind() == Track::Wave; });


   // first search the tracks individually

   TrackListIterator iter(GetTracks());
   Track* track = iter.First();
   std::vector<FoundClipBoundary> results;

   int nTracksSearched = 0;
   int trackNum = 1;
   while (track) {
      if (track->GetKind() == Track::Wave && (!anyWaveTracksSelected || track->GetSelected())) {
         auto waveTrack = static_cast<const WaveTrack*>(track);
         bool stereoAndDiff = waveTrack->GetLinked() && !ChannelsHaveSameClipBoundaries(waveTrack);

         auto result = next ? FindNextClipBoundary(waveTrack, time) :
            FindPrevClipBoundary(waveTrack, time);
         if (result.nFound > 0) {
            result.trackNum = trackNum;
            result.channel = stereoAndDiff;
            results.push_back(result);
         }
         if (stereoAndDiff) {
            auto waveTrack2 = static_cast<const WaveTrack*>(track->GetLink());
            auto result = next ? FindNextClipBoundary(waveTrack2, time) :
               FindPrevClipBoundary(waveTrack2, time);
            if (result.nFound > 0) {
               result.trackNum = trackNum;
               result.channel = stereoAndDiff;
               results.push_back(result);
            }
         }

         nTracksSearched++;
      }

      trackNum++;
      track = iter.Next(true);
   }


   if (results.size() > 0) {
      // If any clip boundaries were found
      // find the clip boundary or boundaries with the min/max time
      auto compare = [] (const FoundClipBoundary& a, const FoundClipBoundary&b)
         { return a.time < b.time; };

      auto p = next ? min_element(results.begin(), results.end(), compare ) :
         max_element(results.begin(), results.end(), compare);

      for ( auto &r : results )
         if ( r.time == (*p).time )
            finalResults.push_back( r );
   }

   return nTracksSearched;          // can be used for screen reader messages if required
}


void AudacityProject::OnCursorNextClipBoundary(const CommandContext &WXUNUSED(context) )
{
   OnCursorClipBoundary(true);
}

void AudacityProject::OnCursorPrevClipBoundary(const CommandContext &WXUNUSED(context) )
{
   OnCursorClipBoundary(false);
}

void AudacityProject::OnCursorClipBoundary(bool next)
{
   std::vector<FoundClipBoundary> results;
   FindClipBoundaries(next ? mViewInfo.selectedRegion.t1() :
      mViewInfo.selectedRegion.t0(), next, results);

   if (results.size() > 0) {
      // note that if there is more than one result, each has the same time value.
      double time = results[0].time;
      mViewInfo.selectedRegion.setTimes(time, time);
      ModifyState();
      mTrackPanel->ScrollIntoView(mViewInfo.selectedRegion.t0());
      mTrackPanel->Refresh(false);

      wxString message = ClipBoundaryMessage(results);
      mTrackPanel->MessageForScreenReader(message);
   }
}

wxString AudacityProject::FoundTrack::ComposeTrackName() const
{
   auto name = waveTrack->GetName();
   auto shortName = name == waveTrack->GetDefaultName()
      /* i18n-hint: compose a name identifying an unnamed track by number */
      ? wxString::Format( _("Track %d"), trackNum )
      : name;
   auto longName = shortName;
   if (channel) {
      if ( waveTrack->GetLinked() )
      /* i18n-hint: given the name of a track, specify its left channel */
         longName = wxString::Format(_("%s left"), shortName);
      else
      /* i18n-hint: given the name of a track, specify its right channel */
         longName = wxString::Format(_("%s right"), shortName);
   }
   return longName;
}

// for clip boundary commands, create a message for screen readers
wxString AudacityProject::ClipBoundaryMessage(const std::vector<FoundClipBoundary>& results)
{
   wxString message;
   for (auto& result : results) {

      auto longName = result.ComposeTrackName();

      wxString str;
      auto nClips = result.waveTrack->GetNumClips();
      if (result.nFound < 2) {
            /* i18n-hint: in the string after this one,
               First %s is replaced with the noun "start" or "end"
               identifying one end of a clip,
               first number gives the position of that clip in a sequence
               of clips,
               last number counts all clips,
               and the last string is the name of the track containing the clips.
             */
         _("dummyStringClipBoundaryMessage");
         auto format = wxPLURAL(
            "%s %d of %d clip %s",
            "%s %d of %d clips %s",
            nClips
         );
         str = wxString::Format(format,
            result.clipStart1 ? _("start") : _("end"),
            result.index1 + 1,
            nClips,
            longName
         );
      }
      else {
            /* i18n-hint: in the string after this one,
               First two %s are each replaced with the noun "start"
               or with "end", identifying and end of a clip,
               first and second numbers give the position of those clips in
               a seqeunce of clips,
               last number counts all clips,
               and the last string is the name of the track containing the clips.
             */
         _("dummyStringClipBoundaryMessageLong");
         auto format = wxPLURAL(
            "%s %d and %s %d of %d clip %s",
            "%s %d and %s %d of %d clips %s",
            nClips
         );
         str = wxString::Format(format,
            result.clipStart1 ? _("start") : _("end"),
            result.index1 + 1,
            result.clipStart2 ? _("start") : _("end"),
            result.index2 + 1,
            nClips,
            longName
         );
      }

      if (message.empty())
         message = str;
      else
         message = wxString::Format(_("%s, %s"), message, str);
   }

   return message;
}

void AudacityProject::OnNewStereoTrack(const CommandContext &WXUNUSED(context) )
{
   auto t = mTracks->Add(mTrackFactory->NewWaveTrack(mDefaultFormat, mRate));
   t->SetChannel(Track::LeftChannel);
   SelectNone();

   t->SetSelected(true);
   t->SetLinked (true);

   t = mTracks->Add(mTrackFactory->NewWaveTrack(mDefaultFormat, mRate));
   t->SetChannel(Track::RightChannel);

   t->SetSelected(true);

   PushState(_("Created new stereo audio track"), _("New Track"));

   RedrawProject();
   mTrackPanel->EnsureVisible(t);
}

void AudacityProject::OnSoundActivated(const CommandContext &WXUNUSED(context) )
{
   SoundActivatedRecord dialog(this /* parent */ );
   dialog.ShowModal();
}

void AudacityProject::OnRescanDevices(const CommandContext &WXUNUSED(context) )
{
   DeviceManager::Instance()->Rescan();
}

void AudacityProject::OnRemoveTracks(const CommandContext &WXUNUSED(context) )
{
   TrackListIterator iter(GetTracks());
   Track *t = iter.First();
   Track *f = NULL;
   Track *l = NULL;

   while (t) {
      if (t->GetSelected()) {
         if (!f)
            f = l;         // Capture the track preceeding the first removed track
         t = iter.RemoveCurrent();
      }
      else {
         l = t;
         t = iter.Next();
      }
   }

   // All tracks but the last were removed...try to use the last track
   if (!f)
      f = l;

   // Try to use the first track after the removal or, if none,
   // the track preceeding the removal
   if (f) {
      t = mTracks->GetNext(f, true);
      if (t)
         f = t;
   }

   // If we actually have something left, then make sure it's seen
   if (f)
      mTrackPanel->EnsureVisible(f);

   PushState(_("Removed audio track(s)"), _("Remove Track"));

   mTrackPanel->UpdateViewIfNoTracks();
   mTrackPanel->Refresh(false);
}

//
// Help Menu
//

void AudacityProject::OnAbout(const CommandContext &WXUNUSED(context) )
{
#ifdef __WXMAC__
   // Modeless dialog, consistent with other Mac applications
   wxCommandEvent dummy;
   wxGetApp().OnMenuAbout(dummy);
#else
   // Windows and Linux still modal.
   AboutDialog dlog(this);
   dlog.ShowModal();
#endif
}

void AudacityProject::OnHelpWelcome(const CommandContext &WXUNUSED(context) )
{
   SplashDialog::Show2( this );
}

void AudacityProject::OnQuickHelp(const CommandContext &WXUNUSED(context) )
{
   HelpSystem::ShowHelp(
      this,
      wxT("Quick_Help"));
}

void AudacityProject::OnManual(const CommandContext &WXUNUSED(context) )
{
   HelpSystem::ShowHelp(
      this,
      wxT("Main_Page"));
}

void AudacityProject::OnShowLog(const CommandContext &WXUNUSED(context) )
{
   AudacityLogger *logger = wxGetApp().GetLogger();
   if (logger) {
      logger->Show();
   }
}

void AudacityProject::OnBenchmark(const CommandContext &WXUNUSED(context) )
{
   ::RunBenchmark(this);
}

void AudacityProject::OnDetectUpstreamDropouts(const CommandContext &WXUNUSED(context) )
{
   bool &setting = gAudioIO->mDetectUpstreamDropouts;
   mCommandManager.Check(wxT("DetectUpstreamDropouts"), !setting);
   setting = !setting;
}

void AudacityProject::OnAudioDeviceInfo(const CommandContext &WXUNUSED(context) )
{
   wxString info = gAudioIO->GetDeviceInfo();

   wxDialogWrapper dlg(this, wxID_ANY, wxString(_("Audio Device Info")));
   dlg.SetName(dlg.GetTitle());
   ShuttleGui S(&dlg, eIsCreating);

   wxTextCtrl *text;
   S.StartVerticalLay();
   {
      S.SetStyle(wxTE_MULTILINE | wxTE_READONLY);
      text = S.Id(wxID_STATIC).AddTextWindow(info);
      S.AddStandardButtons(eOkButton | eCancelButton);
   }
   S.EndVerticalLay();

   dlg.FindWindowById(wxID_OK)->SetLabel(_("&Save"));
   dlg.SetSize(350, 450);

   if (dlg.ShowModal() == wxID_OK)
   {
      wxString fName = FileNames::SelectFile(FileNames::Operation::Export,
                                    _("Save Device Info"),
                                    wxEmptyString,
                                    wxT("deviceinfo.txt"),
                                    wxT("txt"),
                                    wxT("*.txt"),
                                    wxFD_SAVE | wxFD_OVERWRITE_PROMPT | wxRESIZE_BORDER,
                                    this);
      if (!fName.IsEmpty())
      {
         if (!text->SaveFile(fName))
         {
            AudacityMessageBox(_("Unable to save device info"), _("Save Device Info"));
         }
      }
   }
}

void AudacityProject::OnSeparator(const CommandContext &WXUNUSED(context) )
{

}

void AudacityProject::OnCollapseAllTracks(const CommandContext &WXUNUSED(context) )
{
   TrackListIterator iter(GetTracks());
   Track *t = iter.First();

   while (t)
   {
      t->SetMinimized(true);
      t = iter.Next();
   }

   ModifyState();
   RedrawProject();
}

void AudacityProject::OnExpandAllTracks(const CommandContext &WXUNUSED(context) )
{
   TrackListIterator iter(GetTracks());
   Track *t = iter.First();

   while (t)
   {
      t->SetMinimized(false);
      t = iter.Next();
   }

   ModifyState();
   RedrawProject();
}

void AudacityProject::OnLockPlayRegion(const CommandContext &WXUNUSED(context) )
{
   double start, end;
   GetPlayRegion(&start, &end);
   if (start >= mTracks->GetEndTime()) {
       AudacityMessageBox(_("Cannot lock region beyond\nend of project."),
                    _("Error"));
   }
   else {
      mLockPlayRegion = true;
      mRuler->Refresh(false);
   }
}

void AudacityProject::OnUnlockPlayRegion(const CommandContext &WXUNUSED(context) )
{
   mLockPlayRegion = false;
   mRuler->Refresh(false);
}

void AudacityProject::OnResample(const CommandContext &WXUNUSED(context) )
{
   TrackListIterator iter(GetTracks());

   int newRate;

   while (true)
   {
      wxDialogWrapper dlg(this, wxID_ANY, wxString(_("Resample")));
      dlg.SetName(dlg.GetTitle());
      ShuttleGui S(&dlg, eIsCreating);
      wxString rate;
      wxArrayString rates;
      wxComboBox *cb;

      rate.Printf(wxT("%ld"), lrint(mRate));

      rates.Add(wxT("8000"));
      rates.Add(wxT("11025"));
      rates.Add(wxT("16000"));
      rates.Add(wxT("22050"));
      rates.Add(wxT("32000"));
      rates.Add(wxT("44100"));
      rates.Add(wxT("48000"));
      rates.Add(wxT("88200"));
      rates.Add(wxT("96000"));
      rates.Add(wxT("176400"));
      rates.Add(wxT("192000"));
      rates.Add(wxT("352800"));
      rates.Add(wxT("384000"));

      S.StartVerticalLay(true);
      {
         S.AddSpace(-1, 15);

         S.StartHorizontalLay(wxCENTER, false);
         {
            cb = S.AddCombo(_("New sample rate (Hz):"),
                            rate,
                            &rates);
         }
         S.EndHorizontalLay();

         S.AddSpace(-1, 15);

         S.AddStandardButtons();
      }
      S.EndVerticalLay();

      dlg.Layout();
      dlg.Fit();
      dlg.Center();

      if (dlg.ShowModal() != wxID_OK)
      {
         return;  // user cancelled dialog
      }

      long lrate;
      if (cb->GetValue().ToLong(&lrate) && lrate >= 1 && lrate <= 1000000)
      {
         newRate = (int)lrate;
         break;
      }

      AudacityMessageBox(_("The entered value is invalid"), _("Error"),
                   wxICON_ERROR, this);
   }

   int ndx = 0;
   auto flags = UndoPush::AUTOSAVE;
   for (Track *t = iter.First(); t; t = iter.Next())
   {
      wxString msg;

      msg.Printf(_("Resampling track %d"), ++ndx);

      ProgressDialog progress(_("Resample"), msg);

      if (t->GetSelected() && t->GetKind() == Track::Wave) {
         // The resampling of a track may be stopped by the user.  This might
         // leave a track with multiple clips in a partially resampled state.
         // But the thrown exception will cause rollback in the application
         // level handler.

         ((WaveTrack*)t)->Resample(newRate, &progress);

         // Each time a track is successfully, completely resampled,
         // commit that to the undo stack.  The second and later times,
         // consolidate.

         PushState(_("Resampled audio track(s)"), _("Resample Track"), flags);
         flags = flags | UndoPush::CONSOLIDATE;
      }
   }

   GetUndoManager()->StopConsolidating();
   RedrawProject();

   // Need to reset
   FinishAutoScroll();
}

void AudacityProject::OnFullScreen(const CommandContext &WXUNUSED(context) )
{
   bool bChecked = !wxTopLevelWindow::IsFullScreen();
   wxTopLevelWindow::ShowFullScreen(bChecked);
   mCommandManager.Check(wxT("FullScreenOnOff"), bChecked);
}

// Handle small cursor and play head movements
void AudacityProject::SeekLeftOrRight
(double direction, SelectionOperation operation)
{
   // PRL:  What I found and preserved, strange though it be:
   // During playback:  jump depends on preferences and is independent of the zoom
   // and does not vary if the key is held
   // Else: jump depends on the zoom and gets bigger if the key is held

   if( IsAudioActive() )
   {
      if( operation == CURSOR_MOVE )
         SeekWhenAudioActive(mSeekShort * direction);
      else if( operation == SELECTION_EXTEND )
         SeekWhenAudioActive(mSeekLong * direction);
      // Note: no action for CURSOR_CONTRACT
      return;
   }

   // If the last adjustment was very recent, we are
   // holding the key down and should move faster.
   const wxLongLong curtime = ::wxGetLocalTimeMillis();
   enum { MIN_INTERVAL = 50 };
   const bool fast = (curtime - mLastSelectionAdjustment < MIN_INTERVAL);

   mLastSelectionAdjustment = curtime;

   // How much faster should the cursor move if shift is down?
   enum { LARGER_MULTIPLIER = 4 };
   const double seekStep = (fast ? LARGER_MULTIPLIER : 1.0) * direction;

   SeekWhenAudioInactive( seekStep, TIME_UNIT_PIXELS, operation);
}

void AudacityProject::SeekWhenAudioActive(double seekStep)
{
   mLastSelectionAdjustment = ::wxGetLocalTimeMillis();

   gAudioIO->SeekStream(seekStep);
}


void AudacityProject::OnBoundaryMove(int step)
{
   // step is negative, then is moving left.  step positive, moving right.
   // Move the left/right selection boundary, to expand the selection

   // If the last adjustment was very recent, we are
   // holding the key down and should move faster.
   wxLongLong curtime = ::wxGetLocalTimeMillis();
   int pixels = step;
   if( curtime - mLastSelectionAdjustment < 50 )
   {
      pixels *= 4;
   }
   mLastSelectionAdjustment = curtime;

   // we used to have a parameter boundaryContract to say if expanding or contracting.
   // it is no longer needed.
   bool bMoveT0 = (step < 0 );// ^ boundaryContract ;

   if( IsAudioActive() )
   {
      double indicator = gAudioIO->GetStreamTime();
      if( bMoveT0 )
         mViewInfo.selectedRegion.setT0(indicator, false);
      else
         mViewInfo.selectedRegion.setT1(indicator);

      ModifyState();
      GetTrackPanel()->Refresh(false);
      return;
   }

   const double t0 = mViewInfo.selectedRegion.t0();
   const double t1 = mViewInfo.selectedRegion.t1();
   const double end = mTracks->GetEndTime();

   double newT = mViewInfo.OffsetTimeByPixels( bMoveT0 ? t0 : t1, pixels);
   // constrain to be in the track limits.
   newT = std::max( 0.0, newT );
   newT = std::min( newT, end);
   // optionally constrain to be a contraction, i.e. so t0/t1 do not cross over
   //if( boundaryContract )
   //   newT = bMoveT0 ? std::min( t1, newT ) : std::max( t0, newT );

   // Actually move
   if( bMoveT0 )
      mViewInfo.selectedRegion.setT0( newT );
   else 
      mViewInfo.selectedRegion.setT1( newT );

   // Ensure it is visible, and refresh.
   GetTrackPanel()->ScrollIntoView(newT);
   GetTrackPanel()->Refresh(false);

   ModifyState();
}

void AudacityProject::SeekWhenAudioInactive
(double seekStep, TimeUnit timeUnit,
SelectionOperation operation)
{
   if( operation == CURSOR_MOVE )
   {
      MoveWhenAudioInactive( seekStep, timeUnit);
      return;
   }

   int snapToTime = GetSnapTo();
   const double t0 = mViewInfo.selectedRegion.t0();
   const double t1 = mViewInfo.selectedRegion.t1();
   const double end = mTracks->GetEndTime();

   // Is it t0 or t1 moving?
   bool bMoveT0 = ( operation == SELECTION_CONTRACT ) ^ ( seekStep < 0 );
   // newT is where we want to move to
   double newT = OffsetTime( bMoveT0 ? t0 : t1, seekStep, timeUnit, snapToTime);
   // constrain to be in the track limits.
   newT = std::max( 0.0, newT );
   newT = std::min( newT, end);
   // optionally constrain to be a contraction, i.e. so t0/t1 do not cross over
   if( operation == SELECTION_CONTRACT )
      newT = bMoveT0 ? std::min( t1, newT ) : std::max( t0, newT );

   // Actually move
   if( bMoveT0 )
      mViewInfo.selectedRegion.setT0( newT );
   else 
      mViewInfo.selectedRegion.setT1( newT );

   // Ensure it is visible, and refresh.
   GetTrackPanel()->ScrollIntoView(newT);
   GetTrackPanel()->Refresh(false);
}

// Moving a cursor, and collapsed selection.
void AudacityProject::MoveWhenAudioInactive
(double seekStep, TimeUnit timeUnit)
{
   // If TIME_UNIT_SECONDS, snap-to will be off.
   int snapToTime = GetSnapTo();
   const double t0 = mViewInfo.selectedRegion.t0();
   const double end = mTracks->GetEndTime();

   // Move the cursor
   // Already in cursor mode?
   if( mViewInfo.selectedRegion.isPoint() )
   {
      double newT = OffsetTime(t0, seekStep, timeUnit, snapToTime);
      // constrain.
      newT = std::max(0.0, newT);
      newT = std::min(newT, end);
      // Move 
      mViewInfo.selectedRegion.setT0(
         newT,
         false); // do not swap selection boundaries
      mViewInfo.selectedRegion.collapseToT0();

      // Move the visual cursor, avoiding an unnecessary complete redraw
      GetTrackPanel()->DrawOverlays(false);
      GetRulerPanel()->DrawOverlays(false);

      // This updates the selection shown on the selection bar, and the play region
      TP_DisplaySelection();
   } else
   {
      // Transition to cursor mode.
      if( seekStep < 0 )
         mViewInfo.selectedRegion.collapseToT0();
      else
         mViewInfo.selectedRegion.collapseToT1();
      GetTrackPanel()->Refresh(false);
   }

   // Make sure NEW position is in view
   GetTrackPanel()->ScrollIntoView(mViewInfo.selectedRegion.t1());
   return;
}

double AudacityProject::OffsetTime
(double t, double offset, TimeUnit timeUnit, int snapToTime)
{
    if (timeUnit == TIME_UNIT_SECONDS)
        return t + offset; // snapping is currently ignored for non-pixel moves

    if (snapToTime == SNAP_OFF)
        return mViewInfo.OffsetTimeByPixels(t, (int)offset);

    return GridMove(t, (int)offset);
}

// Handles moving a selection edge with the keyboard in snap-to-time mode;
// returns the moved value.
// Will move at least minPix pixels -- set minPix positive to move forward,
// negative to move backward.
double AudacityProject::GridMove(double t, int minPix)
{
   NumericConverter nc(NumericConverter::TIME, GetSelectionFormat(), t, GetRate());

   // Try incrementing/decrementing the value; if we've moved far enough we're
   // done
   double result;
   minPix >= 0 ? nc.Increment() : nc.Decrement();
   result = nc.GetValue();
   if (std::abs(mViewInfo.TimeToPosition(result) - mViewInfo.TimeToPosition(t))
       >= abs(minPix))
       return result;

   // Otherwise, move minPix pixels, then snap to the time.
   result = mViewInfo.OffsetTimeByPixels(t, minPix);
   nc.SetValue(result);
   result = nc.GetValue();
   return result;
}


// Move the cursor forward or backward, while paused or while playing.
void AudacityProject::OnCursorMove(double seekStep)
{
    if (IsAudioActive()) {
        SeekWhenAudioActive(seekStep);
    }
    else
    {
        mLastSelectionAdjustment = ::wxGetLocalTimeMillis();
        MoveWhenAudioInactive(seekStep, TIME_UNIT_SECONDS);
    }

   ModifyState();
}
