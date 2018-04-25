/**********************************************************************

Audacity: A Digital Audio Editor

BackgroundCell.h

Paul Licameli split from TrackPanel.cpp

**********************************************************************/

#ifndef __AUDACITY_BACKGROUND_CELL__
#define __AUDACITY_BACKGROUND_CELL__

#include "CommonTrackPanelCell.h"
#include "../../MemoryX.h"

class AudacityProject;

class BackgroundHandle;

class BackgroundCell final : public CommonTrackPanelCell
{
public:
   BackgroundCell(AudacityProject *pProject)
      : mpProject(pProject)
   {}

   virtual ~BackgroundCell();

protected:
   std::vector<UIHandlePtr> HitTest
      (const TrackPanelMouseState &state,
       const AudacityProject *) override;

   std::shared_ptr<Track> FindTrack() override;

private:
   AudacityProject *mpProject;

   std::weak_ptr<BackgroundHandle> mHandle;
};

#endif
