/**********************************************************************

Audacity: A Digital Audio Editor

WaveTrackVZoomHandle.cpp

Paul Licameli split from TrackPanel.cpp

**********************************************************************/

#include "../../../../Audacity.h"
#include "WaveTrackVZoomHandle.h"
#include "../../../../Experimental.h"
#include "WaveTrackVRulerControls.h"

#include "../../../../HitTestResult.h"
#include "../../../../NumberScale.h"
#include "../../../../prefs/SpectrogramSettings.h"
#include "../../../../prefs/WaveformSettings.h"
#include "../../../../Project.h"
#include "../../../../RefreshCode.h"
#include "../../../../TrackPanelMouseEvent.h"
#include "../../../../WaveTrack.h"
#include "../../../../widgets/PopupMenuTable.h"
#include "../../../../../images/Cursors.h"
#include "../../../../Prefs.h"

namespace
{

struct InitMenuData
{
public:
   WaveTrack *pTrack;
   wxRect rect;
   unsigned result;
   int yy;
};

}

WaveTrackVZoomHandle::WaveTrackVZoomHandle
(const std::shared_ptr<WaveTrack> &pTrack, const wxRect &rect, int y)
   : mpTrack{ pTrack } , mZoomStart(y), mZoomEnd(y), mRect(rect)
{
}

void WaveTrackVZoomHandle::Enter(bool)
{
}

enum {
   OnZoomResetID = 20000,
   OnZoomDiv2ID,
   OnZoomTimes2ID,
   OnZoomInVerticalID,
   OnZoomOutVerticalID,

   // Reserve an ample block of ids for waveform scale types
   OnFirstWaveformScaleID,
   OnLastWaveformScaleID = OnFirstWaveformScaleID + 9,

   // Reserve an ample block of ids for spectrum scale types
   OnFirstSpectrumScaleID,
   OnLastSpectrumScaleID = OnFirstSpectrumScaleID + 19,
};

///////////////////////////////////////////////////////////////////////////////
// Table class

class WaveTrackVRulerMenuTable : public PopupMenuTable
{
protected:
   WaveTrackVRulerMenuTable() {}

   void InitMenu(Menu *pMenu, void *pUserData) override;

private:
   void DestroyMenu() override
   {
      mpData = nullptr;
   }

protected:
   InitMenuData *mpData {};

   void OnZoomReset(wxCommandEvent&){ };
   void OnZoomDiv2Vertical(wxCommandEvent&){ };
   void OnZoomTimes2Vertical(wxCommandEvent&){ };
   void OnZoomInVertical(wxCommandEvent&){ };
   void OnZoomOutVertical(wxCommandEvent&){ };
};

void WaveTrackVRulerMenuTable::InitMenu(Menu *, void *pUserData)
{
   mpData = static_cast<InitMenuData*>(pUserData);
}

///////////////////////////////////////////////////////////////////////////////
// Table class

class WaveformVRulerMenuTable : public WaveTrackVRulerMenuTable
{
   WaveformVRulerMenuTable() : WaveTrackVRulerMenuTable() {}
   virtual ~WaveformVRulerMenuTable() {}
   DECLARE_POPUP_MENU(WaveformVRulerMenuTable);

public:
   static WaveformVRulerMenuTable &Instance();

private:
   virtual void InitMenu(Menu *pMenu, void *pUserData);
};

WaveformVRulerMenuTable &WaveformVRulerMenuTable::Instance()
{
   static WaveformVRulerMenuTable instance;
   return instance;
}

void WaveformVRulerMenuTable::InitMenu(Menu *pMenu, void *pUserData)
{
   WaveTrackVRulerMenuTable::InitMenu(pMenu, pUserData);
}

