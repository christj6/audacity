/**********************************************************************

  Audacity: A Digital Audio Editor

  Menus.h

  Dominic Mazzoni

**********************************************************************/
#ifndef __AUDACITY_MENUS__
#define __AUDACITY_MENUS__

#include "Experimental.h"



// These are all member functions of class AudacityProject.
// Vaughan, 2010-08-05:
//    Note that this file is included in a "public" section of Project.h.
//    Most of these methods do not need to be public, and because
//    we do not subclass AudacityProject, they should be "private."
//    Because the ones that need to be public are intermixed,
//    I've added "private" in just a few cases.

private:
void CreateMenusAndCommands();
void CreateRecentFilesMenu(CommandManager *c);
void ModifyUndoMenuItems();

CommandFlag GetFocusedFrame();

public:
// If checkActive, do not do complete flags testing on an
// inactive project as it is needlessly expensive.
CommandFlag GetUpdateFlags(bool checkActive = false);

private:
double NearestZeroCrossing(double t0);

public:
        // Audio I/O Commands

void OnStop(const CommandContext &context );
void OnPause(const CommandContext &context );
void OnRecord(const CommandContext &context );
void OnRecord2ndChoice(const CommandContext &context );
void OnStopSelect(const CommandContext &context );
void OnSkipStart(const CommandContext &context );
void OnSkipEnd(const CommandContext &context );
void OnSeekLeftShort(const CommandContext &context );
void OnSeekRightShort(const CommandContext &context );
void OnSeekLeftLong(const CommandContext &context );
void OnSeekRightLong(const CommandContext &context );

        // Different posibilities for playing sound

bool MakeReadyToPlay(bool loop = false, bool cutpreview = false); // Helper function that sets button states etc.
void OnPlayStop(const CommandContext &context );
bool DoPlayStopSelect(bool click, bool shift);
void OnPlayStopSelect(const CommandContext &context );
void OnPlayOneSecond(const CommandContext &context );
void OnPlayToSelection(const CommandContext &context );
void OnPlayBeforeSelectionStart(const CommandContext &context );
void OnPlayAfterSelectionStart(const CommandContext &context );
void OnPlayBeforeSelectionEnd(const CommandContext &context );
void OnPlayAfterSelectionEnd(const CommandContext &context );
void OnPlayBeforeAndAfterSelectionStart(const CommandContext &context );
void OnPlayBeforeAndAfterSelectionEnd(const CommandContext &context );
void OnPlayLooped(const CommandContext &context );
void OnPlayCutPreview(const CommandContext &context );

        // Wave track control
void OnTrackMenu(const CommandContext &context );
void OnTrackClose(const CommandContext &context );

        // Device control
void OnInputDevice(const CommandContext &context );
void OnOutputDevice(const CommandContext &context );
void OnAudioHost(const CommandContext &context );
void OnInputChannels(const CommandContext &context );

        // Moving track focus commands

void OnPrevTrack( bool shift );
void OnNextTrack( bool shift );
void OnCursorUp(const CommandContext &context );
void OnCursorDown(const CommandContext &context );
void OnFirstTrack(const CommandContext &context );
void OnLastTrack(const CommandContext &context );

        // Selection-Editing Commands

void OnShiftUp(const CommandContext &context );
void OnShiftDown(const CommandContext &context );
void OnToggle(const CommandContext &context );

void HandleListSelection(Track *t, bool shift, bool ctrl, bool modifyState);

void OnCursorLeft(const CommandContext &context );
void OnCursorRight(const CommandContext &context );
void OnSelExtendLeft(const CommandContext &context );
void OnSelExtendRight(const CommandContext &context );
void OnSelContractLeft(const CommandContext &context );
void OnSelContractRight(const CommandContext &context );

public:

void OnCursorShortJumpLeft(const CommandContext &context );
void OnCursorShortJumpRight(const CommandContext &context );
void OnCursorLongJumpLeft(const CommandContext &context );
void OnCursorLongJumpRight(const CommandContext &context );
void OnSelSetExtendLeft(const CommandContext &context );
void OnSelSetExtendRight(const CommandContext &context );

