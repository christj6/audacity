/**********************************************************************

  Audacity: A Digital Audio Editor

  TrackPanel.h

  Dominic Mazzoni

**********************************************************************/

#ifndef __AUDACITY_TRACK_PANEL__
#define __AUDACITY_TRACK_PANEL__

#include "MemoryX.h"
#include <vector>

#include <wx/timer.h>

#include "Experimental.h"

#include "HitTestResult.h"

#include "SelectedRegion.h"

#include "widgets/OverlayPanel.h"

#include "SelectionState.h"

class wxMenu;
class wxRect;

class Track;
class TrackList;
class TrackPanel;
class TrackPanelCell;
class TrackArtist;
class Ruler;
class SnapManager;
class AdornedRulerPanel;
class LWSlider;
class ControlToolBar; //Needed because state of controls can affect what gets drawn.
class AudacityProject;

class TrackPanelAx;
class TrackPanelCellIterator;
struct TrackPanelMouseEvent;
struct TrackPanelMouseState;

class ViewInfo;

class WaveTrack;
class WaveClip;
class UIHandle;
using UIHandlePtr = std::shared_ptr<UIHandle>;

// Declared elsewhere, to reduce compilation dependencies
class TrackPanelListener;

struct TrackPanelDrawingContext;

enum class UndoPush : unsigned char;

wxDECLARE_EXPORTED_EVENT(AUDACITY_DLL_API,
                         EVT_TRACK_PANEL_TIMER, wxCommandEvent);

enum {
   kTimerInterval = 50, // milliseconds
};


class AUDACITY_DLL_API TrackInfo
{
public:
   TrackInfo(TrackPanel * pParentIn);
   ~TrackInfo();
   void ReCreateSliders();

   static unsigned MinimumTrackHeight();

   struct TCPLine;

   static void DrawItems
      ( TrackPanelDrawingContext &context,
        const wxRect &rect, const Track &track );

   static void DrawItems
      ( TrackPanelDrawingContext &context,
        const wxRect &rect, const Track *pTrack,
        const std::vector<TCPLine> &topLines,
        const std::vector<TCPLine> &bottomLines );

   static void CloseTitleDrawFunction
      ( TrackPanelDrawingContext &context,
        const wxRect &rect, const Track *pTrack );

   static void StatusDrawFunction
      ( const wxString &string, wxDC *dc, const wxRect &rect );

   static void Status1DrawFunction
      ( TrackPanelDrawingContext &context,
        const wxRect &rect, const Track *pTrack );

   static void Status2DrawFunction
      ( TrackPanelDrawingContext &context,
        const wxRect &rect, const Track *pTrack );

public:
   int GetTrackInfoWidth() const;
   static void SetTrackInfoFont(wxDC *dc);

   void DrawBackground(wxDC * dc, const wxRect & rect, bool bSelected, bool bHasMuteSolo, const int labelw, const int vrul) const;
   void DrawBordersWithin(wxDC * dc, const wxRect & rect, const Track &track ) const;

   static void GetCloseBoxHorizontalBounds( const wxRect & rect, wxRect &dest );
   static void GetCloseBoxRect(const wxRect & rect, wxRect &dest);

   static void GetTitleBarHorizontalBounds( const wxRect & rect, wxRect &dest );
   static void GetTitleBarRect(const wxRect & rect, wxRect &dest);

   static void GetNarrowMuteHorizontalBounds
      ( const wxRect & rect, wxRect &dest );

   static void GetSliderHorizontalBounds( const wxPoint &topleft, wxRect &dest );

   static void GetGainRect(const wxPoint & topLeft, wxRect &dest);

   static void GetPanRect(const wxPoint & topLeft, wxRect &dest);

   static bool HideTopItem( const wxRect &rect, const wxRect &subRect,
                               int allowance = 0 );

   static unsigned DefaultNoteTrackHeight();
   static unsigned DefaultWaveTrackHeight();

private:
   void UpdatePrefs();

   TrackPanel * pParent;
   static wxFont gFont;
   // These are on separate lines to work around an MSVC 2013 compiler bug.
   static std::unique_ptr<LWSlider> gGainCaptured;
   static std::unique_ptr<LWSlider> gPanCaptured;
   static std::unique_ptr<LWSlider> gGain;
   static std::unique_ptr<LWSlider> gPan;

   friend class TrackPanel;
};


const int DragThreshold = 3;// Anything over 3 pixels is a drag, else a click.


class AUDACITY_DLL_API TrackPanel final : public OverlayPanel {
 public:

   TrackPanel(wxWindow * parent,
              wxWindowID id,
              const wxPoint & pos,
              const wxSize & size,
              const std::shared_ptr<TrackList> &tracks,
              ViewInfo * viewInfo,
              TrackPanelListener * listener,
              AdornedRulerPanel * ruler );

   virtual ~ TrackPanel();

