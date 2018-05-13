/**********************************************************************

  Audacity: A Digital Audio Editor

  EditToolBar.cpp

  Dominic Mazzoni
  Shane T. Mueller
  Leland Lucius

  See EditToolBar.h for details

*******************************************************************//*!

\class EditToolBar
\brief A ToolBar that has the edit buttons on it.

  This class, which is a child of Toolbar, creates the
  window containing interfaces to commonly-used edit
  functions that are otherwise only available through
  menus. The window can be embedded within a normal project
  window, or within a ToolbarFrame that is managed by a
  global ToolBarStub called gControlToolBarStub.

  All of the controls in this window were custom-written for
  Audacity - they are not native controls on any platform -
  however, it is intended that the images could be easily
  replaced to allow "skinning" or just customization to
  match the look and feel of each platform.

*//*******************************************************************/


#include "../Audacity.h"
#include "EditToolBar.h"

// For compilers that support precompilation, includes "wx/wx.h".
#include <wx/wxprec.h>

#ifndef WX_PRECOMP
#include <wx/event.h>
#include <wx/image.h>
#include <wx/intl.h>
#include <wx/sizer.h>
#include <wx/tooltip.h>
#endif

#include "../AllThemeResources.h"
#include "../AudioIO.h"
#include "../ImageManipulation.h"
#include "../Internat.h"
#include "../Prefs.h"
#include "../Project.h"
#include "../Theme.h"
#include "../Track.h"
#include "../UndoManager.h"
#include "../widgets/AButton.h"

#include "../Experimental.h"
#include "../commands/CommandContext.h"

IMPLEMENT_CLASS(EditToolBar, ToolBar);

const int BUTTON_WIDTH = 27;
const int SEPARATOR_WIDTH = 14;

////////////////////////////////////////////////////////////
/// Methods for EditToolBar
////////////////////////////////////////////////////////////

BEGIN_EVENT_TABLE( EditToolBar, ToolBar )
   EVT_COMMAND_RANGE( ETBCutID+first_ETB_ID,
                      ETBCutID+first_ETB_ID + ETBNumButtons - 1,
                      wxEVT_COMMAND_BUTTON_CLICKED,
                      EditToolBar::OnButton )
END_EVENT_TABLE()

//Standard contructor
EditToolBar::EditToolBar()
: ToolBar(EditBarID, _("Edit"), wxT("Edit"))
{
}

EditToolBar::~EditToolBar()
{
}

void EditToolBar::Create(wxWindow * parent)
{
   ToolBar::Create(parent);
}

void EditToolBar::AddSeparator()
{
   AddSpacer();
}

/// This is a convenience function that allows for button creation in
/// MakeButtons() with fewer arguments
/// Very similar to code in ControlToolBar...
AButton *EditToolBar::AddButton(
   EditToolBar *pBar,
   teBmps eEnabledUp, teBmps eEnabledDown, teBmps eDisabled,
   int id,
   const wxChar *label,
   bool toggle)
{
   AButton *&r = pBar->mButtons[id];

   r = ToolBar::MakeButton(pBar,
      bmpRecoloredUpSmall, bmpRecoloredDownSmall, bmpRecoloredUpHiliteSmall, bmpRecoloredHiliteSmall,
      eEnabledUp, eEnabledDown, eDisabled,
      wxWindowID(id+first_ETB_ID),
      wxDefaultPosition,
      toggle,
      theTheme.ImageSize( bmpRecoloredUpSmall ));

   r->SetLabel(label);
// JKC: Unlike ControlToolBar, does not have a focus rect.  Shouldn't it?
// r->SetFocusRect( r->GetRect().Deflate( 4, 4 ) );

   pBar->Add( r, 0, wxALIGN_CENTER );

   return r;
}