BEGIN_POPUP_MENU(WaveformVRulerMenuTable)

   POPUP_MENU_ITEM(OnZoomDiv2ID,        _("Zoom x1/2"),                     OnZoomDiv2Vertical)
   POPUP_MENU_ITEM(OnZoomTimes2ID,      _("Zoom x2"),                       OnZoomTimes2Vertical)
   POPUP_MENU_SEPARATOR()
   POPUP_MENU_ITEM(OnZoomInVerticalID,  _("Zoom In\tLeft-Click/Left-Drag"),  OnZoomInVertical)
   POPUP_MENU_ITEM(OnZoomOutVerticalID, _("Zoom Out\tShift-Left-Click"),     OnZoomOutVertical)
// The log and linear options are already available as waveform db.
// So don't repeat them here.
END_POPUP_MENU()

///////////////////////////////////////////////////////////////////////////////
// Table class

class SpectrumVRulerMenuTable : public WaveTrackVRulerMenuTable
{
   SpectrumVRulerMenuTable() : WaveTrackVRulerMenuTable() {}
   virtual ~SpectrumVRulerMenuTable() {}
   DECLARE_POPUP_MENU(SpectrumVRulerMenuTable);

public:
   static SpectrumVRulerMenuTable &Instance();

private:
   void InitMenu(Menu *pMenu, void *pUserData);

   void OnSpectrumScaleType(wxCommandEvent &evt);
};

SpectrumVRulerMenuTable &SpectrumVRulerMenuTable::Instance()
{
   static SpectrumVRulerMenuTable instance;
   return instance;
}

void SpectrumVRulerMenuTable::InitMenu(Menu *pMenu, void *pUserData)
{
	/*
   WaveTrackVRulerMenuTable::InitMenu(pMenu, pUserData);

   WaveTrack *const wt = mpData->pTrack;
   const int id =
      OnFirstSpectrumScaleID + (int)(wt->GetSpectrogramSettings().scaleType);
   pMenu->Check(id, true);
   */
}

BEGIN_POPUP_MENU(SpectrumVRulerMenuTable)

   {
      const wxArrayString & names = SpectrogramSettings::GetScaleNames();
      for (int ii = 0, nn = names.size(); ii < nn; ++ii) {
         POPUP_MENU_RADIO_ITEM(OnFirstSpectrumScaleID + ii, names[ii],
            OnSpectrumScaleType);
      }
   }

POPUP_MENU_SEPARATOR()
   POPUP_MENU_ITEM(OnZoomResetID,         _("Zoom Reset"),                     OnZoomReset)
   POPUP_MENU_ITEM(OnZoomInVerticalID,    _("Zoom In\tLeft-Click/Left-Drag"),  OnZoomInVertical)
   POPUP_MENU_ITEM(OnZoomOutVerticalID,   _("Zoom Out\tShift-Left-Click"),     OnZoomOutVertical)
END_POPUP_MENU()

void SpectrumVRulerMenuTable::OnSpectrumScaleType(wxCommandEvent &evt)
{
	/*
   WaveTrack *const wt = mpData->pTrack;
   // Assume linked track is wave or null
   const auto partner = static_cast<WaveTrack*>(wt->GetLink());
   const SpectrogramSettings::ScaleType newScaleType =
      SpectrogramSettings::ScaleType(
         std::max(0,
            std::min((int)(SpectrogramSettings::stNumScaleTypes) - 1,
               evt.GetId() - OnFirstSpectrumScaleID
      )));
   if (wt->GetSpectrogramSettings().scaleType != newScaleType) {
      wt->GetIndependentSpectrogramSettings().scaleType = newScaleType;
      if (partner)
         partner->GetIndependentSpectrogramSettings().scaleType = newScaleType;

      ::GetActiveProject()->ModifyState();

      using namespace RefreshCode;
      mpData->result = UpdateVRuler | RefreshAll;
   }
   */
}

///////////////////////////////////////////////////////////////////////////////

