/**********************************************************************

  Audacity: A Digital Audio Editor

  Project.h

  Dominic Mazzoni

  In Audacity, the main window you work in is called a project.
  Projects can contain an arbitrary number of tracks of many
  different types, but if a project contains just one or two
  tracks then it can be saved in standard formats like WAV or AIFF.
  This window is the one that contains the menu bar (except on
  the Mac).

**********************************************************************/

#ifndef __AUDACITY_PROJECT__
#define __AUDACITY_PROJECT__

#include "Audacity.h"
#include "Experimental.h"

#include "widgets/OverlayPanel.h"

#include "DirManager.h"
#include "SelectionState.h"
#include "ViewInfo.h"
#include "TrackPanelListener.h"
#include "AudioIOListener.h"
#include "commands/CommandManager.h"

#include "MemoryX.h"
#include <wx/defs.h>
#include <wx/event.h>
#include <wx/log.h>
#include <wx/dragimag.h>
#include <wx/generic/dragimgg.h>
#include <wx/frame.h>
#include <wx/intl.h>
#include <wx/dcclient.h>

#include "import/ImportRaw.h" // defines TrackHolders

const int AudacityProjectTimerID = 5200;

class wxWindow;
class wxDialog;
class wxBoxSizer;
class wxScrollEvent;
class wxScrollBar;
class wxPanel;
class wxTimerEvent;

class AudacityProject;
class Importer;
class ODLock;
class TrackList;

class TrackPanel;
class MeterPanel;

// toolbar classes
class ControlToolBar;
class DeviceToolBar;
class EditToolBar;
class MeterToolBar;
class SelectionBar;
class SpectralSelectionBar;
class ToolManager;

// windows and frames
class AdornedRulerPanel;
class HistoryWindow;

struct AudioIOStartStreamOptions;
struct UndoState;

class Regions;

class LWSlider;
class UndoManager;
enum class UndoPush : unsigned char;

class Track;
class WaveClip;
class BackgroundCell;

AudacityProject *CreateNewAudacityProject();
AUDACITY_DLL_API AudacityProject *GetActiveProject();
void RedrawAllProjects();
void RefreshCursorForAllProjects();
AUDACITY_DLL_API void CloseAllProjects();

void GetDefaultWindowRect(wxRect *defRect);
void GetNextWindowPlacement(wxRect *nextRect, bool *pMaximized, bool *pIconized);
bool IsWindowAccessible(wxRect *requestedRect);

// Use shared_ptr to projects, because elsewhere we need weak_ptr
using AProjectHolder = std::shared_ptr< AudacityProject >;
using AProjectArray = std::vector< AProjectHolder >;

extern AProjectArray gAudacityProjects;


enum class PlayMode : int {
   normalPlay,
   oneSecondPlay, // Disables auto-scrolling
   loopedPlay // Disables auto-scrolling
};

enum StatusBarField {
   stateStatusBarField = 1,
   mainStatusBarField = 2,
   rateStatusBarField = 3
};

////////////////////////////////////////////////////////////
/// Custom events
////////////////////////////////////////////////////////////
DECLARE_EXPORTED_EVENT_TYPE(AUDACITY_DLL_API, EVT_CAPTURE_KEY, -1);

class CommandContext;
class CommandManager;
class Track;
class TrackHolder;
class TrackList;
class WaveClip;
class WaveTrack;

#include "./commands/CommandFlag.h"
#include "../include/audacity/EffectInterface.h"

#include "./commands/CommandManager.h"