void EditToolBar::Populate()
{
   SetBackgroundColour( theTheme.Colour( clrMedium  ) );
   MakeButtonBackgroundsSmall();

   /* Buttons */
   // Tooltips slightly more verbose than the menu entries are.
   AddButton(this, bmpCut, bmpCut, bmpCutDisabled, ETBCutID,
      _("Cut selection"));
   AddButton(this, bmpCopy, bmpCopy, bmpCopyDisabled, ETBCopyID,
      _("Copy selection"));
   AddButton(this, bmpPaste, bmpPaste, bmpPasteDisabled, ETBPasteID,
      _("Paste"));

   AddSeparator();

   AddButton(this, bmpUndo, bmpUndo, bmpUndoDisabled, ETBUndoID,
      _("Undo"));
   AddButton(this, bmpRedo, bmpRedo, bmpRedoDisabled, ETBRedoID,
      _("Redo"));

   AddSeparator();

   // Tooltips match menu entries.
   // We previously had longer tooltips which were not more clear.
   AddButton(this, bmpZoomIn, bmpZoomIn, bmpZoomInDisabled, ETBZoomInID,
      _("Zoom In"));
   AddButton(this, bmpZoomOut, bmpZoomOut, bmpZoomOutDisabled, ETBZoomOutID,
      _("Zoom Out"));
   AddButton(this, bmpZoomSel, bmpZoomSel, bmpZoomSelDisabled, ETBZoomSelID,
      _("Zoom to Selection"));
   AddButton(this, bmpZoomFit, bmpZoomFit, bmpZoomFitDisabled, ETBZoomFitID,
      _("Fit to Width"));

   mButtons[ETBZoomInID]->SetEnabled(false);
   mButtons[ETBZoomOutID]->SetEnabled(false);

   mButtons[ETBZoomSelID]->SetEnabled(false);
   mButtons[ETBZoomFitID]->SetEnabled(false);
   mButtons[ETBPasteID]->SetEnabled(false);

   RegenerateTooltips();
}

void EditToolBar::UpdatePrefs()
{
   RegenerateTooltips();

   // Set label to pull in language change
   SetLabel(_("Edit"));

   // Give base class a chance
   ToolBar::UpdatePrefs();
}

void EditToolBar::RegenerateTooltips()
{
   ForAllButtons( ETBActTooltips );
}

void EditToolBar::EnableDisableButtons()
{
   ForAllButtons( ETBActEnableDisable );
}


static const struct Entry {
   int tool;
   wxString commandName;
   wxString untranslatedLabel;
} EditToolbarButtonList[] = {
   { ETBCutID,      wxT("Cut"),         XO("Cut")  },
   { ETBCopyID,     wxT("Copy"),        XO("Copy")  },
   { ETBPasteID,    wxT("Paste"),       XO("Paste")  },
   { ETBUndoID,     wxT("Undo"),        XO("Undo")  },
   { ETBRedoID,     wxT("Redo"),        XO("Redo")  },

   { ETBZoomInID,   wxT("ZoomIn"),      XO("Zoom In")  },
   { ETBZoomOutID,  wxT("ZoomOut"),     XO("Zoom Out")  },
   { ETBZoomSelID,  wxT("ZoomSel"),     XO("Fit selection to width")  },
   { ETBZoomFitID,  wxT("FitInWindow"), XO("Fit project to width")  },
};


void EditToolBar::ForAllButtons(int Action)
{
   AudacityProject *p;
   CommandManager* cm = nullptr;

   if( Action & ETBActEnableDisable ){
      p = GetActiveProject();
      if (!p) return;
      cm = p->GetCommandManager();
      if (!cm) return;
   }


   for (const auto &entry : EditToolbarButtonList) {
#if wxUSE_TOOLTIPS
      if( Action & ETBActTooltips ){
         TranslatedInternalString command{
            entry.commandName, wxGetTranslation(entry.untranslatedLabel) };
         ToolBar::SetButtonToolTip( *mButtons[entry.tool], &command, 1u );
      }
#endif
      if (cm) {
         mButtons[entry.tool]->SetEnabled(cm->GetEnabled(entry.commandName));
      }
   }
}

void EditToolBar::OnButton(wxCommandEvent &event)
{
   int id = event.GetId()-first_ETB_ID;
   // Be sure the pop-up happens even if there are exceptions, except for buttons which toggle.
   auto cleanup = finally( [&] { mButtons[id]->InteractionOver();});

   AudacityProject *p = GetActiveProject();
   if (!p) return;
   CommandManager* cm = p->GetCommandManager();
   if (!cm) return;

   auto flags = p->GetUpdateFlags();
   const CommandContext context( *GetActiveProject() );
   cm->HandleTextualCommand(EditToolbarButtonList[id].commandName, flags, NoFlagsSpecifed);
}


