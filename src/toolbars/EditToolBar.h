/**********************************************************************

  Audacity: A Digital Audio Editor


  EditToolbar.h

  Dominic Mazzoni
  Shane T. Mueller
  Leland Lucius

**********************************************************************/

#ifndef __AUDACITY_EDIT_TOOLBAR__
#define __AUDACITY_EDIT_TOOLBAR__

#include <wx/defs.h>

#include "ToolBar.h"
#include "../Theme.h"
#include "../Experimental.h"

class wxCommandEvent;
class wxDC;
class wxImage;
class wxWindow;

class AButton;

enum {
   ETBCutID,
   ETBCopyID,
   ETBPasteID,

   ETBUndoID,
   ETBRedoID,

   ETBZoomInID,
   ETBZoomOutID,

   ETBZoomSelID,
   ETBZoomFitID,

   ETBNumButtons
};

const int first_ETB_ID = 11300;

// flags so 1,2,4,8 etc.
enum {
   ETBActTooltips = 1,
   ETBActEnableDisable = 2,
};

class EditToolBar final : public ToolBar {

 public:

   EditToolBar();
   virtual ~EditToolBar();

   void Create(wxWindow *parent) override;

   void OnButton(wxCommandEvent & event);

   void Populate() override;
   void Repaint(wxDC * WXUNUSED(dc)) override {};
   void EnableDisableButtons() override;
   void UpdatePrefs() override;

 private:

   static AButton *AddButton(
      EditToolBar *pBar,
      teBmps eEnabledUp, teBmps eEnabledDown, teBmps eDisabled,
      int id, const wxChar *label, bool toggle = false);

   void AddSeparator();

   void MakeButtons();

   void RegenerateTooltips() override;
   void ForAllButtons(int Action);

   AButton *mButtons[ETBNumButtons];

   wxImage *upImage;
   wxImage *downImage;
   wxImage *hiliteImage;

 public:

   DECLARE_CLASS(EditToolBar)
   DECLARE_EVENT_TABLE()
};

#endif