HitTestPreview WaveTrackVZoomHandle::HitPreview(const wxMouseState &state)
{
   static auto zoomInCursor =
      ::MakeCursor(wxCURSOR_MAGNIFIER, ZoomInCursorXpm, 19, 15);
   static auto zoomOutCursor =
      ::MakeCursor(wxCURSOR_MAGNIFIER, ZoomOutCursorXpm, 19, 15);
   static  wxCursor arrowCursor{ wxCURSOR_ARROW };
   bool bVZoom;
   gPrefs->Read(wxT("/GUI/VerticalZooming"), &bVZoom, false);
   bVZoom &= !state.RightIsDown();
   const auto message = bVZoom ? 
      _("Click to vertically zoom in. Shift-click to zoom out. Drag to specify a zoom region.") :
      _("Right-click for menu.");

   return {
      message,
      bVZoom ? (state.ShiftDown() ? &*zoomOutCursor : &*zoomInCursor) : &arrowCursor
      // , message
   };
}

WaveTrackVZoomHandle::~WaveTrackVZoomHandle()
{
}

UIHandle::Result WaveTrackVZoomHandle::Click
(const TrackPanelMouseEvent &, AudacityProject *)
{
   return RefreshCode::RefreshNone;
}

UIHandle::Result WaveTrackVZoomHandle::Drag
(const TrackPanelMouseEvent &evt, AudacityProject *pProject)
{
   using namespace RefreshCode;
   auto pTrack = pProject->GetTracks()->Lock(mpTrack);
   if (!pTrack)
      return Cancelled;

   const wxMouseEvent &event = evt.event;
   if ( event.RightIsDown() )
      return RefreshNone;
   mZoomEnd = event.m_y;
   return RefreshNone;
}

HitTestPreview WaveTrackVZoomHandle::Preview
(const TrackPanelMouseState &st, const AudacityProject *)
{
   return HitPreview(st.state);
}

UIHandle::Result WaveTrackVZoomHandle::Release
(const TrackPanelMouseEvent &evt, AudacityProject *pProject,
 wxWindow *pParent)
{
   using namespace RefreshCode;
   auto pTrack = pProject->GetTracks()->Lock(mpTrack);
   if (!pTrack)
      return RefreshNone;

   const wxMouseEvent &event = evt.event;
   const bool shiftDown = event.ShiftDown();
   const bool rightUp = event.RightUp();


   bool bVZoom;
   gPrefs->Read(wxT("/GUI/VerticalZooming"), &bVZoom, false);

   // Popup menu... 
   if (
       rightUp &&
       !(event.ShiftDown() || event.CmdDown()))
   {
      InitMenuData data {
         pTrack.get(), mRect, RefreshCode::RefreshNone, event.m_y
      };

      PopupMenuTable *const pTable =
         (pTrack->GetDisplay() == WaveTrack::Spectrum)
         ? (PopupMenuTable *) &SpectrumVRulerMenuTable::Instance()
         : (PopupMenuTable *) &WaveformVRulerMenuTable::Instance();
      std::unique_ptr<PopupMenuTable::Menu>
         pMenu(PopupMenuTable::BuildMenu(pParent, pTable, &data));

      // Accelerators only if zooming enabled.
      if( !bVZoom )
      {
         wxMenuItemList & L = pMenu->GetMenuItems();
         // let's iterate over the list in STL syntax
         wxMenuItemList::iterator iter;
         for (iter = L.begin(); iter != L.end(); ++iter)
         {
             wxMenuItem *pItem = *iter;
             // Remove accelerator, if any.
             pItem->SetItemLabel( (pItem->GetItemLabel() + "\t" ).BeforeFirst('\t') );
         }
      }


      pParent->PopupMenu(pMenu.get(), event.m_x, event.m_y);

      return data.result;
   }
   else{
      // Ignore Capture Lost event 
      bVZoom &= event.GetId() != kCaptureLostEventId;
      // shiftDown | rightUp | ZoomKind
      //    T      |    T    | 1to1
      //    T      |    F    | Out
      //    F      |    -    | In
      if( bVZoom ){
         if( shiftDown )
            mZoomStart=mZoomEnd;
      }
   }

   return UpdateVRuler | RefreshAll;
}

UIHandle::Result WaveTrackVZoomHandle::Cancel(AudacityProject*)
{
   // Cancel is implemented!  And there is no initial state to restore,
   // so just return a code.
   return RefreshCode::RefreshAll;
}