void OnSetLeftSelection(const CommandContext &context );
void OnSetRightSelection(const CommandContext &context );

void OnSelToStart(const CommandContext &context );
void OnSelToEnd(const CommandContext &context );

void OnZeroCrossing(const CommandContext &context );

double GetTime(const Track *t);

void OnFullScreen(const CommandContext &context );

static void DoMacMinimize(AudacityProject *project);
void OnMacMinimize(const CommandContext &context );
void OnMacMinimizeAll(const CommandContext &context );
void OnMacZoom(const CommandContext &context );
void OnMacBringAllToFront(const CommandContext &context );

        // File Menu

void OnNew(const CommandContext &context );
void OnOpen(const CommandContext &context );
void OnClose(const CommandContext &context );
void OnSave(const CommandContext &context );
void OnSaveAs(const CommandContext &context );

void OnCheckDependencies(const CommandContext &context );

void OnExport(const wxString & Format);
void OnExportAudio(const CommandContext &context );
void OnExportWav(const CommandContext &context );
void OnExportSelection(const CommandContext &context );

void OnPreferences(const CommandContext &context );
void OnReloadPreferences(const CommandContext &context );

void OnExit(const CommandContext &context );

        // Edit Menu

public:
void OnUndo(const CommandContext &context );
void OnRedo(const CommandContext &context );

private:
static void FinishCopy(const Track *n, Track *dest);
static void FinishCopy(const Track *n, Track::Holder &&dest, TrackList &list);

public:
void OnCut(const CommandContext &context );
void OnCopy(const CommandContext &context );

void OnPaste(const CommandContext &context );
private:
bool HandlePasteText(); // Handle text paste (into active label), if any. Return true if pasted.
bool HandlePasteNothingSelected(); // Return true if nothing selected, regardless of paste result.
public:

void OnPasteOver(const CommandContext &context );

void OnDelete(const CommandContext &context );

void OnSelectTimeAndTracks(bool bAllTime, bool bAllTracks);
void OnSelectAllTime(const CommandContext &context );
void OnSelectAllTracks(const CommandContext &context );
void OnSelectAll(const CommandContext &context );
void OnSelectSomething(const CommandContext &context );
void OnSelectNone(const CommandContext &context );
private:
int CountSelectedWaveTracks();
int CountSelectedTracks();
public:
void OnSelectCursorEnd(const CommandContext &context );
void OnSelectStartCursor(const CommandContext &context );
void OnSelectTrackStartToEnd(const CommandContext &context );
void OnSelectPrevClipBoundaryToCursor(const CommandContext &context );
void OnSelectCursorToNextClipBoundary(const CommandContext &context );
void OnSelectClipBoundary(bool next);
struct FoundTrack {
   const WaveTrack* waveTrack{};
   int trackNum{};
   bool channel{};

   wxString ComposeTrackName() const;
};
struct FoundClip : FoundTrack {
   bool found{};
   double startTime{};
   double endTime{};
   int index{};
};
FoundClip FindNextClip(const WaveTrack* wt, double t0, double t1);
FoundClip FindPrevClip(const WaveTrack* wt, double t0, double t1);
int FindClips(double t0, double t1, bool next, std::vector<FoundClip>& results);
bool ChannelsHaveSameClipBoundaries(const WaveTrack* wt);
void OnSelectPrevClip(const CommandContext &context );
void OnSelectNextClip(const CommandContext &context );
void OnSelectClip(bool next);

void OnZoomIn(const CommandContext &context );
void OnZoomOut(const CommandContext &context );
void OnZoomToggle(const CommandContext &context );
void OnZoomNormal(const CommandContext &context );
void OnZoomFit(const CommandContext &context );
void OnZoomFitV(const CommandContext &context );
void DoZoomFitV();
void OnZoomSel(const CommandContext &context );
void OnGoSelStart(const CommandContext &context );
void OnGoSelEnd(const CommandContext &context );

