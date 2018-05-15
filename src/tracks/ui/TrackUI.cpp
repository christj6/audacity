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

#include "../ui/SelectHandle.h"
#include "../../TrackPanelResizerCell.h"
#include "BackgroundCell.h"

std::vector<UIHandlePtr> Track::HitTest
(const TrackPanelMouseState &st,
 const AudacityProject *pProject)
{
   UIHandlePtr result;
   std::vector<UIHandlePtr> results;

   // In other tools, let subclasses determine detailed hits.
   results =
      DetailedHitTest( st, pProject );

   result = SelectHandle::HitTest(mSelectHandle, st, pProject, Pointer(this));
   if (result)
      results.push_back(result);

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
