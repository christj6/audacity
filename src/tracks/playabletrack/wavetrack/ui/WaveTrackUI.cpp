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

#include "CutlineHandle.h"
#include "../../../ui/SelectHandle.h"

std::shared_ptr<TrackControls> WaveTrack::GetControls()
{
   return std::make_shared<WaveTrackControls>( Pointer( this ) );
}
