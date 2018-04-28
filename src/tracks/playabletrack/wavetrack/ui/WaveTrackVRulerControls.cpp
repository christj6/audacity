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