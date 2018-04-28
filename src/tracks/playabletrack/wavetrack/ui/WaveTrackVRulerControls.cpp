/**********************************************************************

Audacity: A Digital Audio Editor

WaveTrackVRulerControls.cpp

Paul Licameli split from TrackPanel.cpp

**********************************************************************/

#include "../../../../Audacity.h"
#include "WaveTrackVRulerControls.h"
#include "WaveTrackVZoomHandle.h"

#include "../../../../HitTestResult.h"
#include "../../../../NumberScale.h"
#include "../../../../prefs/SpectrogramSettings.h"
#include "../../../../prefs/WaveformSettings.h"
#include "../../../../Project.h"
#include "../../../../RefreshCode.h"
#include "../../../../TrackPanelMouseEvent.h"
#include "../../../../WaveTrack.h"

///////////////////////////////////////////////////////////////////////////////
WaveTrackVRulerControls::~WaveTrackVRulerControls()
{
}

std::vector<UIHandlePtr> WaveTrackVRulerControls::HitTest
(const TrackPanelMouseState &st,
 const AudacityProject *pProject)
{
   std::vector<UIHandlePtr> results;

   if ( st.state.GetX() <= st.rect.GetRight() - kGuard ) {
      auto pTrack = Track::Pointer<WaveTrack>( FindTrack().get() );
      if (pTrack) {
         auto result = std::make_shared<WaveTrackVZoomHandle>(
            pTrack, st.rect, st.state.m_y );
         result = AssignUIHandlePtr(mVZoomHandle, result);
         results.push_back(result);
      }
   }

   auto more = TrackVRulerControls::HitTest(st, pProject);
   std::copy(more.begin(), more.end(), std::back_inserter(results));

   return results;
}

void WaveTrackVRulerControls::DoZoomPreset( int i)
{
	/*
   const auto pTrack = FindTrack();
   if (!pTrack)
      return;
   wxASSERT(pTrack->GetKind() == Track::Wave);

   const auto wt = static_cast<WaveTrack*>(pTrack.get());
		 */
}
