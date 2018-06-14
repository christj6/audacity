/**********************************************************************

  Audacity: A Digital Audio Editor

  BatchCommandDialog.cpp

  Dominic Mazzoni
  James Crook

*******************************************************************//*!

\class MacroCommandDialog
\brief Provides a list of configurable commands for use with MacroCommands

Provides a list of commands, mostly effects, which can be chained
together in a simple linear sequence.  Can configure parameters on each
selected command.

*//*******************************************************************/

#include "Audacity.h"

#include <wx/defs.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/intl.h>
#include <wx/sizer.h>
#include <wx/statbox.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/listctrl.h>
#include <wx/radiobut.h>
#include <wx/button.h>
#include <wx/string.h>
#include <wx/dialog.h>


#include "Project.h"
#include "BatchCommandDialog.h"
#include "commands/CommandManager.h"
#include "BatchCommands.h"
#include "ShuttleGui.h"
#include "widgets/HelpSystem.h"


#define CommandsListID        7001
#define EditParamsButtonID    7002
#define UsePresetButtonID     7003

BEGIN_EVENT_TABLE(MacroCommandDialog, wxDialogWrapper)
END_EVENT_TABLE();

MacroCommandDialog::MacroCommandDialog(wxWindow * parent, wxWindowID id):
   wxDialogWrapper(parent, id, _("Select Command"),
            wxDefaultPosition, wxDefaultSize,
            wxCAPTION | wxRESIZE_BORDER)
   , mCatalog( GetActiveProject() )
{
   SetLabel(_("Select Command"));         // Provide visual label
   SetName(_("Select Command"));          // Provide audible label
}