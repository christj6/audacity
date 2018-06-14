/**********************************************************************

  Audacity: A Digital Audio Editor

  BatchCommandDialog.h

  Dominic Mazzoni
  James Crook

**********************************************************************/

#ifndef __AUDACITY_MACRO_COMMAND_DIALOG__
#define __AUDACITY_MACRO_COMMAND_DIALOG__

#include <wx/defs.h>
#include <wx/string.h>


#ifdef __WXMSW__
    #include  <wx/ownerdrw.h>
#endif

//#include  "wx/log.h"
#include  <wx/sizer.h>
#include  <wx/menuitem.h>
#include  <wx/checklst.h>

#include "BatchCommands.h"

class wxWindow;
class wxCheckBox;
class wxChoice;
class wxTextCtrl;
class wxStaticText;
class wxRadioButton;
class wxListCtrl;
class wxListEvent;
class wxButton;
class ShuttleGui;

class MacroCommandDialog final : public wxDialogWrapper {
 public:
   // constructors and destructors
   MacroCommandDialog(wxWindow *parent, wxWindowID id);
 public:
   wxString   mSelectedCommand;
   wxString   mSelectedParameters;
 private:
   wxString GetHelpPageName() { return wxT("Scripting Reference") ; }

   wxButton   *mEditParams;
   wxButton   *mUsePreset;
   wxListCtrl *mChoices;
   wxTextCtrl * mCommand;
   wxTextCtrl * mParameters;
   wxTextCtrl * mDetails;

   wxString mInternalCommandName;

   const MacroCommandsCatalog mCatalog;

   DECLARE_EVENT_TABLE()
};


#endif
