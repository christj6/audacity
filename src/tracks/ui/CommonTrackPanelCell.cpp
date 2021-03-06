/**********************************************************************

Audacity: A Digital Audio Editor

CommonTrackPanelCell.cpp

Paul Licameli split from TrackPanel.cpp

**********************************************************************/

#include "../../Audacity.h"
#include "CommonTrackPanelCell.h"

#include "../../Experimental.h"
#include "../../HitTestResult.h"
#include "../../Project.h"
#include "../../RefreshCode.h"
#include "../../Track.h"
#include "../../TrackPanel.h"
#include "../../TrackPanelMouseEvent.h"

CommonTrackPanelCell::~CommonTrackPanelCell()
{
}

unsigned CommonTrackPanelCell::HandleWheelRotation
(const TrackPanelMouseEvent &evt, AudacityProject *pProject)
{
   using namespace RefreshCode;

   if (pProject->GetTracks()->empty())
      // Scrolling and Zoom in and out commands are disabled when there are no tracks.
      // This should be disabled too for consistency.  Otherwise
      // you do see changes in the time ruler.
      return Cancelled;

   unsigned result = RefreshAll;
   const wxMouseEvent &event = evt.event;
   ViewInfo &viewInfo = pProject->GetViewInfo();
   const auto steps = evt.steps;

   if (event.ShiftDown())
   {
      // MM: Scroll left/right when used with Shift key down
      pProject->TP_ScrollWindow(
         viewInfo.OffsetTimeByPixels(
            viewInfo.PositionToTime(0), 50.0 * -steps));
   }
   else if (event.CmdDown())
   {
      // MM: Zoom in/out when used with Control key down
      // We're converting pixel positions to times,
      // counting pixels from the left edge of the track.
      int trackLeftEdge = pProject->GetTrackPanel()->GetLeftOffset();

      // Time corresponding to mouse position
      wxCoord xx;
      double center_h;
      double mouse_h = viewInfo.PositionToTime(event.m_x, trackLeftEdge);

      // Zooming out? Focus on mouse.
      if( steps <= 0 )
         center_h = mouse_h;
      // No Selection? Focus on mouse.
      else if((viewInfo.selectedRegion.t1() - viewInfo.selectedRegion.t0() ) < 0.00001  )
         center_h = mouse_h;
      // Before Selection? Focus on left
      else if( mouse_h < viewInfo.selectedRegion.t0() )
         center_h = viewInfo.selectedRegion.t0();
      // After Selection? Focus on right
      else if( mouse_h > viewInfo.selectedRegion.t1() )
         center_h = viewInfo.selectedRegion.t1();
      // Inside Selection? Focus on mouse
      else
         center_h = mouse_h;

      xx = viewInfo.TimeToPosition(center_h, trackLeftEdge);

      // Time corresponding to last (most far right) audio.
      double audioEndTime = pProject->GetTracks()->GetEndTime();

      wxCoord xTrackEnd = viewInfo.TimeToPosition( audioEndTime );
      viewInfo.ZoomBy(pow(2.0, steps));

      double new_center_h = viewInfo.PositionToTime(xx, trackLeftEdge);
      viewInfo.h += (center_h - new_center_h);

      // If wave has gone off screen, bring it back.
      // This means that the end of the track stays where it was.
      if( viewInfo.h > audioEndTime )
         viewInfo.h += audioEndTime - viewInfo.PositionToTime( xTrackEnd );


      result |= FixScrollbars;
   }
   else
   {
		// MM: Scroll up/down when used without modifier keys
		double lines = steps * 4 + mVertScrollRemainder;
		mVertScrollRemainder = lines - floor(lines);
		lines = floor(lines);
		auto didSomething = pProject->TP_ScrollUpDown((int)-lines);
		if (!didSomething)
			result |= Cancelled;
   }

   return result;
}
