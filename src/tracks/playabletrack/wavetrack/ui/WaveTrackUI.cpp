/**********************************************************************

Audacity: A Digital Audio Editor

WaveTrackUI.cpp

Paul Licameli split from TrackPanel.cpp

**********************************************************************/

#include "../../../../WaveTrack.h"
#include "WaveTrackControls.h"
#include "WaveTrackVRulerControls.h"

#include "../../../../HitTestResult.h"
#include "../../../../Project.h"
#include "../../../../TrackPanelMouseEvent.h"
#include "../../../../toolbars/ToolsToolBar.h"

#include "CutlineHandle.h"
#include "../../../ui/SelectHandle.h"

std::vector<UIHandlePtr> WaveTrack::DetailedHitTest
(const TrackPanelMouseState &st,
 const AudacityProject *pProject, int currentTool, bool bMultiTool)
{
   // This is the only override of Track::DetailedHitTest that still
   // depends on the state of the Tools toolbar.
   // If that toolbar were eliminated, this could simplify to a sequence of
   // hit test routines describable by a table.

   UIHandlePtr result;
   std::vector<UIHandlePtr> results;
   bool isWaveform = (GetDisplay() == WaveTrack::Waveform);

   // Some special targets are not drawn in spectrogram,
   // so don't hit them in such views.
   if (isWaveform) {
      UIHandlePtr result;
      if (NULL != (result = CutlineHandle::HitTest(
         mCutlineHandle, st.state, st.rect,
         pProject, Pointer<WaveTrack>(this))))
         // This overriding test applies in all tools
         results.push_back(result);
   }

   return results;
}

std::shared_ptr<TrackControls> WaveTrack::GetControls()
{
   return std::make_shared<WaveTrackControls>( Pointer( this ) );
}

std::shared_ptr<TrackVRulerControls> WaveTrack::GetVRulerControls()
{
   return std::make_shared<WaveTrackVRulerControls>( Pointer( this ) );
}
