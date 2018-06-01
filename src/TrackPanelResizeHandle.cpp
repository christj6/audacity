/**********************************************************************

Audacity: A Digital Audio Editor

TrackPanelResizeHandle.cpp

Paul Licameli split from TrackPanel.cpp

**********************************************************************/

#include "Audacity.h"
#include "TrackPanelResizeHandle.h"
#include "Experimental.h"

#include "MemoryX.h"

#include <wx/cursor.h>
#include <wx/translation.h>

#include "HitTestResult.h"
#include "Project.h"
#include "RefreshCode.h"
#include "Track.h"
#include "TrackPanelMouseEvent.h"
#include "tracks/ui/TrackControls.h"

HitTestPreview TrackPanelResizeHandle::HitPreview(bool bLinked)
{
   static wxCursor resizeCursor{ wxCURSOR_SIZENS };

   /// When in the resize area we can adjust size or relative size.
   // Check to see whether it is the first channel of a stereo track
   if (bLinked) {
      // If we are in the label we got here 'by mistake' and we're
      // not actually in the resize area at all.  (The resize area
      // is shorter when it is between stereo tracks).

      return {
         _("Click and drag to adjust relative size of stereo tracks."),
         &resizeCursor
      };
   }
   else {
      return {
         _("Click and drag to resize the track."),
         &resizeCursor
      };
   }
}

TrackPanelResizeHandle::~TrackPanelResizeHandle()
{
}

UIHandle::Result TrackPanelResizeHandle::Click
(const TrackPanelMouseEvent &WXUNUSED(evt), AudacityProject *WXUNUSED(pProject))
{
   return RefreshCode::RefreshNone;
}

TrackPanelResizeHandle::TrackPanelResizeHandle
( const std::shared_ptr<Track> &track, int y, const AudacityProject *pProject )
   : mpTrack{ track }
   , mMouseClickY( y )
{
   auto tracks = pProject->GetTracks();
   Track *prev = tracks->GetPrev(track.get());
   Track *next = tracks->GetNext(track.get());

   //STM:  Determine whether we should rescale one or two tracks
   if (prev && prev->GetLink() == track.get()) {
      // mpTrack is the lower track
      mInitialTrackHeight = track->GetHeight();
      mInitialActualHeight = track->GetActualHeight();
      mInitialMinimized = track->GetMinimized();
      mInitialUpperTrackHeight = prev->GetHeight();
      mInitialUpperActualHeight = prev->GetActualHeight();
      mMode = IsResizingBelowLinkedTracks;
   }
   else if (next && track->GetLink() == next) {
      // mpTrack is the upper track
      mInitialTrackHeight = next->GetHeight();
      mInitialActualHeight = next->GetActualHeight();
      mInitialMinimized = next->GetMinimized();
      mInitialUpperTrackHeight = track->GetHeight();
      mInitialUpperActualHeight = track->GetActualHeight();
      mMode = IsResizingBetweenLinkedTracks;
   }
   else {
      // DM: Save the initial mouse location and the initial height
      mInitialTrackHeight = track->GetHeight();
      mInitialActualHeight = track->GetActualHeight();
      mInitialMinimized = track->GetMinimized();
      mMode = IsResizing;
   }
}