   IteratorRange< TrackPanelCellIterator > Cells();

   void UpdatePrefs();
   void ApplyUpdatedTheme();

   void OnPaint(wxPaintEvent & event);
   void OnMouseEvent(wxMouseEvent & event);
   void OnCaptureLost(wxMouseCaptureLostEvent & event);
   void OnCaptureKey(wxCommandEvent & event);
   void OnKeyDown(wxKeyEvent & event);
   void OnChar(wxKeyEvent & event);
   void OnKeyUp(wxKeyEvent & event);

   void OnSetFocus(wxFocusEvent & event);
   void OnKillFocus(wxFocusEvent & event);

   void OnContextMenu(wxContextMenuEvent & event);

   void OnPlayback(wxCommandEvent &);
   void OnTrackListResizing(wxCommandEvent & event);
   void OnTrackListDeletion(wxCommandEvent & event);
   void UpdateViewIfNoTracks(); // Call this to update mViewInfo, etc, after track(s) removal, before Refresh().

   double GetMostRecentXPos();

   void OnIdle(wxIdleEvent & event);
   void OnTimer(wxTimerEvent& event);

   int GetLeftOffset() const { return GetLabelWidth() + 1;}

   // Width and height, relative to upper left corner at (GetLeftOffset(), 0)
   // Either argument may be NULL
   void GetTracksUsableArea(int *width, int *height) const;

   void Refresh
      (bool eraseBackground = true, const wxRect *rect = (const wxRect *) NULL)
      override;

   void RefreshTrack(Track *trk, bool refreshbacking = true);

   void DisplaySelection();

   void HandleInterruptedDrag();
   void Uncapture( wxMouseState *pState = nullptr );
   bool CancelDragging();
   bool HandleEscapeKey(bool down);
   void UpdateMouseState(const wxMouseState &state);
   void HandleModifierKey();
   void HandlePageUpKey();
   void HandlePageDownKey();
   AudacityProject * GetProject() const;

   void ScrollIntoView(double pos);
   void ScrollIntoView(int x);

   void OnTrackMenu(Track *t = NULL);
   Track * GetFirstSelectedTrack();
   bool IsMouseCaptured();

   void EnsureVisible(Track * t);
   void VerticalScroll( float fracPosition);

   Track *GetFocusedTrack();
   void SetFocusedTrack(Track *t);

   void HandleCursorForPresentMouseState(bool doHit = true);

   void UpdateVRulers();
   void UpdateVRuler(Track *t);
   void UpdateTrackVRuler(const Track *t);
   void UpdateVRulerSize();

   // Returns the time corresponding to the pixel column one past the track area
   // (ignoring any fisheye)
   double GetScreenEndTime() const;

 protected:
   bool IsAudioActive();
   void HandleClick( const TrackPanelMouseEvent &tpmEvent );

public:
   size_t GetTrackCount() const;
   size_t GetSelectedTrackCount() const;

protected:
   void UpdateSelectionDisplay();

public:
   void UpdateAccessibility();
   void MessageForScreenReader(const wxString& message);

   // MM: Handle mouse wheel rotation
   void HandleWheelRotation( TrackPanelMouseEvent &tpmEvent );

   void MakeParentRedrawScrollbars();

protected:
   void MakeParentModifyState();

   // Find track info by coordinate
   struct FoundCell {
      std::shared_ptr<Track> pTrack;
      std::shared_ptr<TrackPanelCell> pCell;
      wxRect rect;
   };
   FoundCell FindCell(int mouseX, int mouseY);

   void HandleMotion( wxMouseState &state, bool doHit = true );
   void HandleMotion
      ( const TrackPanelMouseState &tpmState, bool doHit = true );

   // If label, rectangle includes track control panel only.
   // If !label, rectangle includes all of that, and the vertical ruler, and
   // the proper track area.
   wxRect FindTrackRect( const Track * target, bool label );

   int GetVRulerWidth() const;
   int GetVRulerOffset() const { return mTrackInfo.GetTrackInfoWidth(); }

   int GetLabelWidth() const { return mTrackInfo.GetTrackInfoWidth() + GetVRulerWidth(); }

// JKC Nov-2011: These four functions only used from within a dll such as mod-track-panel
// They work around some messy problems with constructors.
public:
   const TrackList * GetTracks() const { return mTracks.get(); }
   TrackList * GetTracks() { return mTracks.get(); }
   ViewInfo * GetViewInfo(){ return mViewInfo;}
   TrackPanelListener * GetListener(){ return mListener;}
   AdornedRulerPanel * GetRuler(){ return mRuler;}
// JKC and here is a factory function which just does 'NEW' in standard Audacity.
   // Precondition: parent != NULL
   static TrackPanel *(*FactoryFunction)(wxWindow * parent,
              wxWindowID id,
              const wxPoint & pos,
              const wxSize & size,
              const std::shared_ptr<TrackList> &tracks,
              ViewInfo * viewInfo,
              TrackPanelListener * listener,
              AdornedRulerPanel * ruler);

protected:
   void DrawTracks(wxDC * dc);

