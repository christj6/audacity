/**********************************************************************

Audacity: A Digital Audio Editor

TrackButtonHandles.cpp

Paul Licameli split from TrackPanel.cpp

**********************************************************************/

#include "../../Audacity.h"
#include "TrackButtonHandles.h"

#include "../../HitTestResult.h"
#include "../../Project.h"
#include "../../RefreshCode.h"
#include "../../Track.h"
#include "../../TrackPanel.h"

CloseButtonHandle::CloseButtonHandle
( const std::shared_ptr<Track> &pTrack, const wxRect &rect )
   : ButtonHandle{ pTrack, rect }
{}

CloseButtonHandle::~CloseButtonHandle()
{
}

UIHandle::Result CloseButtonHandle::CommitChanges
(const wxMouseEvent &, AudacityProject *pProject, wxWindow*)
{
   using namespace RefreshCode;
   Result result = RefreshNone;

   auto pTrack = mpTrack.lock();
   if (pTrack)
   {
      pProject->StopIfPaused();
      if (!pProject->IsAudioActive()) {
         // This pushes an undo item:
         pProject->RemoveTrack(pTrack.get());
         // Redraw all tracks when any one of them closes
         // (Could we invent a return code that draws only those at or below
         // the affected track?)
         result |= Resize | RefreshAll | FixScrollbars | DestroyedCell;
      }
   }

   return result;
}

wxString CloseButtonHandle::Tip(const wxMouseState &) const
{
   auto name = _("Close");
   auto project = ::GetActiveProject();
   auto focused =
      project->GetTrackPanel()->GetFocusedTrack() == GetTrack().get();
   if (!focused)
      return name;

   auto commandManager = project->GetCommandManager();
   TranslatedInternalString command{ wxT("TrackClose"), name };
   return commandManager->DescribeCommandsAndShortcuts( &command, 1u );
}

UIHandlePtr CloseButtonHandle::HitTest
(std::weak_ptr<CloseButtonHandle> &holder,
 const wxMouseState &state, const wxRect &rect, TrackPanelCell *pCell)
{
   wxRect buttonRect;
   TrackInfo::GetCloseBoxRect(rect, buttonRect);

   if (buttonRect.Contains(state.m_x, state.m_y)) {
      auto pTrack = static_cast<CommonTrackPanelCell*>(pCell)->FindTrack();
      auto result = std::make_shared<CloseButtonHandle>( pTrack, buttonRect );
      result = AssignUIHandlePtr(holder, result);
      return result;
   }
   else
      return {};
}

////////////////////////////////////////////////////////////////////////////////

MenuButtonHandle::MenuButtonHandle
( const std::shared_ptr<TrackPanelCell> &pCell,
  const std::shared_ptr<Track> &pTrack, const wxRect &rect )
   : ButtonHandle{ pTrack, rect }
   , mpCell{ pCell }
{}

MenuButtonHandle::~MenuButtonHandle()
{
}

UIHandle::Result MenuButtonHandle::CommitChanges
(const wxMouseEvent &, AudacityProject *pProject, wxWindow *WXUNUSED(pParent))
{
   auto pPanel = pProject->GetTrackPanel();
   auto pCell = mpCell.lock();
   if (!pCell)
      return RefreshCode::Cancelled;
   auto pTrack =
      static_cast<CommonTrackPanelCell*>(pCell.get())->FindTrack();
   if (!pTrack)
      return RefreshCode::Cancelled;
   pPanel->CallAfter( [=]{ pPanel->OnTrackMenu( pTrack.get() ); } );
   return RefreshCode::RefreshNone;
}

wxString MenuButtonHandle::Tip(const wxMouseState &) const
{
   auto name = _("Open menu...");
   auto project = ::GetActiveProject();
   auto focused =
      project->GetTrackPanel()->GetFocusedTrack() == GetTrack().get();
   if (!focused)
      return name;

   auto commandManager = project->GetCommandManager();
   TranslatedInternalString command{ wxT("TrackMenu"), name };
   return commandManager->DescribeCommandsAndShortcuts( &command, 1u );
}

UIHandlePtr MenuButtonHandle::HitTest
(std::weak_ptr<MenuButtonHandle> &holder,
 const wxMouseState &state, const wxRect &rect,
 const std::shared_ptr<TrackPanelCell> &pCell)
{
   wxRect buttonRect;
   TrackInfo::GetTitleBarRect(rect, buttonRect);

   if (buttonRect.Contains(state.m_x, state.m_y)) {
      auto pTrack = static_cast<CommonTrackPanelCell*>(pCell.get())->FindTrack();
      auto result = std::make_shared<MenuButtonHandle>( pCell, pTrack, buttonRect );
      result = AssignUIHandlePtr(holder, result);
      return result;
   }
   else
      return {};
}
