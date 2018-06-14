/**********************************************************************

  Audacity: A Digital Audio Editor

  ApplyMacroDialog.cpp

  Dominic Mazzoni
  James Crook

*******************************************************************//*!

\class ApplyMacroDialog
\brief Shows progress in executing commands in MacroCommands.

*//*******************************************************************/

#include "Audacity.h"
#include "BatchProcessDialog.h"

#include <wx/defs.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/filedlg.h>
#include <wx/intl.h>
#include <wx/sizer.h>
#include <wx/statbox.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/listctrl.h>
#include <wx/radiobut.h>
#include <wx/button.h>
#include <wx/imaglist.h>
#include <wx/settings.h>

#include "AudacityException.h"
#include "ShuttleGui.h"
#include "Prefs.h"
#include "Project.h"
#include "Internat.h"
#include "commands/CommandManager.h"
#include "commands/CommandContext.h"
#include "../images/Arrow.xpm"
#include "../images/Empty9x16.xpm"
#include "BatchCommands.h"
#include "Track.h"
#include "UndoManager.h"

#include "Theme.h"
#include "AllThemeResources.h"

#include "FileDialog.h"
#include "FileNames.h"
#include "import/Import.h"
#include "widgets/ErrorDialog.h"
#include "widgets/HelpSystem.h"

#define MacrosListID       7001
#define CommandsListID     7002
#define ApplyToProjectID   7003
#define ApplyToFilesID     7004
#define ExpandID           7005
#define ShrinkID           7006

BEGIN_EVENT_TABLE(ApplyMacroDialog, wxDialogWrapper)
END_EVENT_TABLE()