   void DrawEverythingElse(TrackPanelDrawingContext &context,
                           const wxRegion & region,
                           const wxRect & clip);
   void DrawOutside
      (TrackPanelDrawingContext &context,
       Track *t, const wxRect & rec);

   void HighlightFocusedTrack (wxDC* dc, const wxRect &rect);
   void DrawShadow            (Track *t, wxDC* dc, const wxRect & rect);
   void DrawBordersAroundTrack(Track *t, wxDC* dc, const wxRect & rect, const int labelw, const int vrul);
   void DrawOutsideOfTrack
      (TrackPanelDrawingContext &context,
       Track *t, const wxRect & rect);

public:
   // Set the object that performs catch-all event handling when the pointer
   // is not in any track or ruler or control panel.
   void SetBackgroundCell
      (const std::shared_ptr< TrackPanelCell > &pCell);
   std::shared_ptr< TrackPanelCell > GetBackgroundCell();

protected:

   TrackInfo mTrackInfo;

public:

   TrackInfo *GetTrackInfo() { return &mTrackInfo; }
   const TrackInfo *GetTrackInfo() const { return &mTrackInfo; }

protected:
   TrackPanelListener *mListener;

   std::shared_ptr<TrackList> mTracks;
   ViewInfo *mViewInfo;

   AdornedRulerPanel *mRuler;

   std::unique_ptr<TrackArtist> mTrackArtist;

   class AUDACITY_DLL_API AudacityTimer final : public wxTimer {
   public:
     void Notify() override{
       // (From Debian)
       //
       // Don't call parent->OnTimer(..) directly here, but instead post
       // an event. This ensures that this is a pure wxWidgets event
       // (no GDK event behind it) and that it therefore isn't processed
       // within the YieldFor(..) of the clipboard operations (workaround
       // for Debian bug #765341).
       // QueueEvent() will take ownership of the event
       parent->GetEventHandler()->QueueEvent(safenew wxTimerEvent(*this));
     }
     TrackPanel *parent;
   } mTimer;

   int mTimeCount;

   bool mRefreshBacking;

   bool mRedrawAfterStop;

   wxMouseState mLastMouseState;

   int mMouseMostRecentX;
   int mMouseMostRecentY;

   friend class TrackPanelAx;

#if wxUSE_ACCESSIBILITY
   TrackPanelAx *mAx{};
#else
   std::unique_ptr<TrackPanelAx> mAx;
#endif

public:
   TrackPanelAx &GetAx() { return *mAx; }

protected:

   static wxString gSoloPref;

   SelectedRegion mLastDrawnSelectedRegion {};

 public:
   wxSize vrulerSize;

 protected:
   std::weak_ptr<TrackPanelCell> mLastCell;
   std::vector<UIHandlePtr> mTargets;
   size_t mTarget {};
   unsigned mMouseOverUpdateFlags{};

 public:
   UIHandlePtr Target()
   {
      if (mTargets.size())
         return mTargets[mTarget];
      else
         return {};
   }

 protected:
   void ClearTargets()
   {
      // Forget the rotation of hit test candidates when the mouse moves from
      // cell to cell or outside of the TrackPanel entirely.
      mLastCell.reset();
      mTargets.clear();
      mTarget = 0;
      mMouseOverUpdateFlags = 0;
   }

   bool HasRotation();
   bool HasEscape();

   bool ChangeTarget(bool forward, bool cycle);

   std::weak_ptr<Track> mpClickedTrack;
   UIHandlePtr mUIHandle;

   std::shared_ptr<TrackPanelCell> mpBackground;

   bool mEnableTab{};

   DECLARE_EVENT_TABLE()

   // friending GetInfoCommand allow automation to get sizes of the 
   // tracks, track control panel and such.
   friend class GetInfoCommand;
};

// See big pictorial comment in TrackPanel for explanation of these numbers
enum : int {
   kLeftInset = 4,
   kRightInset = kLeftInset,
   kTopInset = 4,
   kShadowThickness = 1,
   kBorderThickness = 1,
   kTopMargin = kTopInset + kBorderThickness,
   kBottomMargin = kShadowThickness + kBorderThickness,
   kLeftMargin = kLeftInset + kBorderThickness,
   kRightMargin = kRightInset + kShadowThickness + kBorderThickness,
};

enum : int {
   kTrackInfoWidth = 100,
   kTrackInfoBtnSize = 18, // widely used dimension, usually height
   kTrackInfoSliderHeight = 25,
   kTrackInfoSliderWidth = 84,
   kTrackInfoSliderAllowance = 5,
   kTrackInfoSliderExtra = 5,
};

#endif
