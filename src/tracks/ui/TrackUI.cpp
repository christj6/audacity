/**********************************************************************

Audacity: A Digital Audio Editor

TrackUI.cpp

Paul Licameli split from TrackPanel.cpp

**********************************************************************/

#include "../../Track.h"
#include "../../TrackPanelMouseEvent.h"
#include "TrackControls.h"
#include "TrackVRulerControls.h"

#include "../../HitTestResult.h"
#include "../../Project.h"
#include "../../toolbars/ToolsToolBar.h"

#include "../ui/SelectHandle.h"
#include "ZoomHandle.h"
#include "../../TrackPanelResizerCell.h"
#include "BackgroundCell.h"

std::vector<UIHandlePtr> Track::HitTest
(const TrackPanelMouseState &st,
 const AudacityProject *pProject)
{
   UIHandlePtr result;
   std::vector<UIHandlePtr> results;
   const ToolsToolBar * pTtb = pProject->GetToolsToolBar();
   const bool isMultiTool = pTtb->IsDown(multiTool);
   const auto currentTool = pTtb->GetCurrentTool();

   if ( !isMultiTool && currentTool == zoomTool ) {
      // Zoom tool is a non-selecting tool that takes precedence in all tracks
      // over all other tools, no matter what detail you point at.
      result = ZoomHandle::HitAnywhere(
         pProject->GetBackgroundCell()->mZoomHandle);
      results.push_back(result);
      return results;
   }

   // In other tools, let subclasses determine detailed hits.
   results =
      DetailedHitTest( st, pProject, currentTool, isMultiTool );

   // There are still some general cases.

   // Let the multi-tool right-click handler apply only in default of all
   // other detailed hits.
   if ( isMultiTool ) {
      result = ZoomHandle::HitTest(
         pProject->GetBackgroundCell()->mZoomHandle, st.state);
      if (result)
         results.push_back(result);
   }

   // Finally, default of all is adjustment of the selection box.
   if ( isMultiTool || currentTool == selectTool ) {
      result = SelectHandle::HitTest(
         mSelectHandle, st, pProject, Pointer(this));
      if (result)
         results.push_back(result);
   }

   return results;
}

std::shared_ptr<TrackPanelCell> Track::GetTrackControl()
{
   if (!mpControls)
      // create on demand
      mpControls = GetControls();
   return mpControls;
}

std::shared_ptr<TrackPanelCell> Track::GetVRulerControl()
{
   if (!mpVRulerContols)
      // create on demand
      mpVRulerContols = GetVRulerControls();
   return mpVRulerContols;
}

#include "../../TrackPanelResizeHandle.h"
std::shared_ptr<TrackPanelCell> Track::GetResizer()
{
   if (!mpResizer)
      // create on demand
      mpResizer = std::make_shared<TrackPanelResizerCell>( Pointer( this ) );
   return mpResizer;
}