void OnExpandAllTracks(const CommandContext &context );
void OnCollapseAllTracks(const CommandContext &context );

void OnHistory(const CommandContext &context );

void OnShowTransportToolBar(const CommandContext &context );
void OnShowDeviceToolBar(const CommandContext &context );
void OnShowEditToolBar(const CommandContext &context );
void OnShowMeterToolBar(const CommandContext &context );
void OnShowRecordMeterToolBar(const CommandContext &context );
void OnShowPlayMeterToolBar(const CommandContext &context );
void OnShowSelectionToolBar(const CommandContext &context );
void OnResetToolBars(const CommandContext &context );

        // Transport Menu

void OnSoundActivated(const CommandContext &context );
void OnTogglePlayRecording(const CommandContext &context );
void OnRescanDevices(const CommandContext &context );

// Import Submenu
void OnImport(const CommandContext &context );

private:
   bool mCursorPositionHasBeenStored{false};
   double mCursorPositionStored;
public:
void OnCursorTrackStart(const CommandContext &context );
void OnCursorTrackEnd(const CommandContext &context );
void OnCursorSelStart(const CommandContext &context );
void OnCursorSelEnd(const CommandContext &context );
   struct FoundClipBoundary : FoundTrack {
   int nFound{};    // 0, 1, or 2
   double time{};
   int index1{};
   bool clipStart1{};
   int index2{};
   bool clipStart2{};
};
FoundClipBoundary FindNextClipBoundary(const WaveTrack* wt, double time);
FoundClipBoundary FindPrevClipBoundary(const WaveTrack* wt, double time);
double AdjustForFindingStartTimes(const std::vector<const WaveClip*>& clips, double time);
double AdjustForFindingEndTimes(const std::vector<const WaveClip*>& clips, double time);
int FindClipBoundaries(double time, bool next, std::vector<FoundClipBoundary>& results);
void OnCursorNextClipBoundary(const CommandContext &context );
void OnCursorPrevClipBoundary(const CommandContext &context );
void OnCursorClipBoundary(bool next);
static wxString ClipBoundaryMessage(const std::vector<FoundClipBoundary>& results);

// Tracks menu
void OnNewStereoTrack(const CommandContext &context );
void OnRemoveTracks(const CommandContext &context );

        // Help Menu

void OnAbout(const CommandContext &context );
void OnQuickHelp(const CommandContext &context );
void OnManual(const CommandContext &context );
void OnShowLog(const CommandContext &context );
void OnHelpWelcome(const CommandContext &context );
void OnAudioDeviceInfo(const CommandContext &context );

       //

void OnSeparator(const CommandContext &context );

      // Keyboard navigation

void NextOrPrevFrame(bool next);
void PrevFrame(const CommandContext &context );
void NextFrame(const CommandContext &context );

void PrevWindow(const CommandContext &context );
void NextWindow(const CommandContext &context );

void OnResample(const CommandContext &context );

private:
enum SelectionOperation {
    SELECTION_EXTEND,
    SELECTION_CONTRACT,
    CURSOR_MOVE
};

enum CursorDirection {
   DIRECTION_LEFT = -1,
   DIRECTION_RIGHT = +1
};

enum TimeUnit {
    TIME_UNIT_SECONDS,
    TIME_UNIT_PIXELS
};

bool OnlyHandleKeyUp( const CommandContext &context );
void OnCursorMove(double seekStep);
void OnBoundaryMove(int step);

// Handle small cursor and play head movements
void SeekLeftOrRight
(double direction, SelectionOperation operation);

void SeekWhenAudioActive(double seekStep);
void SeekWhenAudioInactive
(double seekStep, TimeUnit timeUnit,
 SelectionOperation operation);
void MoveWhenAudioInactive
(double seekStep, TimeUnit timeUnit);



double OffsetTime(double t, double offset, TimeUnit timeUnit, int snapToTime);

// Helper for moving by keyboard with snap-to-grid enabled
double GridMove(double t, int minPix);

// Make sure we return to "public" for subsequent declarations in Project.h.
public:


#endif



