/**********************************************************************

Audacity: A Digital Audio Editor

WavelTrackButtonHandles.h

Paul Licameli split from TrackPanel.cpp

**********************************************************************/

#ifndef __AUDACITY_TRACK_BUTTON_HANDLES__
#define __AUDACITY_TRACK_BUTTON_HANDLES__

#include "../ui/ButtonHandle.h"

class wxMouseState;

class CloseButtonHandle final : public ButtonHandle
{
   CloseButtonHandle(const CloseButtonHandle&) = delete;

protected:
   Result CommitChanges
      (const wxMouseEvent &event, AudacityProject *pProject, wxWindow *pParent)
      override;

   wxString Tip(const wxMouseState &state) const override;

   bool StopsOnKeystroke () override { return true; }
   
public:
   explicit CloseButtonHandle
      ( const std::shared_ptr<Track> &pTrack, const wxRect &rect );

   CloseButtonHandle &operator=(const CloseButtonHandle&) = default;

   virtual ~CloseButtonHandle();

   static UIHandlePtr HitTest
      (std::weak_ptr<CloseButtonHandle> &holder,
       const wxMouseState &state, const wxRect &rect, TrackPanelCell *pCell);
};

////////////////////////////////////////////////////////////////////////////////

#include <wx/event.h>
#include "../../widgets/PopupMenuTable.h"

class MenuButtonHandle final : public ButtonHandle
{
   MenuButtonHandle(const MenuButtonHandle&) = delete;

protected:
   Result CommitChanges
      (const wxMouseEvent &event, AudacityProject *pProject, wxWindow *pParent)
      override;

   wxString Tip(const wxMouseState &state) const override;

public:
   explicit MenuButtonHandle
      ( const std::shared_ptr<TrackPanelCell> &pCell,
        const std::shared_ptr<Track> &pTrack, const wxRect &rect );

   MenuButtonHandle &operator=(const MenuButtonHandle&) = default;

   virtual ~MenuButtonHandle();

   static UIHandlePtr HitTest
      (std::weak_ptr<MenuButtonHandle> &holder,
       const wxMouseState &state, const wxRect &rect,
       const std::shared_ptr<TrackPanelCell> &pCell);

private:
   std::weak_ptr<TrackPanelCell> mpCell;
};

#endif