ApplyMacroDialog::ApplyMacroDialog(wxWindow * parent, bool bInherited):
   wxDialogWrapper(parent, wxID_ANY, _("Macros Palette"),
            wxDefaultPosition, wxDefaultSize,
            wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
   , mCatalog( GetActiveProject() )
{
   //AudacityProject * p = GetActiveProject();
   mAbort = false;
   mbExpanded = false;
   if( bInherited )
      return;
   SetLabel(_("Macros Palette"));         // Provide visual label
   SetName(_("Macros Palette"));          // Provide audible label
   Populate();

}

ApplyMacroDialog::~ApplyMacroDialog()
{
}

void ApplyMacroDialog::Populate()
{
   //------------------------- Main section --------------------
   ShuttleGui S(this, eIsCreating);
   PopulateOrExchange(S);
   // ----------------------- End of main section --------------
   // Get and validate the currently active macro
   mActiveMacro = gPrefs->Read(wxT("/Batch/ActiveMacro"), wxT(""));
   // Go populate the macros list.
   PopulateMacros();

   Layout();
   Fit();
   wxSize sz = GetSize();
   SetSizeHints( sz );

   // Size and place window
   SetSize(std::min(wxSystemSettings::GetMetric(wxSYS_SCREEN_X) * 3 / 4, sz.GetWidth()),
           std::min(wxSystemSettings::GetMetric(wxSYS_SCREEN_Y) * 4 / 5, 400));

   Center();

   // Set the column size for the macros list.
   sz = mMacros->GetClientSize();
   mMacros->SetColumnWidth(0, sz.x);
}

/// Defines the dialog and does data exchange with it.
void ApplyMacroDialog::PopulateOrExchange(ShuttleGui &S)
{
   /*i18n-hint: A macro is a sequence of commands that can be applied
      * to one or more audio files.*/
   S.StartStatic(_("&Select Macro"), 1);
   {
      S.SetStyle(wxSUNKEN_BORDER | wxLC_REPORT | wxLC_HRULES | wxLC_VRULES |
                  wxLC_SINGLE_SEL);
      mMacros = S.Id(MacrosListID).Prop(1).AddListControlReportMode();
      mMacros->InsertColumn(0, _("Macro"), wxLIST_FORMAT_LEFT);
   }
   S.EndStatic();

   S.StartHorizontalLay(wxEXPAND, 0);
   {
      S.AddPrompt( _("Apply Macro to:") );
      S.Id(ApplyToProjectID).AddButton(_("&Project"));
      S.Id(ApplyToFilesID).AddButton(_("&Files..."));
   }
   S.EndHorizontalLay();

   S.StartHorizontalLay(wxEXPAND, 0);
   {
      mResize = S.Id(ExpandID).AddButton(_("&Expand"));
      S.Prop(1).AddSpace( 10 );
      S.AddStandardButtons( eCancelButton | eHelpButton);
   }
   S.EndHorizontalLay();
}

/// This clears and updates the contents of mMacros, the list of macros.
/// It has cut-and-paste code from PopulateList, and both should call 
/// a shared function.
void ApplyMacroDialog::PopulateMacros()
{
   wxArrayString names = mMacroCommands.GetNames();
   int i;

   int topItem = mMacros->GetTopItem();
   mMacros->DeleteAllItems();
   for (i = 0; i < (int)names.GetCount(); i++) {
      mMacros->InsertItem(i, names[i]);
   }

   int item = mMacros->FindItem(-1, mActiveMacro);
   bool bFound = item >=0;
   if (item == -1) {
      item = 0;
      mActiveMacro = mMacros->GetItemText(0);
   }

   // Select the name in the list...this will fire an event.
   mMacros->SetItemState(item, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);

   if( 0 <= topItem && topItem < (int)mMacros->GetItemCount())
   {
      // Workaround for scrolling being windows only.
      // Try to scroll back to where we once were...
      mMacros->EnsureVisible( (int)mMacros->GetItemCount() -1 );
      mMacros->EnsureVisible( topItem );
      // And then make sure whatever is selected is still visible...
      if( bFound )
         mMacros->EnsureVisible( item );
   }
}

wxString ApplyMacroDialog::MacroIdOfName( const wxString & MacroName )
{
   wxString Temp = MacroName;
   Temp.Replace(" ","");
   Temp = wxString( "Macro_" ) + Temp;
   return Temp;
}

// Apply macro, given its ID.
// Does nothing if not found, rather than returning an error.
void ApplyMacroDialog::ApplyMacroToProject( const wxString & MacroID, bool bHasGui )
{
   for( int i=0;i<mMacros->GetItemCount();i++){
      wxString name = mMacros->GetItemText(i);
      if( MacroIdOfName( name ) == MacroID ){
         ApplyMacroToProject( i, bHasGui );
         return;
      }
   }
}

// Apply macro, given its number in the list.
void ApplyMacroDialog::ApplyMacroToProject( int iMacro, bool bHasGui )
{
   wxString name = mMacros->GetItemText(iMacro);
   if( name.IsEmpty() )
      return;

   wxDialogWrapper activityWin( this, wxID_ANY, GetTitle());
   activityWin.SetName(activityWin.GetTitle());
   ShuttleGui S(&activityWin, eIsCreating);

   S.StartHorizontalLay(wxCENTER, false);
   {
      S.StartStatic( {}, false);   // deliberately not translated (!)
      {
         S.SetBorder(20);
         S.AddFixedText(wxString::Format(_("Applying '%s' to current project"),
                                         name));
      }
      S.EndStatic();
   }
   S.EndHorizontalLay();

   activityWin.Layout();
   activityWin.Fit();
   activityWin.CenterOnScreen();
   // Avoid overlap with progress.
   int x,y;
   activityWin.GetPosition( &x, &y );
   activityWin.Move(wxMax(0,x-300), 0);
   activityWin.Show();

   // Without this the newly created dialog may not show completely.
   wxYield();

   //Since we intend to keep this dialog open, there is no reason to hide it 
   //and then show it again.
   //if( bHasGui )
   //   Hide();

   gPrefs->Write(wxT("/Batch/ActiveMacro"), name);
   gPrefs->Flush();

   mMacroCommands.ReadMacro(name);

   // The disabler must get deleted before the EndModal() call.  Otherwise,
   // the menus on OSX will remain disabled.
   bool success;
   {
      wxWindowDisabler wd(&activityWin);
      success = GuardedCall< bool >(
         [this]{ return mMacroCommands.ApplyMacro(mCatalog); } );
   }

   if( !bHasGui )
      return;

   Show();
   Raise();
}

/////////////////////////////////////////////////////////////////////
#include <wx/textdlg.h>
#include "BatchCommandDialog.h"

enum {
   AddButtonID = 10000,
   RemoveButtonID,
   ImportButtonID,
   ExportButtonID,
   DefaultsButtonID,
   InsertButtonID,
   EditButtonID,
   DeleteButtonID,
   UpButtonID,
   DownButtonID,
   RenameButtonID,
// MacrosListID             7005
// CommandsListID,       7002
// Re-Use IDs from ApplyMacroDialog.
   ApplyToProjectButtonID = ApplyToProjectID,
   ApplyToFilesButtonID = ApplyToFilesID,
};

BEGIN_EVENT_TABLE(MacrosWindow, ApplyMacroDialog)
   EVT_BUTTON(InsertButtonID, MacrosWindow::OnInsert)
   EVT_BUTTON(EditButtonID, MacrosWindow::OnEditCommandParams)
   EVT_BUTTON(DeleteButtonID, MacrosWindow::OnDelete)
   EVT_BUTTON(UpButtonID, MacrosWindow::OnUp)
   EVT_BUTTON(DownButtonID, MacrosWindow::OnDown)
   EVT_BUTTON(DefaultsButtonID, MacrosWindow::OnDefaults)

   EVT_BUTTON(wxID_OK, MacrosWindow::OnOK)

   EVT_KEY_DOWN(MacrosWindow::OnKeyDown)
END_EVENT_TABLE()

enum {
   BlankColumn,
   ItemNumberColumn,
   ActionColumn,
   ParamsColumn,
};

/// Constructor
MacrosWindow::MacrosWindow(wxWindow * parent, bool bExpanded):
   ApplyMacroDialog(parent, true)
{
   mbExpanded = bExpanded;
   wxString Title = mbExpanded ? _("Manage Macros") : _("Macros Palette");
   SetLabel( Title );   // Provide visual label
   SetName(  Title );   // Provide audible label
   SetTitle( Title );

   mChanged = false;
   mSelectedCommand = 0;

   if( mbExpanded )
      Populate();
   else
      ApplyMacroDialog::Populate();
}

MacrosWindow::~MacrosWindow()
{
}

/// Creates the dialog and its contents.
void MacrosWindow::Populate()
{
   //------------------------- Main section --------------------
   ShuttleGui S(this, eIsCreating);
   PopulateOrExchange(S);
   // ----------------------- End of main section --------------

   // Get and validate the currently active macro
   mActiveMacro = gPrefs->Read(wxT("/Batch/ActiveMacro"), wxT(""));
   // Go populate the macros list.
   PopulateMacros();

   // We have a bare list.  We need to add columns and content.
   PopulateList();

   // Layout and set minimum size of window
   Layout();
   Fit();
   SetSizeHints(GetSize());

   // Size and place window
   SetSize(std::min(wxSystemSettings::GetMetric(wxSYS_SCREEN_X) * 3 / 4, 800),
           std::min(wxSystemSettings::GetMetric(wxSYS_SCREEN_Y) * 4 / 5, 400));
   Center();

   // Set the column size for the macros list.
   wxSize sz = mMacros->GetClientSize();
   mMacros->SetColumnWidth(0, sz.x);

   // Size columns properly
   FitColumns();
}

/// Defines the dialog and does data exchange with it.
void MacrosWindow::PopulateOrExchange(ShuttleGui & S)
{
   S.StartHorizontalLay(wxEXPAND, 1);
   {
      S.StartStatic(_("&Select Macro"),0);
      {
         S.StartHorizontalLay(wxEXPAND,1);
         {
            S.SetStyle(wxSUNKEN_BORDER | wxLC_REPORT | wxLC_HRULES | wxLC_SINGLE_SEL |
                        wxLC_EDIT_LABELS);
            mMacros = S.Id(MacrosListID).Prop(1).AddListControlReportMode();
            // i18n-hint: This is the heading for a column in the edit macros dialog
            mMacros->InsertColumn(0, _("Macro"), wxLIST_FORMAT_LEFT);
            S.StartVerticalLay(wxALIGN_TOP, 0);
            {
               S.Id(AddButtonID).AddButton(_("&New"));
               mRemove = S.Id(RemoveButtonID).AddButton(_("Remo&ve"));
               mRename = S.Id(RenameButtonID).AddButton(_("&Rename..."));
               S.Id(ImportButtonID).AddButton(_("I&mport..."))->Enable( false);
               S.Id(ExportButtonID).AddButton(_("E&xport..."))->Enable( false);
            }
            S.EndVerticalLay();
         }
         S.EndHorizontalLay();
      }
      S.EndStatic();

      S.StartStatic(_("Edit S&teps"), true);
      {
         S.StartHorizontalLay(wxEXPAND,1);
         {
            
            S.SetStyle(wxSUNKEN_BORDER | wxLC_REPORT | wxLC_HRULES | wxLC_VRULES |
                        wxLC_SINGLE_SEL);
            mList = S.Id(CommandsListID).AddListControlReportMode();

            //An empty first column is a workaround - under Win98 the first column
            //can't be right aligned.
            mList->InsertColumn(BlankColumn, wxT(""), wxLIST_FORMAT_LEFT);
            /* i18n-hint: This is the number of the command in the list */
            mList->InsertColumn(ItemNumberColumn, _("Num"), wxLIST_FORMAT_RIGHT);
            mList->InsertColumn(ActionColumn, _("Command  "), wxLIST_FORMAT_RIGHT);
            mList->InsertColumn(ParamsColumn, _("Parameters"), wxLIST_FORMAT_LEFT);

            S.StartVerticalLay(wxALIGN_TOP, 0);
            {
               S.Id(InsertButtonID).AddButton(_("&Insert"), wxALIGN_LEFT);
               S.Id(EditButtonID).AddButton(_("&Edit..."), wxALIGN_LEFT);
               S.Id(DeleteButtonID).AddButton(_("De&lete"), wxALIGN_LEFT);
               S.Id(UpButtonID).AddButton(_("Move &Up"), wxALIGN_LEFT);
               S.Id(DownButtonID).AddButton(_("Move &Down"), wxALIGN_LEFT);
               mDefaults = S.Id(DefaultsButtonID).AddButton(_("De&faults"));
            }
            S.EndVerticalLay();
         }
         S.EndHorizontalLay();
      }
      S.EndStatic();
   }
   S.EndHorizontalLay();

   S.StartHorizontalLay(wxEXPAND, 0);
   {  
      mResize = S.Id(ShrinkID).AddButton(_("Shrin&k"));
      // Using variable text just to get the positioning options.
      S.Prop(0).AddVariableText( _("Apply Macro to:"), false, wxALL | wxALIGN_CENTRE_VERTICAL );
      S.Id(ApplyToProjectID).AddButton(_("&Project"));
      S.Id(ApplyToFilesID).AddButton(_("&Files..."));
      S.Prop(1).AddSpace( 10 );
      S.AddStandardButtons( eOkButton | eCancelButton | eHelpButton);
   }
   S.EndHorizontalLay();


   return;
}

/// This clears and updates the contents of mList, the commands for the current macro.
void MacrosWindow::PopulateList()
{
   int topItem = mList->GetTopItem();
   mList->DeleteAllItems();

   for (int i = 0; i < mMacroCommands.GetCount(); i++) {
      AddItem(mMacroCommands.GetCommand(i),
              mMacroCommands.GetParams(i));
   }
   /*i18n-hint: This is the last item in a list.*/
   AddItem(_("- END -"), wxT(""));

   // Select the name in the list...this will fire an event.
   if (mSelectedCommand >= (int)mList->GetItemCount()) {
      mSelectedCommand = 0;
   }
   mList->SetItemState(mSelectedCommand, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
   if( 0 <= topItem && topItem < (int)mList->GetItemCount())
   {
      // Workaround for scrolling being windows only.
      // Try to scroll back to where we once were...
      mList->EnsureVisible( (int)mList->GetItemCount() -1 );
      mList->EnsureVisible( topItem );
      // And then make sure whatever is selected is still visible...
      if (mSelectedCommand >= 0) {
         mList->EnsureVisible( mSelectedCommand );
      }
   }
}

/// Add one item into mList
void MacrosWindow::AddItem(const wxString &Action, const wxString &Params)
{
   auto entry = mCatalog.ByCommandId(Action);
   auto friendlyName = entry != mCatalog.end()
      ? entry->name.Translated()
      :
         // Expose an internal name to the user in default of any friendly name
         // -- AVOID THIS!
        Action;

   int i = mList->GetItemCount();

   mList->InsertItem(i, wxT(""));
   mList->SetItem(i, ItemNumberColumn, wxString::Format(wxT(" %02i"), i + 1));
   mList->SetItem(i, ActionColumn, friendlyName );
   mList->SetItem(i, ParamsColumn, Params );
}

void MacrosWindow::UpdateMenus()
{
   // OK even on mac, as dialog is modal.
   GetActiveProject()->RebuildMenuBar();
}

void MacrosWindow::UpdateDisplay( bool bExpanded )
{
   if( bExpanded == mbExpanded )
      return;

   if( !SaveChanges() )
      return;

   mbExpanded = bExpanded;
   DestroyChildren();
   SetSizer( nullptr );
   
   mChanged = false;
   mSelectedCommand = 0;
   SetMinSize( wxSize( 200,200 ));

   // Get and set position for optical stability.
   // Expanded and shrunk dialogs 'stay where they were'.
   // That's OK , and what we want, even if we exapnd off-screen.
   // We won't shrink to being off-screen, since the shrink button 
   // was clicked, so must have been on screen.
   wxPoint p = GetPosition( );
   if( mbExpanded )
      Populate();
   else
      ApplyMacroDialog::Populate();
   SetPosition( p );
   mResize->SetFocus();

   wxString Title = mbExpanded ? _("Manage Macros") : _("Macros Palette");
   SetLabel( Title );         // Provide visual label
   SetName( Title );          // Provide audible label
   SetTitle( Title );
}

bool MacrosWindow::ChangeOK()
{
   if (mChanged) {
      wxString title;
      wxString msg;
      int id;

      title.Printf(_("%s changed"), mActiveMacro);
      msg = _("Do you want to save the changes?");

      id = AudacityMessageBox(msg, title, wxYES_NO | wxCANCEL);
      if (id == wxCANCEL) {
         return false;
      }

      if (id == wxYES) {
         if (!mMacroCommands.WriteMacro(mActiveMacro)) {
            return false;
         }
      }

      mChanged = false;
   }

   return true;
}

void MacrosWindow::FitColumns()
{
   mList->SetColumnWidth(0, 0);  // First column width is zero, to hide it.

#if defined(__WXMAC__)
   // wxMac uses a hard coded width of 150 when wxLIST_AUTOSIZE_USEHEADER
   // is specified, so we calculate the width ourselves. This method may
   // work equally well on other platforms.
   for (size_t c = 1; c < mList->GetColumnCount(); c++) {
      wxListItem info;
      int width;

      mList->SetColumnWidth(c, wxLIST_AUTOSIZE);
      info.Clear();
      info.SetId(c);
      info.SetMask(wxLIST_MASK_TEXT | wxLIST_MASK_WIDTH);
      mList->GetColumn(c, info);

      mList->GetTextExtent(info.GetText(), &width, NULL);
      width += 2 * 4;    // 2 * kItemPadding - see listctrl_mac.cpp
      width += 16;       // kIconWidth - see listctrl_mac.cpp

      mList->SetColumnWidth(c, wxMax(width, mList->GetColumnWidth(c)));
   }

   // Looks strange, but it forces the horizontal scrollbar to get
   // drawn.  If not done, strange column sizing can occur if the
   // user attempts to resize the columns.
   mList->SetClientSize(mList->GetClientSize());
#else
   mList->SetColumnWidth(1, wxLIST_AUTOSIZE_USEHEADER);
   mList->SetColumnWidth(2, wxLIST_AUTOSIZE_USEHEADER);
   mList->SetColumnWidth(3, wxLIST_AUTOSIZE);
#endif

   int bestfit = mList->GetColumnWidth(3);
   int clientsize = mList->GetClientSize().GetWidth();
   int col1 = mList->GetColumnWidth(1);
   int col2 = mList->GetColumnWidth(2);
   bestfit = (bestfit > clientsize-col1-col2)? bestfit : clientsize-col1-col2;
   mList->SetColumnWidth(3, bestfit);

}

///
void MacrosWindow::OnInsert(wxCommandEvent & WXUNUSED(event))
{
   long item = mList->GetNextItem(-1,
                                  wxLIST_NEXT_ALL,
                                  wxLIST_STATE_SELECTED);
   if (item == -1) {
      item = mList->GetItemCount()-1;
   }
   InsertCommandAt( item );
}

void MacrosWindow::InsertCommandAt(int item)
{
   if (item == -1) {
      return;
   }

   MacroCommandDialog d(this, wxID_ANY);

   if (!d.ShowModal()) {
      Raise();
      return;
   }
   Raise();

   if(d.mSelectedCommand != wxT(""))
   {
      mMacroCommands.AddToMacro(d.mSelectedCommand,
                                d.mSelectedParameters,
                                item);
      mChanged = true;
      mSelectedCommand = item + 1;
      PopulateList();
   }

}

void MacrosWindow::OnEditCommandParams(wxCommandEvent & WXUNUSED(event))
{
   int item = mList->GetNextItem( -1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED );

   // LAST command in list is END.
   // If nothing selected, add at END.
   // If END selected, add at END.
   // When adding at end we use InsertCommandAt, so that a new command
   // can be chosen.
   int lastItem = mList->GetItemCount()-1;
   if( (item<0) || (item+1) == mList->GetItemCount() )
   {
      InsertCommandAt( lastItem );
      return;
   }

   // Just edit the parameters, and not the command.
   wxString command = mMacroCommands.GetCommand(item);
   wxString params  = mMacroCommands.GetParams(item);

   params = wxEmptyString;
   Raise();

   mMacroCommands.DeleteFromMacro(item);
   mMacroCommands.AddToMacro(command,
                             params,
                             item);
   mChanged = true;
   mSelectedCommand = item;
   PopulateList();
}

///
void MacrosWindow::OnDelete(wxCommandEvent & WXUNUSED(event))
{
   long item = mList->GetNextItem(-1,
                                  wxLIST_NEXT_ALL,
                                  wxLIST_STATE_SELECTED);
   if (item == -1 || item + 1 == mList->GetItemCount()) {
      return;
   }

   mMacroCommands.DeleteFromMacro(item);
   mChanged = true;

   if (item >= (mList->GetItemCount() - 2) && item >= 0) {
      item--;
   }
   mSelectedCommand = item;
   PopulateList();
}

///
void MacrosWindow::OnUp(wxCommandEvent & WXUNUSED(event))
{
   long item = mList->GetNextItem(-1,
                                  wxLIST_NEXT_ALL,
                                  wxLIST_STATE_SELECTED);
   if (item == -1 || item == 0 || item + 1 == mList->GetItemCount()) {
      return;
   }

   mMacroCommands.AddToMacro(mMacroCommands.GetCommand(item),
                             mMacroCommands.GetParams(item),
                             item - 1);
   mMacroCommands.DeleteFromMacro(item + 1);
   mChanged = true;
   mSelectedCommand = item - 1;
   PopulateList();
}

///
void MacrosWindow::OnDown(wxCommandEvent & WXUNUSED(event))
{
   long item = mList->GetNextItem(-1,
                                  wxLIST_NEXT_ALL,
                                  wxLIST_STATE_SELECTED);
   if (item == -1 || item + 2 >= mList->GetItemCount()) {
      return;
   }

   mMacroCommands.AddToMacro(mMacroCommands.GetCommand(item),
                             mMacroCommands.GetParams(item),
                             item + 2);
   mMacroCommands.DeleteFromMacro(item);
   mChanged = true;
   mSelectedCommand = item + 1;
   PopulateList();
}

/// Select the empty Command macro.
void MacrosWindow::OnDefaults(wxCommandEvent & WXUNUSED(event))
{
   mMacroCommands.RestoreMacro(mActiveMacro);

   mChanged = true;

   PopulateList();
}

bool MacrosWindow::SaveChanges(){
   gPrefs->Write(wxT("/Batch/ActiveMacro"), mActiveMacro);
   gPrefs->Flush();

   if (mChanged) {
      if (!mMacroCommands.WriteMacro(mActiveMacro)) {
         return false;
      }
   }
   mChanged = false;
   return true;
}

/// Send changed values back to Prefs, and update Audacity.
void MacrosWindow::OnOK(wxCommandEvent & WXUNUSED(event))
{
   if( !SaveChanges() )
      return;
   Hide();
   //EndModal(true);
}

///
void MacrosWindow::OnKeyDown(wxKeyEvent &event)
{
   if (event.GetKeyCode() == WXK_DELETE) {
      wxLogDebug(wxT("wxKeyEvent"));
   }

   event.Skip();
}