UIHandle::Result TrackPanelResizeHandle::Drag
(const TrackPanelMouseEvent &evt, AudacityProject *pProject)
{
   auto pTrack = pProject->GetTracks()->Lock(mpTrack);
   if ( !pTrack )
      return RefreshCode::Cancelled;

   const wxMouseEvent &event = evt.event;
   TrackList *const tracks = pProject->GetTracks();

   int delta = (event.m_y - mMouseClickY);

   // On first drag, jump out of minimized mode.  Initial height
   // will be height of minimized track.
   //
   // This used to be in HandleResizeClick(), but simply clicking
   // on a resize border would switch the minimized state.
   if (pTrack->GetMinimized()) {
      Track *link = pTrack->GetLink();

      pTrack->SetHeight(pTrack->GetHeight());
      pTrack->SetMinimized(false);

      if (link) {
         link->SetHeight(link->GetHeight());
         link->SetMinimized(false);
         // Initial values must be reset since they weren't based on the
         // minimized heights.
         mInitialUpperTrackHeight = link->GetHeight();
         mInitialTrackHeight = pTrack->GetHeight();
      }
   }

   // Common pieces of code for MONO_WAVE_PAN and otherwise.
   auto doResizeBelow = [&] (Track *prev, bool WXUNUSED(vStereo)) {
      double proportion = static_cast < double >(mInitialTrackHeight)
      / (mInitialTrackHeight + mInitialUpperTrackHeight);

      int newTrackHeight = static_cast < int >
      (mInitialTrackHeight + delta * proportion);

      int newUpperTrackHeight = static_cast < int >
      (mInitialUpperTrackHeight + delta * (1.0 - proportion));

      //make sure neither track is smaller than its minimum height
      if (newTrackHeight < pTrack->GetMinimizedHeight())
         newTrackHeight = pTrack->GetMinimizedHeight();
      if (newUpperTrackHeight < prev->GetMinimizedHeight())
         newUpperTrackHeight = prev->GetMinimizedHeight();

      pTrack->SetHeight(newTrackHeight);
      prev->SetHeight(newUpperTrackHeight);
   };

   auto doResize = [&] {
      int newTrackHeight = mInitialTrackHeight + delta;
      if (newTrackHeight < pTrack->GetMinimizedHeight())
         newTrackHeight = pTrack->GetMinimizedHeight();
      pTrack->SetHeight(newTrackHeight);
   };

   //STM: We may be dragging one or two (stereo) tracks.
   // If two, resize proportionally if we are dragging the lower track, and
   // adjust compensatively if we are dragging the upper track.

   switch( mMode )
   {
      case IsResizingBelowLinkedTracks:
      {
         Track *prev = tracks->GetPrev(pTrack.get());
         doResizeBelow(prev, false);
         break;
      }
      case IsResizing:
      {
         doResize();
         break;
      }
      default:
         // don't refresh in this case.
         return RefreshCode::RefreshNone;
   }

   return RefreshCode::RefreshAll;
}

HitTestPreview TrackPanelResizeHandle::Preview
(const TrackPanelMouseState &, const AudacityProject *)
{
   return HitPreview(mMode == IsResizingBetweenLinkedTracks);
}

UIHandle::Result TrackPanelResizeHandle::Release
(const TrackPanelMouseEvent &, AudacityProject *pProject,
 wxWindow *)
{
   ///  This happens when the button is released from a drag.
   ///  Since we actually took care of resizing the track when
   ///  we got drag events, all we have to do here is clean up.
   ///  We also modify the undo state (the action doesn't become
   ///  undo-able, but it gets merged with the previous undo-able
   ///  event).
   pProject->ModifyState();
   return RefreshCode::FixScrollbars;
}

UIHandle::Result TrackPanelResizeHandle::Cancel(AudacityProject *pProject)
{
   auto pTrack = pProject->GetTracks()->Lock(mpTrack);
   if ( !pTrack )
      return RefreshCode::Cancelled;

   TrackList *const tracks = pProject->GetTracks();

   switch (mMode) {
   case IsResizing:
   {
      pTrack->SetHeight(mInitialActualHeight);
      pTrack->SetMinimized(mInitialMinimized);
   }
   break;
   case IsResizingBetweenLinkedTracks:
   {
      Track *const next = tracks->GetNext(pTrack.get());
      pTrack->SetHeight(mInitialUpperActualHeight);
      pTrack->SetMinimized(mInitialMinimized);
      next->SetHeight(mInitialActualHeight);
      next->SetMinimized(mInitialMinimized);
   }
   break;
   case IsResizingBelowLinkedTracks:
   {
      Track *const prev = tracks->GetPrev(pTrack.get());
      pTrack->SetHeight(mInitialActualHeight);
      pTrack->SetMinimized(mInitialMinimized);
      prev->SetHeight(mInitialUpperActualHeight);
      prev->SetMinimized(mInitialMinimized);
   }
   break;
   }

   return RefreshCode::RefreshAll;
}