class AUDACITY_DLL_API AudacityProject final : public wxFrame,
                                     public TrackPanelListener,
                                     public AudioIOListener
{
 public:
   AudacityProject(wxWindow * parent, wxWindowID id,
                   const wxPoint & pos, const wxSize & size);
   virtual ~AudacityProject();

   virtual void ApplyUpdatedTheme();

   AudioIOStartStreamOptions GetDefaultPlayOptions();

   TrackList *GetTracks() { return mTracks.get(); }
   const TrackList *GetTracks() const { return mTracks.get(); }
   size_t GetTrackCount() const { return GetTracks()->size(); }
   UndoManager *GetUndoManager() { return mUndoManager.get(); }

   sampleFormat GetDefaultFormat() { return mDefaultFormat; }

   double GetRate() const { return mRate; }
   bool ZoomInAvailable() const { return mViewInfo.ZoomInAvailable(); }
   bool ZoomOutAvailable() const { return mViewInfo.ZoomOutAvailable(); }
   const SelectedRegion &GetSelection() const { return mViewInfo.selectedRegion; }
   SelectedRegion &GetSelection() { return mViewInfo.selectedRegion; }
   double GetSel0() const { return mViewInfo.selectedRegion.t0(); }
   double GetSel1() const { return mViewInfo.selectedRegion.t1(); }
   const ZoomInfo &GetZoomInfo() const { return mViewInfo; }
   const ViewInfo &GetViewInfo() const { return mViewInfo; }
   ViewInfo &GetViewInfo() { return mViewInfo; }

   std::shared_ptr<Track> GetFirstVisible();

   void GetPlayRegion(double* playRegionStart, double *playRegionEnd);

   void SetSel0(double);        //Added by STM
   void SetSel1(double);        //Added by STM

   bool Clipboard() { return (msClipT1 - msClipT0) > 0.0; }

   wxString GetName();
   const std::shared_ptr<DirManager> &GetDirManager();
   TrackFactory *GetTrackFactory();
   AdornedRulerPanel *GetRulerPanel();
   int GetAudioIOToken() const;
   bool IsAudioActive() const;
   void SetAudioIOToken(int token);

   bool IsActive();

   // File I/O

   /** @brief Show an open dialogue for opening audio files, and possibly other
    * sorts of files.
    *
    * The file type filter will automatically contain:
    * - "All files" with any extension or none,
    * - "All supported files" based on the file formats supported in this
    *   build of Audacity,
    * - All of the individual formats specified by the importer plug-ins which
    *   are built into this build of Audacity, each with the relevant file
    *   extensions for that format.
    * The dialogue will start in the DefaultOpenPath directory read from the
    * preferences, failing that the working directory. The file format filter
    * will be set to the DefaultOpenType from the preferences, failing that
    * the first format specified in the dialogue. These two parameters will
    * be saved to the preferences once the user has chosen a file to open.
    * @param extraformat Specify the name of an additional format to allow
    * opening in this dialogue. This string is free-form, but should be short
    * enough to fit in the file dialogue filter drop-down. It should be
    * translated.
    * @param extrafilter Specify the file extension(s) for the additional format
    * specified by extraformat. The patterns must include the wildcard (e.g.
    * "*.aup" not "aup" or ".aup"), separate multiple patters with a semicolon,
    * e.g. "*.aup;*.AUP" because patterns are case-sensitive. Do not add a
    * trailing semicolon to the string. This string should not be translated
    * @return Array of file paths which the user selected to open (multiple
    * selections allowed).
    */
   static wxArrayString ShowOpenDialog(const wxString &extraformat = wxEmptyString,
         const wxString &extrafilter = wxEmptyString);
   static bool IsAlreadyOpen(const wxString & projPathName);

   // Return the given project if that is not NULL, else create a project.
   // Then open the given project path.
   // But if an exception escapes this function, create no NEW project.
   static AudacityProject *OpenProject(AudacityProject *pProject);

private:
   void EnqueueODTasks();

public:
   // If pNewTrackList is passed in non-NULL, it gets filled with the pointers to NEW tracks.
   bool Import(const wxString &fileName, WaveTrackArray *pTrackArray = NULL);

   void ZoomAfterImport(Track *pTrack);

   // Takes array of unique pointers; returns array of shared
   std::vector< std::shared_ptr<Track> >
   AddImportedTracks(const wxString &fileName,
                     TrackHolders &&newTracks);

   bool Save();
   bool SaveAs();
   bool SaveAs(const wxString & newFileName, bool addToHistory = true);
public:

   void Clear();

   const wxString &GetFileName() { return mFileName; }
   bool GetDirty() { return mDirty; }
   void SetProjectTitle( int number =-1);

   wxPanel *GetTopPanel() { return mTopPanel; }
   TrackPanel * GetTrackPanel() {return mTrackPanel;}
   const TrackPanel * GetTrackPanel() const {return mTrackPanel;}
   SelectionState &GetSelectionState() { return mSelectionState; }

   bool GetIsEmpty();

   bool GetTracksFitVerticallyZoomed() { return mTracksFitVerticallyZoomed; } //lda
   void SetTracksFitVerticallyZoomed(bool flag) { mTracksFitVerticallyZoomed = flag; } //lda

   /** \brief Sets the wxDialog that is being displayed
     * Used by the custom dialog warning constructor and destructor
     */
   void SetMissingAliasFileDialog(wxDialog *dialog);

   /** \brief returns a pointer to the wxDialog if it is displayed, NULL otherwise.
     */
   wxDialog *GetMissingAliasFileDialog();

   // Timer Record Auto Save/Export Routines
   bool SaveFromTimerRecording(wxFileName fnFile);
   bool ExportFromTimerRecording(wxFileName fnFile, int iFormat, int iSubFormat, int iFilterIndex);
   bool IsProjectSaved();

   bool ProjectHasTracks();

   // Routine to estimate how many minutes of recording time are left on disk
   int GetEstimatedRecordingMinsLeftOnDisk(long lCaptureChannels = 0);
   // Converts number of minutes to human readable format
   wxString GetHoursMinsString(int iMinutes);

#include "Menus.h"

   CommandManager *GetCommandManager() { return &mCommandManager; }
   const CommandManager *GetCommandManager() const { return &mCommandManager; }

   // Keyboard capture
   static bool HasKeyboardCapture(const wxWindow *handler);
   static wxWindow *GetKeyboardCaptureHandler();
   static void CaptureKeyboard(wxWindow *handler);
   static void ReleaseKeyboard(wxWindow *handler);

   void RebuildMenuBar();
   void RebuildOtherMenus();


   // Message Handlers

   void OnMenu(wxCommandEvent & event);
   void OnUpdateUI(wxUpdateUIEvent & event);

   void MacShowUndockedToolbars(bool show);
   void OnActivate(wxActivateEvent & event);

   void OnMouseEvent(wxMouseEvent & event);
   void OnIconize(wxIconizeEvent &event);
   void OnSize(wxSizeEvent & event);
   void OnShow(wxShowEvent & event);
   void OnMove(wxMoveEvent & event);
   void DoScroll();
   void OnScroll(wxScrollEvent & event);
   void OnCloseWindow(wxCloseEvent & event);
   void OnTimer(wxTimerEvent & event);
   void OnToolBarUpdate(wxCommandEvent & event);
   void OnODTaskUpdate(wxCommandEvent & event);
   void OnODTaskComplete(wxCommandEvent & event);

   void HandleResize();
   void UpdateLayout();
   double GetScreenEndTime() const;
   void ZoomInByFactor( double ZoomFactor );
   void ZoomOutByFactor( double ZoomFactor );

   // Other commands
   static TrackList *GetClipboardTracks();
   static void ClearClipboard();
   static void DeleteClipboard();

   int GetProjectNumber(){ return mProjectNo;};
   static int CountUnnamed();
   static void RefreshAllTitles(bool bShowProjectNumbers );
   // checkActive is a temporary hack that should be removed as soon as we
   // get multiple effect preview working
   void UpdateMenus(bool checkActive = true);
   void UpdatePrefs();
   void UpdatePrefsVariables();
   void RedrawProject(const bool bForceWaveTracks = false);
   void RefreshCursor();
   void SelectNone();
   void SelectAllIfNone();
   void StopIfPaused();
   void Zoom(double level);
   void ZoomBy(double multiplier);
   void Rewind(bool shift);
   void SkipEnd(bool shift);


   typedef void (WaveTrack::* EditFunction)(double, double);
   typedef std::unique_ptr<Track> (WaveTrack::* EditDestFunction)(double, double);

   void RemoveTrack(Track * toRemove);

   // Snap To

   int GetSnapTo() const;

   // Selection Format

   void SetSelectionFormat(const wxString & format);
   const wxString & GetSelectionFormat() const;

   // Scrollbars

   void OnScrollLeft();
   void OnScrollRight();

   void OnScrollLeftButton(wxScrollEvent & event);
   void OnScrollRightButton(wxScrollEvent & event);

   void FinishAutoScroll();
   void FixScrollbars();

   bool MayScrollBeyondZero() const;
   double ScrollingLowerBoundTime() const;
   // How many pixels are covered by the period from lowermost scrollable time, to the given time:
   // PRL: Bug1197: we seem to need to compute all in double, to avoid differing results on Mac
   double PixelWidthBeforeTime(double scrollto) const;
   void SetHorizontalThumb(double scrollto);

   // PRL:  old and incorrect comment below, these functions are used elsewhere than TrackPanel
   // TrackPanel access
   wxSize GetTPTracksUsableArea() /* not override */;
   void RefreshTPTrack(Track* pTrk, bool refreshbacking = true) /* not override */;

   // TrackPanel callback methods, overrides of TrackPanelListener
   void TP_DisplaySelection() override;
   void TP_DisplayStatusMessage(const wxString &msg) override;

   void TP_PushState(const wxString &longDesc, const wxString &shortDesc,
                             UndoPush flags) override;
   void TP_ModifyState() override;
   void TP_RedrawScrollbars() override;
   void TP_ScrollLeft() override;
   void TP_ScrollRight() override;
   void TP_ScrollWindow(double scrollto) override;
   bool TP_ScrollUpDown(int delta) override;
   void TP_HandleResize() override;

   // ToolBar

   // In the GUI, ControlToolBar appears as the "Transport Toolbar". "Control Toolbar" is historic.
   ControlToolBar *GetControlToolBar();

   DeviceToolBar *GetDeviceToolBar();
   EditToolBar *GetEditToolBar();
   SelectionBar *GetSelectionBar();

   MeterPanel *GetPlaybackMeter();
   void SetPlaybackMeter(MeterPanel *playback);
   MeterPanel *GetCaptureMeter();
   void SetCaptureMeter(MeterPanel *capture);

   wxStatusBar* GetStatusBar() { return mStatusBar; }

private:
   bool SnapSelection();

public:

   PlayMode mLastPlayMode{ PlayMode::normalPlay };
   ViewInfo mViewInfo;

   // Audio IO callback methods
   void OnAudioIORate(int rate) override;
   void OnAudioIOStartRecording() override;
   void OnAudioIOStopRecording() override;

   // Command Handling
   bool ReportIfActionNotAllowed
      ( const wxString & Name, CommandFlag & flags, CommandFlag flagsRqd, CommandFlag mask );
   bool TryToMakeActionAllowed
      ( CommandFlag & flags, CommandFlag flagsRqd, CommandFlag mask );

   bool UndoAvailable();
   bool RedoAvailable();

   void PushState(const wxString &desc, const wxString &shortDesc); // use UndoPush::AUTOSAVE
   void PushState(const wxString &desc, const wxString &shortDesc, UndoPush flags);

 private:

   void OnCapture(wxCommandEvent & evt);
   void InitialState();

 public:
   void ModifyState();

 private:
   void PopState(const UndoState &state);

   void AutoSave();

   double GetZoomOfToFit();
   double GetZoomOfSelection();
   double GetZoomOfPreset(int preset );
   double GetZoomOfPref( const wxString & PresetPrefName, int defaultPreset );

 public:
   bool IsSoloSimple() const { return mSoloPref == wxT("Simple"); }
   bool IsSoloNone() const { return mSoloPref == wxT("None"); }

 private:

   // The project's name and file info
   wxString mFileName;
   bool mbLoadedFromAup;
   std::shared_ptr<DirManager> mDirManager; // MM: DirManager now created dynamically

   static int mProjectCounter;// global counter.
   int mProjectNo; // count when this project was created.

   double mRate;
   sampleFormat mDefaultFormat;

   // Recent files
   wxMenu *mRecentFilesMenu;

   // List of tracks and display info
   std::shared_ptr<TrackList> mTracks;

   int mSnapTo;
   wxString mSelectionFormat;
   wxString mFrequencySelectionFormatName;
   wxString mBandwidthSelectionFormatName;

   std::shared_ptr<TrackList> mLastSavedTracks;

   // Clipboard (static because it is shared by all projects)
   static std::shared_ptr<TrackList> msClipboard;
   static AudacityProject *msClipProject;
   static double msClipT0;
   static double msClipT1;

public:
   ///Prevents DELETE from external thread - for e.g. use of GetActiveProject
   //shared by all projects
   static ODLock &AllProjectDeleteMutex();

private:
   // History/Undo manager
   std::unique_ptr<UndoManager> mUndoManager;
   bool mDirty{ false };

   // Commands

   CommandManager mCommandManager;

   CommandFlag mLastFlags;

   // Window elements

   std::unique_ptr<wxTimer> mTimer;
   long mLastStatusUpdateTime;

   wxStatusBar *mStatusBar;

   AdornedRulerPanel *mRuler{};
   wxPanel *mTopPanel{};
   TrackPanel *mTrackPanel{};
   SelectionState mSelectionState{};
   bool mCircularTrackNavigation{};
   std::unique_ptr<TrackFactory> mTrackFactory{};
   wxPanel * mMainPanel;
   wxScrollBar *mHsbar;
   wxScrollBar *mVsbar;
   bool mAutoScrolling{ false };
   bool mActive{ true };
   bool mIconized;

   HistoryWindow *mHistoryWindow{};

   // dialog for missing alias warnings
   wxDialog            *mAliasMissingWarningDialog{};

   bool mShownOnce{ false };

   // Project owned meters
   MeterPanel *mPlaybackMeter{};
   MeterPanel *mCaptureMeter{};

   std::unique_ptr<ToolManager> mToolManager;

 public:
   ToolManager *GetToolManager() { return mToolManager.get(); }
   bool mShowSplashScreen;
   wxString mHelpPref;
   wxString mSoloPref;
   bool mbBusyImporting{ false }; // used to fix bug 584

   void SetNormalizedWindowState(wxRect pSizeAndLocation) {  mNormalizedWindowState = pSizeAndLocation;   }
   wxRect GetNormalizedWindowState() const { return mNormalizedWindowState;   }
 private:
   //sort method used by OnSortName and OnSortTime
   //currently only supported flags are kAudacitySortByName and kAudacitySortByName
   //in the future we might have 0x01 as sort ascending and we can bit or it
#define kAudacitySortByTime (1 << 1)
#define kAudacitySortByName (1 << 2)

   int  mAudioIOToken{ -1 };

   bool mIsDeleting{ false };
   bool mTracksFitVerticallyZoomed{ false };  //lda

   // 0 is grey out, 1 is Autoselect, 2 is Give warnings.
   int  mWhatIfNoSelection;
   bool mStopIfWasPaused;

   wxArrayString mStrOtherNamesArray; // used to make sure compressed file names are unique
   
   wxRect mNormalizedWindowState;

   // Are we currently closing as the result of a menu command?
   bool mMenuClose{ false };

   bool mbInitializingScrollbar{ false };

   // Flag that we're recoding.
   bool mIsCapturing{ false };

   // Keyboard capture
   wxWindow *mKeyboardCaptureHandler{};

   double mSeekShort;
   double mSeekLong;

   wxLongLong mLastSelectionAdjustment;

   // See explanation in OnCloseWindow
   bool mIsBeingDeleted{ false };

   // CommandManager needs to use private methods
   friend class CommandManager;

   // TrackPanelOverlay objects
   std::unique_ptr<Overlay>
      mIndicatorOverlay, mCursorOverlay;

   std::shared_ptr<BackgroundCell> mBackgroundCell;

   public:
   class PlaybackScroller final : public wxEvtHandler
   {
   public:
      explicit PlaybackScroller(AudacityProject *project);

      enum class Mode {
         Off,
         Refresh,
         Centered,
         Right,
      };

      Mode GetMode() const { return mMode; }
      void Activate(Mode mode)
      {
         mMode = mode;
      }

   private:
      void OnTimer(wxCommandEvent &event);

      AudacityProject *mProject;
      Mode mMode { Mode::Off };
   };
   std::unique_ptr<PlaybackScroller> mPlaybackScroller;

public:
   PlaybackScroller &GetPlaybackScroller() { return *mPlaybackScroller; }
   std::shared_ptr<BackgroundCell> GetBackgroundCell() const
      { return mBackgroundCell; }

   DECLARE_EVENT_TABLE()
};

#endif

