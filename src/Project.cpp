/**********************************************************************

  Audacity: A Digital Audio Editor

  Project.cpp

  Dominic Mazzoni
  Vaughan Johnson

*******************************************************************//**

\file Project.cpp
\brief Implements AudacityProject, DropTarget, and FileObject.F
Includes Menus.cpp.

*//****************************************************************//**

\class AudacityProject
\brief AudacityProject provides the main window, with tools and
tracks contained within it.

  In Audacity, the main window you work in is called a project.
  AudacityProjects can contain an arbitrary number of tracks of many
  different types, but if a project contains just one or two
  tracks then it can be saved in standard formats like WAV or AIFF.
  This window is the one that contains the menu bar (except on
  the Mac).

\attention The menu functions for AudacityProject, those for creating
the menu bars and acting on clicks, are found in file Menus.cpp

*//****************************************************************//**

\class DropTarget
\brief DropTarget, derived from wxFileDropTarget gives drag and drop
functionality for audio files.

*//****************************************************************//**

\class FileObject
\brief FileObject, derived from wxFileDataObject gives extended drag
and drop functionality for audio files.

*//****************************************************************//**

\class ViewInfo
\brief ViewInfo is used mainly to hold the zooming, selection and
scroll information.  It also has some status flags.

*//*******************************************************************/

#include "Audacity.h"
#include "Project.h"

#include <stdio.h>
#include <iostream>
#include <wx/wxprec.h>
#include <wx/apptrait.h>

#include <wx/defs.h>
#include <wx/app.h>
#include <wx/dc.h>
#include <wx/dcmemory.h>
#include <wx/dnd.h>
#include <wx/docview.h>
#include <wx/event.h>
#include <wx/ffile.h>
#include <wx/filedlg.h>
#include <wx/filefn.h>
#include <wx/filename.h>
#include <wx/intl.h>
#include <wx/log.h>
#include <wx/menu.h>
#include <wx/notebook.h>
#include <wx/progdlg.h>
#include <wx/scrolbar.h>
#include <wx/sizer.h>
#include <wx/statusbr.h>
#include <wx/string.h>
#include <wx/textfile.h>
#include <wx/timer.h>
#include <wx/display.h>

#if defined(__WXMAC__)
#if !wxCHECK_VERSION(3, 0, 0)
#include <CoreServices/CoreServices.h>
#include <wx/mac/private.h>
#endif
#endif

#include "AudacityException.h"
#include "FreqWindow.h"
#include "AutoRecovery.h"
#include "AudacityApp.h"
#include "AColor.h"
#include "AudioIO.h"
#include "Dependencies.h"
#include "Diags.h"
#include "HistoryWindow.h"
#include "Internat.h"
#include "import/Import.h"
#include "Mix.h"
#include "Prefs.h"
#include "Sequence.h"
#include "Snap.h"
#include "Tags.h"
#include "TrackPanel.h"
#include "WaveTrack.h"
#include "DirManager.h"
#include "effects/Effect.h"
#include "prefs/PrefsDialog.h"
#include "widgets/LinkingHtmlWindow.h"
#include "widgets/ASlider.h"
#include "widgets/ErrorDialog.h"
#include "widgets/Ruler.h"
#include "widgets/Warning.h"
#include "xml/XMLFileReader.h"
#include "PlatformCompatibility.h"
#include "Experimental.h"
#include "export/Export.h"
#include "FileNames.h"
#include "BlockFile.h"
#include "ondemand/ODManager.h"
#include "ondemand/ODTask.h"
#include "ondemand/ODComputeSummaryTask.h"
#include "ModuleManager.h"

#include "Theme.h"
#include "AllThemeResources.h"

#include "FileDialog.h"

#include "UndoManager.h"

#include "toolbars/ToolManager.h"
#include "toolbars/ControlToolBar.h"
#include "toolbars/DeviceToolBar.h"
#include "toolbars/EditToolBar.h"
#include "toolbars/MeterToolBar.h"
#include "toolbars/MixerToolBar.h"
#include "toolbars/SelectionBar.h"
#include "toolbars/ToolsToolBar.h"

#include "tracks/ui/BackgroundCell.h"
#include "tracks/ui/EditCursorOverlay.h"
#include "tracks/ui/PlayIndicatorOverlay.h"

#include "commands/ScriptCommandRelay.h"
#include "commands/CommandTargets.h"
#include "commands/Command.h"
#include "commands/CommandType.h"
#include "commands/CommandContext.h"

#include "../images/AudacityLogoAlpha.xpm"

std::shared_ptr<TrackList> AudacityProject::msClipboard{ TrackList::Create() };
double AudacityProject::msClipT0 = 0.0;
double AudacityProject::msClipT1 = 0.0;
AudacityProject *AudacityProject::msClipProject = NULL;
ODLock &AudacityProject::AllProjectDeleteMutex()
{
   static ODLock theMutex;
   return theMutex;
};

#if defined(__WXMAC__)
// const int sbarSpaceWidth = 15;
// const int sbarControlWidth = 16;
// const int sbarExtraLen = 1;
const int sbarHjump = 30;       //STM: This is how far the thumb jumps when the l/r buttons are pressed, or auto-scrolling occurs -- in pixels
#elif defined(__WXMSW__)
const int sbarSpaceWidth = 16;
const int sbarControlWidth = 16;
const int sbarExtraLen = 0;
const int sbarHjump = 30;       //STM: This is how far the thumb jumps when the l/r buttons are pressed, or auto-scrolling occurs -- in pixels
#else // wxGTK, wxMOTIF, wxX11
const int sbarSpaceWidth = 15;
const int sbarControlWidth = 15;
const int sbarExtraLen = 0;
const int sbarHjump = 30;       //STM: This is how far the thumb jumps when the l/r buttons are pressed, or auto-scrolling occurs -- in pixels
#include "Theme.h"
#include "AllThemeResources.h"
#endif

int AudacityProject::mProjectCounter=0;// global counter.


////////////////////////////////////////////////////////////
/// Custom events
////////////////////////////////////////////////////////////
DEFINE_EVENT_TYPE(EVT_CAPTURE_KEY);

//
// This small template class resembles a try-finally block
//
// It sets var to val_entry in the constructor and
// var to val_exit in the destructor.
//
template <typename T>
class VarSetter
{
public:
   VarSetter(T* var, T val_entry, T val_exit)
   {
      mVar = var;
      mValExit = val_exit;
      *var = val_entry;
   }

   ~VarSetter()
   {
      *mVar = mValExit;
   }
private:
   T* mVar;
   T mValExit;
};

// This wrapper prevents the scrollbars from retaining focus after being
// used.  Otherwise, the only way back to the track panel is to click it
// and that causes your original location to be lost.
class ScrollBar final : public wxScrollBar
{
public:
   ScrollBar(wxWindow* parent, wxWindowID id, long style)
   :   wxScrollBar(parent, id, wxDefaultPosition, wxDefaultSize, style)
   {
   }

   void OnSetFocus(wxFocusEvent & e)
   {
      wxWindow *w = e.GetWindow();
      if (w != NULL) {
         w->SetFocus();
      }
   }

   void SetScrollbar(int position, int thumbSize,
                     int range, int pageSize,
                     bool refresh = true) override;

private:
   DECLARE_EVENT_TABLE()
};

void ScrollBar::SetScrollbar(int position, int thumbSize,
                             int range, int pageSize,
                             bool refresh)
{
   // Mitigate flashing of scrollbars by refreshing only when something really changes.

   // PRL:  This may have been made unnecessary by other fixes for flashing, see
   // commit ac05b190bee7dd0000bce56edb0e5e26185c972f

   auto changed =
      position != GetThumbPosition() ||
      thumbSize != GetThumbSize() ||
      range != GetRange() ||
      pageSize != GetPageSize();
   if (!changed)
      return;

   wxScrollBar::SetScrollbar(position, thumbSize, range, pageSize, refresh);
}

BEGIN_EVENT_TABLE(ScrollBar, wxScrollBar)
   EVT_SET_FOCUS(ScrollBar::OnSetFocus)
END_EVENT_TABLE()

/* Define Global Variables */
//The following global counts the number of documents that have been opened
//for the purpose of project placement (not to keep track of the number)
//It is only accurate modulo ten, and does not decrement when a project is
//closed.
static int gAudacityOffsetInc = 0;
static int gAudacityPosInc = 0;
//This is a pointer to the currently-active project.
static AudacityProject *gActiveProject;
//This array holds onto all of the projects currently open
AProjectArray gAudacityProjects;

/* Declare Static functions */
static void SetActiveProject(AudacityProject * project);

AUDACITY_DLL_API AudacityProject *GetActiveProject()
{
   return gActiveProject;
}

void SetActiveProject(AudacityProject * project)
{
   gActiveProject = project;
   wxTheApp->SetTopWindow(project);
}

#if wxUSE_DRAG_AND_DROP
class FileObject final : public wxFileDataObject
{
public:
   FileObject()
   {
   }

   bool IsSupportedFormat(const wxDataFormat & format, Direction WXUNUSED(dir = Get)) const
      // PRL:  This function does NOT override any inherited virtual!  What does it do?
   {
      if (format.GetType() == wxDF_FILENAME) {
         return true;
      }

#if defined(__WXMAC__)
#if !wxCHECK_VERSION(3, 0, 0)
      if (format.GetFormatId() == kDragPromisedFlavorFindFile) {
         return true;
      }
#endif
#endif

      return false;
   }
};

class DropTarget final : public wxFileDropTarget
{
public:
   DropTarget(AudacityProject *proj)
   {
      mProject = proj;

      // SetDataObject takes ownership
      SetDataObject(safenew FileObject());
   }

   ~DropTarget()
   {
   }

#if defined(__WXMAC__)
#if !wxCHECK_VERSION(3, 0, 0)
   bool GetData() override
   {
      bool foundSupported = false;
      bool firstFileAdded = false;
      OSErr result;

      UInt16 items = 0;
      CountDragItems((DragReference)m_currentDrag, &items);

      for (UInt16 index = 1; index <= items; index++) {

         DragItemRef theItem = 0;
         GetDragItemReferenceNumber((DragReference)m_currentDrag, index, &theItem);

         UInt16 flavors = 0;
         CountDragItemFlavors((DragReference)m_currentDrag, theItem , &flavors ) ;

         for (UInt16 flavor = 1 ;flavor <= flavors; flavor++) {

            FlavorType theType = 0;
            result = GetFlavorType((DragReference)m_currentDrag, theItem, flavor, &theType);
            if (theType != kDragPromisedFlavorFindFile && theType != kDragFlavorTypeHFS) {
               continue;
            }
            foundSupported = true;

            Size dataSize = 0;
            GetFlavorDataSize((DragReference)m_currentDrag, theItem, theType, &dataSize);

            ArrayOf<char> theData{ dataSize };
            GetFlavorData((DragReference)m_currentDrag, theItem, theType, (void*) theData.get(), &dataSize, 0L);

            wxString name;
            if (theType == kDragPromisedFlavorFindFile) {
               name = wxMacFSSpec2MacFilename((FSSpec *)theData.get());
            }
            else if (theType == kDragFlavorTypeHFS) {
               name = wxMacFSSpec2MacFilename(&((HFSFlavor *)theData.get())->fileSpec);
            }

            if (!firstFileAdded) {
               // reset file list
               ((wxFileDataObject*)GetDataObject())->SetData(0, "");
               firstFileAdded = true;
            }

            ((wxFileDataObject*)GetDataObject())->AddFile(name);

            // We only want to process one flavor
            break;
         }
      }
      return foundSupported;
   }
#endif

   bool OnDrop(wxCoord x, wxCoord y) override
   {
      // bool foundSupported = false;
#if !wxCHECK_VERSION(3, 0, 0)
      bool firstFileAdded = false;
      OSErr result;

      UInt16 items = 0;
      CountDragItems((DragReference)m_currentDrag, &items);

      for (UInt16 index = 1; index <= items; index++) {

         DragItemRef theItem = 0;
         GetDragItemReferenceNumber((DragReference)m_currentDrag, index, &theItem);

         UInt16 flavors = 0;
         CountDragItemFlavors((DragReference)m_currentDrag, theItem , &flavors ) ;

         for (UInt16 flavor = 1 ;flavor <= flavors; flavor++) {

            FlavorType theType = 0;
            result = GetFlavorType((DragReference)m_currentDrag, theItem, flavor, &theType);
            if (theType != kDragPromisedFlavorFindFile && theType != kDragFlavorTypeHFS) {
               continue;
            }
            return true;
         }
      }
#endif
      return CurrentDragHasSupportedFormat();
   }

#endif

   bool OnDropFiles(wxCoord WXUNUSED(x), wxCoord WXUNUSED(y), const wxArrayString& filenames) override
   {
      // Experiment shows that this function can be reached while there is no
      // catch block above in wxWidgets.  So stop all exceptions here.
      return GuardedCall< bool > ( [&] {
         //sort by OD non OD.  load Non OD first so user can start editing asap.
         wxArrayString sortednames(filenames);
         sortednames.Sort(CompareNoCaseFileName);

         ODManager::Pauser pauser;

         auto cleanup = finally( [&] {
            mProject->HandleResize(); // Adjust scrollers for NEW track sizes.
         } );

         for (const auto &name : sortednames) {
               mProject->Import(name);
         }

         mProject->ZoomAfterImport(nullptr);

         return true;
      } );
   }

private:
   AudacityProject *mProject;
};

#endif


bool ImportXMLTagHandler::HandleXMLTag(const wxChar *tag, const wxChar **attrs)
{
   if (wxStrcmp(tag, wxT("import")) || attrs==NULL || (*attrs)==NULL || wxStrcmp(*attrs++, wxT("filename")))
       return false;
   wxString strAttr = *attrs;
   if (!XMLValueChecker::IsGoodPathName(strAttr))
   {
      // Maybe strAttr is just a fileName, not the full path. Try the project data directory.
      wxFileName fileName(mProject->GetDirManager()->GetProjectDataDir(), strAttr);
      if (XMLValueChecker::IsGoodFileName(strAttr, fileName.GetPath(wxPATH_GET_VOLUME)))
         strAttr = fileName.GetFullPath();
      else
      {
         wxLogWarning(wxT("Could not import file: %s"), strAttr);
         return false;
      }
   }

   WaveTrackArray trackArray;

   // Guard this call so that C++ exceptions don't propagate through
   // the expat library
   GuardedCall(
      [&] { mProject->Import(strAttr, &trackArray); },
      [&] (AudacityException*) { trackArray.clear(); }
   );

   if (trackArray.empty())
      return false;

   // Handle other attributes, now that we have the tracks.
   attrs++;
   const wxChar** pAttr;
   bool bSuccess = true;

   for (size_t i = 0; i < trackArray.size(); i++)
   {
      // Most of the "import" tag attributes are the same as for "wavetrack" tags,
      // so apply them via WaveTrack::HandleXMLTag().
      bSuccess = trackArray[i]->HandleXMLTag(wxT("wavetrack"), attrs);

      // "offset" tag is ignored in WaveTrack::HandleXMLTag except for legacy projects,
      // so handle it here.
      double dblValue;
      pAttr = attrs;
      while (*pAttr)
      {
         const wxChar *attr = *pAttr++;
         const wxChar *value = *pAttr++;
         const wxString strValue = value;
         if (!wxStrcmp(attr, wxT("offset")) &&
               XMLValueChecker::IsGoodString(strValue) &&
               Internat::CompatibleToDouble(strValue, &dblValue))
            trackArray[i]->SetOffset(dblValue);
      }
   }
   return bSuccess;
};

AudacityProject *CreateNewAudacityProject()
{
   wxRect wndRect;
   bool bMaximized = false;
   bool bIconized = false;
   GetNextWindowPlacement(&wndRect, &bMaximized, &bIconized);

   // Create and show a NEW project
   // Use a non-default deleter in the smart pointer!
   gAudacityProjects.push_back( AProjectHolder {
      safenew AudacityProject(
         nullptr, -1,
         wxDefaultPosition,
         wxSize(wndRect.width, wndRect.height)
      ),
      Destroyer< AudacityProject > {}
   } );
   const auto p = gAudacityProjects.back().get();

   // wxGTK3 seems to need to require creating the window using default position
   // and then manually positioning it.
   p->SetPosition(wndRect.GetPosition());

   if(bMaximized) {
      p->Maximize(true);
   }
   else if (bIconized) {
      // if the user close down and iconized state we could start back up and iconized state
      // p->Iconize(TRUE);
   }

   //Initialise the Listener
   gAudioIO->SetListener(p);

   //Set the NEW project as active:
   SetActiveProject(p);

   // Okay, GetActiveProject() is ready. Now we can get its CommandManager,
   // and add the shortcut keys to the tooltips.
   p->GetToolManager()->RegenerateTooltips();

   ModuleManager::Get().Dispatch(ProjectInitialized);

   p->Show(true);

   return p;
}

void RedrawAllProjects()
{
   size_t len = gAudacityProjects.size();
   for (size_t i = 0; i < len; i++)
      gAudacityProjects[i]->RedrawProject();
}

void RefreshCursorForAllProjects()
{
   size_t len = gAudacityProjects.size();
   for (size_t i = 0; i < len; i++)
      gAudacityProjects[i]->RefreshCursor();
}

AUDACITY_DLL_API void CloseAllProjects()
{
   size_t len = gAudacityProjects.size();
   for (size_t i = 0; i < len; i++)
      gAudacityProjects[i]->Close();

   //Set the Offset and Position increments to 0
   gAudacityOffsetInc = 0;
   gAudacityPosInc = 0;
}

// BG: The default size and position of the first window
void GetDefaultWindowRect(wxRect *defRect)
{
   *defRect = wxGetClientDisplayRect();

   int width = 940;
   int height = 674;

   //These conditional values assist in improving placement and size
   //of NEW windows on different platforms.
#ifdef __WXGTK__
   height += 20;
#endif

#ifdef __WXMSW__
   height += 40;
#endif

#ifdef __WXMAC__
   height += 55;
#endif

   // Use screen size where it is smaller than the values we would like.
   // Otherwise use the values we would like, and centred.
   if (width < defRect->width)
   {
      defRect->x = (defRect->width - width)/2;
      defRect->width = width;
   }

   if (height < defRect->height)
   {
      defRect->y = (defRect->height - height)/2;
      // Bug 1119 workaround
      // Small adjustment for very small Mac screens.
      // If there is only a tiny space at the top
      // then instead of vertical centre, align to bottom.
      const int pixelsFormenu = 60;
      if( defRect->y < pixelsFormenu )
         defRect->y *=2;
      defRect->height = height;
   }
}

// true iff we have enough of the top bar to be able to reposition the window.
bool IsWindowAccessible(wxRect *requestedRect)
{
   wxDisplay display;
   wxRect targetTitleRect(requestedRect->GetLeftTop(), requestedRect->GetBottomRight());
   // Hackery to approximate a window top bar size from a window size.
   // and exclude the open/close and borders.
   targetTitleRect.x += 15;
   targetTitleRect.width -= 100;
   if (targetTitleRect.width <  165) targetTitleRect.width = 165;
   targetTitleRect.height = 15;
   int targetBottom = targetTitleRect.GetBottom();
   int targetRight = targetTitleRect.GetRight();
   // This looks like overkill to check each and every pixel in the ranges.
   // and decide that if any is visible on screen we are OK.
   for (int i =  targetTitleRect.GetLeft(); i < targetRight; i++) {
      for (int j = targetTitleRect.GetTop(); j < targetBottom; j++) {
         int monitor = display.GetFromPoint(wxPoint(i, j));
         if (monitor != wxNOT_FOUND) {
            return TRUE;
         }
      }
   }
   return FALSE;
}

// Returns the screen containing a rectangle, or -1 if none does.
int ScreenContaining( wxRect & r ){
   unsigned int n = wxDisplay::GetCount();
   for(unsigned int i = 0;i<n;i++){
      wxDisplay d(i);
      wxRect scr = d.GetClientArea();
      if( scr.Contains( r ) )
         return (int)i;
   }
   return -1;
}

// true IFF TL and BR corners are on a connected display.
// Does not need to check all four.  We just need to check that 
// the window probably is straddling screens in a sensible way.
// If the user wants to use mixed landscape and portrait, they can.
bool CornersOnScreen( wxRect & r ){
   if( wxDisplay::GetFromPoint( r.GetTopLeft()  ) == wxNOT_FOUND) return false;
   if( wxDisplay::GetFromPoint( r.GetBottomRight()  ) == wxNOT_FOUND) return false;
   return true;
}

// BG: Calculate where to place the next window (could be the first window)
// BG: Does not store X and Y in prefs. This is intentional.
//
// LL: This should NOT need to be this complicated...FIXME
void GetNextWindowPlacement(wxRect *nextRect, bool *pMaximized, bool *pIconized)
{
   int inc = 25;

   wxRect defaultRect;
   GetDefaultWindowRect(&defaultRect);

   gPrefs->Read(wxT("/Window/Maximized"), pMaximized, false);
   gPrefs->Read(wxT("/Window/Iconized"), pIconized, false);

   wxRect windowRect;
   gPrefs->Read(wxT("/Window/X"), &windowRect.x, defaultRect.x);
   gPrefs->Read(wxT("/Window/Y"), &windowRect.y, defaultRect.y);
   gPrefs->Read(wxT("/Window/Width"), &windowRect.width, defaultRect.width);
   gPrefs->Read(wxT("/Window/Height"), &windowRect.height, defaultRect.height);

   wxRect normalRect;
   gPrefs->Read(wxT("/Window/Normal_X"), &normalRect.x, defaultRect.x);
   gPrefs->Read(wxT("/Window/Normal_Y"), &normalRect.y, defaultRect.y);
   gPrefs->Read(wxT("/Window/Normal_Width"), &normalRect.width, defaultRect.width);
   gPrefs->Read(wxT("/Window/Normal_Height"), &normalRect.height, defaultRect.height);

   // Workaround 2.1.1 and earlier bug on OSX...affects only normalRect, but let's just
   // validate for all rects and plats
   if (normalRect.width == 0 || normalRect.height == 0) {
      normalRect = defaultRect;
   }
   if (windowRect.width == 0 || windowRect.height == 0) {
      windowRect = defaultRect;
   }


   wxRect screenRect( wxGetClientDisplayRect());
#if defined(__WXMAC__)

   // On OSX, the top of the window should never be less than the menu height,
   // so something is amiss if it is
   if (normalRect.y < screenRect.y) {
      normalRect = defaultRect;
   }
   if (windowRect.y < screenRect.y) {
      windowRect = defaultRect;
   }
#endif

   // IF projects empty, THEN it's the first window.
   // It lands where the config says it should, and can straddle screen.
   if (gAudacityProjects.empty()) {
      if (*pMaximized || *pIconized) {
         *nextRect = normalRect;
      }
      else {
         *nextRect = windowRect;
      }
      // Resize, for example if one monitor that was on is now off.
      if (!CornersOnScreen( wxRect(*nextRect).Deflate( 32, 32 ))) {
         *nextRect = defaultRect;
      }
      if (!IsWindowAccessible(nextRect)) {
         *nextRect = defaultRect;
      }
      // Do not trim the first project window down.
      // All corners are on screen (or almost so), and 
      // the rect may straddle screens.
      return;
   }


   // ELSE a subsequent NEW window.  It will NOT straddle screens.

   // We don't mind being 32 pixels off the screen in any direction.
   // Make sure initial sizes (pretty much) fit within the display bounds
   // We used to trim the sizes which could result in ridiculously small windows.
   // contributing to bug 1243.
   // Now instead if the window significantly doesn't fit the screen, we use the default 
   // window instead, which we know does.
   if (ScreenContaining( wxRect(normalRect).Deflate( 32, 32 ))<0) {
      normalRect = defaultRect;
   }
   if (ScreenContaining( wxRect(windowRect).Deflate( 32, 32 ) )<0) {
      windowRect = defaultRect;
   }

   bool validWindowSize = false;
   AudacityProject * validProject = NULL;
   size_t numProjects = gAudacityProjects.size();
   for (int i = numProjects; i > 0 ; i--) {
      if (!gAudacityProjects[i-1]->IsIconized()) {
            validWindowSize = true;
            validProject = gAudacityProjects[i-1].get();
            break;
      }
   }
   if (validWindowSize) {
      *nextRect = validProject->GetRect();
      *pMaximized = validProject->IsMaximized();
      *pIconized = validProject->IsIconized();
      // Do not straddle screens.
      if (ScreenContaining( wxRect(*nextRect).Deflate( 32, 32 ) )<0) {
         *nextRect = defaultRect;
      }
   }
   else {
      *nextRect = normalRect;
   }

   //Placement depends on the increments
   nextRect->x += inc;
   nextRect->y += inc;

   // defaultrect is a rectangle on the first screen.  It's the right fallback to 
   // use most of the time if things are not working out right with sizing.
   // windowRect is a saved rectangle size.
   // normalRect seems to be a substitute for windowRect when iconized or maximised.

   // Windows can say that we are off screen when actually we are not.
   // On Windows 10 I am seeing miscalculation by about 6 pixels.
   // To fix this we allow some sloppiness on the edge being counted as off screen.
   // This matters most when restoring very carefully sized windows that are maximised
   // in one dimension (height or width) but not both.
   const int edgeSlop = 10;

   // Next four lines are getting the rectangle for the screen that contains the
   // top left corner of nextRect (and defaulting to rect of screen 0 otherwise).
   wxPoint p = nextRect->GetLeftTop();
   int scr = std::max( 0, wxDisplay::GetFromPoint( p ));
   wxDisplay d( scr );
   screenRect = d.GetClientArea();

   // Now we (possibly) start trimming our rectangle down.
   // Have we hit the right side of the screen?
   wxPoint bottomRight = nextRect->GetBottomRight();
   if (bottomRight.x > (screenRect.GetRight()+edgeSlop)) {
      int newWidth = screenRect.GetWidth() - nextRect->GetLeft();
      if (newWidth < defaultRect.GetWidth()) {
         nextRect->x = windowRect.x;
         nextRect->y = windowRect.y;
         nextRect->width = windowRect.width;
      }
      else {
         nextRect->width = newWidth;
      }
   }

   // Have we hit the bottom of the screen?
   bottomRight = nextRect->GetBottomRight();
   if (bottomRight.y > (screenRect.GetBottom()+edgeSlop)) {
      nextRect->y -= inc;
      bottomRight = nextRect->GetBottomRight();
      if (bottomRight.y > (screenRect.GetBottom()+edgeSlop)) {
         nextRect->SetBottom(screenRect.GetBottom());
      }
   }

   // After all that we could have a window that does not have a visible
   // top bar.  [It is unlikely, but something might have gone wrong]
   // If so, use the safe fallback size.
   if (!IsWindowAccessible(nextRect)) {
      *nextRect = defaultRect;
   }
}

static wxString CreateUniqueName()
{
   static int count = 0;
   return wxDateTime::Now().Format(wxT("%Y-%m-%d %H-%M-%S")) +
          wxString::Format(wxT(" N-%i"), ++count);
}

enum {
   FirstID = 1000,

   // Window controls

   HSBarID,
   VSBarID,
   TrackPanelID
};


BEGIN_EVENT_TABLE(AudacityProject, wxFrame)
   EVT_MENU(wxID_ANY, AudacityProject::OnMenu)
   EVT_MOUSE_EVENTS(AudacityProject::OnMouseEvent)
   EVT_CLOSE(AudacityProject::OnCloseWindow)
   EVT_SIZE(AudacityProject::OnSize)
   EVT_SHOW(AudacityProject::OnShow)
   EVT_MOVE(AudacityProject::OnMove)
   EVT_ACTIVATE(AudacityProject::OnActivate)
   EVT_COMMAND_SCROLL_LINEUP(HSBarID, AudacityProject::OnScrollLeftButton)
   EVT_COMMAND_SCROLL_LINEDOWN(HSBarID, AudacityProject::OnScrollRightButton)
   EVT_COMMAND_SCROLL(HSBarID, AudacityProject::OnScroll)
   EVT_COMMAND_SCROLL(VSBarID, AudacityProject::OnScroll)
   EVT_TIMER(AudacityProjectTimerID, AudacityProject::OnTimer)
   // Fires for menu with ID #1...first menu defined
   EVT_UPDATE_UI(1, AudacityProject::OnUpdateUI)
   EVT_ICONIZE(AudacityProject::OnIconize)
   EVT_COMMAND(wxID_ANY, EVT_OPEN_AUDIO_FILE, AudacityProject::OnOpenAudioFile)
   EVT_COMMAND(wxID_ANY, EVT_TOOLBAR_UPDATED, AudacityProject::OnToolBarUpdate)
   //mchinen:multithreaded calls - may not be threadsafe with CommandEvent: may have to change.
   EVT_COMMAND(wxID_ANY, EVT_ODTASK_UPDATE, AudacityProject::OnODTaskUpdate)
   EVT_COMMAND(wxID_ANY, EVT_ODTASK_COMPLETE, AudacityProject::OnODTaskComplete)
END_EVENT_TABLE()

AudacityProject::AudacityProject(wxWindow * parent, wxWindowID id,
                                 const wxPoint & pos,
                                 const wxSize & size)
   : wxFrame(parent, id, _TS("Audacity"), pos, size),
     mViewInfo(0.0, 1.0, ZoomInfo::GetDefaultZoom()),
     mbLoadedFromAup( false ),
     mRate((double) gPrefs->Read(wxT("/SamplingRate/DefaultProjectSampleRate"), AudioIO::GetOptimalSupportedSampleRate())),
     mDefaultFormat((sampleFormat) gPrefs->
           Read(wxT("/SamplingRate/DefaultProjectSampleFormat"), floatSample)),
     mSnapTo(gPrefs->Read(wxT("/SnapTo"), SNAP_OFF)),
     mSelectionFormat(gPrefs->Read(wxT("/SelectionFormat"), wxT(""))),
     mFrequencySelectionFormatName(gPrefs->Read(wxT("/FrequencySelectionFormatName"), wxT(""))),
     mBandwidthSelectionFormatName(gPrefs->Read(wxT("/BandwidthSelectionFormatName"), wxT(""))),
     mUndoManager(std::make_unique<UndoManager>())
{
   mTracks = TrackList::Create();

   // Note that the first field of the status bar is a dummy, and it's width is set
   // to zero latter in the code. This field is needed for wxWidgets 2.8.12 because
   // if you move to the menu bar, the first field of the menu bar is cleared, which
   // is undesirable behaviour.
   // In addition, the help strings of menu items are by default sent to the first
   // field. Currently there are no such help strings, but it they were introduced, then
   // there would need to be an event handler to send them to the appropriate field.
   mStatusBar = CreateStatusBar(4);
   mProjectNo = mProjectCounter++; // Bug 322

   wxGetApp().SetMissingAliasedFileWarningShouldShow(true);

   // MM: DirManager is created dynamically, freed on demand via ref-counting
   // MM: We don't need to Ref() here because it start with refcount=1
   mDirManager = std::make_shared<DirManager>();

   mLastSavedTracks.reset();

   //
   // Initialize view info (shared with TrackPanel)
   //

   UpdatePrefs();

   mLockPlayRegion = false;

   // Make sure valgrind sees mIsSyncLocked is initialized, even
   // though we're about to set it from prefs.
   mIsSyncLocked = false;
   gPrefs->Read(wxT("/GUI/SyncLockTracks"), &mIsSyncLocked, false);

   // LLL:  Read this!!!
   //
   // Until the time (and cpu) required to refresh the track panel is
   // reduced, leave the following window creations in the order specified.
   // This will place the refresh of the track panel last, allowing all
   // the others to get done quickly.
   //
   // Near as I can tell, this is only a problem under Windows.
   //


   // PRL:  this panel groups the top tool dock and the ruler into one
   // tab cycle.
   // Must create it with non-default width equal to the main window width,
   // or else the device toolbar doesn't make initial widths of the choice
   // controls correct.
   mTopPanel = safenew wxPanelWrapper {
      this, wxID_ANY, wxDefaultPosition,
      wxSize{ this->GetSize().GetWidth(), -1 }
   };
   mTopPanel->SetName( "Top Panel" );// Not localised
   mTopPanel->SetAutoLayout(true);

   //
   // Create the ToolDock
   //
   mToolManager = std::make_unique<ToolManager>( this, mTopPanel );
   mToolManager->LayoutToolBars();

   //
   // Create the horizontal ruler
   //
   mRuler = safenew AdornedRulerPanel( this, mTopPanel,
                                   wxID_ANY,
                                   wxDefaultPosition,
                                   wxSize( -1, AdornedRulerPanel::GetRulerHeight() ),
                                   &mViewInfo );

   //
   // Create the TrackPanel and the scrollbars
   //
   wxWindow    * pPage;

   // Not using a notebook, so we place the track panel inside another panel,
   // this keeps the notebook code and normal code consistant and also
   // paves the way for adding additional windows inside the track panel.
   mMainPanel = safenew wxPanelWrapper(this, -1,
      wxDefaultPosition,
      wxDefaultSize,
      wxNO_BORDER);
   mMainPanel->SetSizer( safenew wxBoxSizer(wxVERTICAL) );
   mMainPanel->SetName("Main Panel");// Not localised.
   pPage = mMainPanel;
   // Set the colour here to the track panel background to avoid
   // flicker when Audacity starts up.
   // However, that leads to areas next to the horizontal scroller
   // being painted in background colour and not scroller background
   // colour, so suppress this for now.
   //pPage->SetBackgroundColour( theTheme.Colour( clrDark ));
   {
      auto ubs = std::make_unique<wxBoxSizer>(wxVERTICAL);
      ubs->Add(mToolManager->GetTopDock(), 0, wxEXPAND | wxALIGN_TOP);
      ubs->Add(mRuler, 0, wxEXPAND);
      mTopPanel->SetSizer(ubs.release());
   }

   wxBoxSizer *bs;
   {
      auto ubs = std::make_unique<wxBoxSizer>(wxVERTICAL);
      bs = ubs.get();
      bs->Add(mTopPanel, 0, wxEXPAND | wxALIGN_TOP);
      bs->Add(pPage, 1, wxEXPAND);
      bs->Add(mToolManager->GetBotDock(), 0, wxEXPAND);
      SetAutoLayout(true);
      SetSizer(ubs.release());
   }
   bs->Layout();

   // The right hand side translates to NEW TrackPanel(...) in normal
   // Audacity without additional DLLs.
   mTrackPanel = TrackPanel::FactoryFunction(pPage,
                                             TrackPanelID,
                                             wxDefaultPosition,
                                             wxDefaultSize,
                                             mTracks,
                                             &mViewInfo,
                                             this,
                                             mRuler);
   mTrackPanel->UpdatePrefs();

   mIndicatorOverlay = std::make_unique<PlayIndicatorOverlay>(this);

   mCursorOverlay = std::make_unique<EditCursorOverlay>(this);

   mBackgroundCell = std::make_shared<BackgroundCell>(this);

   // More order dependencies here...
   // This must follow construction of *mIndicatorOverlay, because it must
   // attach its timer event handler later (so that its handler is invoked
   // earlier)
   mPlaybackScroller = std::make_unique<PlaybackScroller>(this);

   // This must follow construction of *mPlaybackScroller,
   // because it must
   // attach its timer event handler later (so that its handler is invoked
   // earlier)
   this->Bind(EVT_TRACK_PANEL_TIMER,
      &ViewInfo::OnTimer,
      &mViewInfo);

   // Add the overlays, in the sequence in which they will be painted
   mTrackPanel->AddOverlay(mIndicatorOverlay.get());
   mTrackPanel->AddOverlay(mCursorOverlay.get());

   CreateMenusAndCommands();

   mTrackPanel->SetBackgroundCell(mBackgroundCell);

   // LLL: When Audacity starts or becomes active after returning from
   //      another application, the first window that can accept focus
   //      will be given the focus even if we try to SetFocus().  By
   //      creating the scrollbars after the TrackPanel, we resolve
   //      several focus problems.
   mHsbar = safenew ScrollBar(pPage, HSBarID, wxSB_HORIZONTAL);
   mVsbar = safenew ScrollBar(pPage, VSBarID, wxSB_VERTICAL);
   mHsbar->SetName(_("Horizontal Scrollbar"));
   mVsbar->SetName(_("Vertical Scrollbar"));

   // LLL: When Audacity starts or becomes active after returning from
   //      another application, the first window that can accept focus
   //      will be given the focus even if we try to SetFocus().  By
   //      making the TrackPanel that first window, we resolve several
   //      keyboard focus problems.
   pPage->MoveBeforeInTabOrder(mTopPanel);

   bs = (wxBoxSizer *)pPage->GetSizer();

   {
      // Top horizontal grouping
      auto hs = std::make_unique<wxBoxSizer>(wxHORIZONTAL);

      // Track panel
      hs->Add(mTrackPanel, 1, wxEXPAND | wxALIGN_LEFT | wxALIGN_TOP);

      {
         // Vertical grouping
         auto vs = std::make_unique<wxBoxSizer>(wxVERTICAL);

         // Vertical scroll bar
         vs->Add(mVsbar, 1, wxEXPAND | wxALIGN_TOP);
         hs->Add(vs.release(), 0, wxEXPAND | wxALIGN_TOP);
      }

      bs->Add(hs.release(), 1, wxEXPAND | wxALIGN_LEFT | wxALIGN_TOP);
   }

   {
      // Bottom horizontal grouping
      auto hs = std::make_unique<wxBoxSizer>(wxHORIZONTAL);

      // Bottom scrollbar
      hs->Add(mTrackPanel->GetLeftOffset() - 1, 0);
      hs->Add(mHsbar, 1, wxALIGN_BOTTOM);
      hs->Add(mVsbar->GetSize().GetWidth(), 0);
      bs->Add(hs.release(), 0, wxEXPAND | wxALIGN_LEFT);
   }

   // Lay it out
   pPage->SetAutoLayout(true);
   pPage->Layout();

   mMainPanel->Layout();

   wxASSERT( mTrackPanel->GetProject()==this);

   // MM: Give track panel the focus to ensure keyboard commands work
   mTrackPanel->SetFocus();

   // Create tags object
   mTags = std::make_shared<Tags>();

   InitialState();
   FixScrollbars();
   mRuler->SetLeftOffset(mTrackPanel->GetLeftOffset());  // bevel on AdornedRuler

   //
   // Set the Icon
   //

   // loads either the XPM or the windows resource, depending on the platform
#if !defined(__WXMAC__) && !defined(__WXX11__)
   {
#if defined(__WXMSW__)
      wxIcon ic{ wxICON(AudacityLogo) };
#elif defined(__WXGTK__)
      wxIcon ic{wxICON(AudacityLogoAlpha)};
#else
      wxIcon ic{};
      ic.CopyFromBitmap(theTheme.Bitmap(bmpAudacityLogo48x48));
#endif
      SetIcon(ic);
   }
#endif
   mIconized = false;

   mTrackFactory.reset(safenew TrackFactory{ mDirManager, &mViewInfo });

   int widths[] = {0, GetControlToolBar()->WidthForStatusBar(mStatusBar), -1, 150};
   mStatusBar->SetStatusWidths(4, widths);
   wxString msg = wxString::Format(_("Welcome to Audacity version %s"),
                                   AUDACITY_VERSION_STRING);
   mStatusBar->SetStatusText(msg, mainStatusBarField);
   GetControlToolBar()->UpdateStatusBar(this);
   mLastStatusUpdateTime = ::wxGetUTCTime();

   mTimer = std::make_unique<wxTimer>(this, AudacityProjectTimerID);
   mTimer->Start(200);

#if wxUSE_DRAG_AND_DROP
   // We can import now, so become a drag target
//   SetDropTarget(safenew AudacityDropTarget(this));
//   mTrackPanel->SetDropTarget(safenew AudacityDropTarget(this));

   // SetDropTarget takes ownership
   mTrackPanel->SetDropTarget(safenew DropTarget(this));
#endif

   wxTheApp->Bind(EVT_AUDIOIO_CAPTURE,
                     &AudacityProject::OnCapture,
                     this);

   //Initialize the last selection adjustment time.
   mLastSelectionAdjustment = ::wxGetLocalTimeMillis();
}

AudacityProject::~AudacityProject()
{
   // Tool manager gives us capture sometimes
   if(HasCapture())
      ReleaseMouse();

   if (wxGetApp().GetRecentFiles())
   {
      wxGetApp().GetRecentFiles()->RemoveMenu(mRecentFilesMenu);
   }

   if(mTrackPanel) {
      mTrackPanel->RemoveOverlay(mCursorOverlay.get());
      mTrackPanel->RemoveOverlay(mIndicatorOverlay.get());
   }
}

void AudacityProject::ApplyUpdatedTheme()
{
   SetBackgroundColour(theTheme.Colour( clrMedium ));
   ClearBackground();// For wxGTK.
   mTrackPanel->ApplyUpdatedTheme();
}


AudioIOStartStreamOptions AudacityProject::GetDefaultPlayOptions()
{
   AudioIOStartStreamOptions options { GetRate() };
   options.listener = this;
   return options;
}

void AudacityProject::UpdatePrefsVariables()
{
   gPrefs->Read(wxT("/AudioFiles/ShowId3Dialog"), &mShowId3Dialog, true);
   gPrefs->Read(wxT("/AudioFiles/NormalizeOnLoad"),&mNormalizeOnLoad, false);
   gPrefs->Read(wxT("/GUI/AutoScroll"), &mViewInfo.bUpdateTrackIndicator, true);
   gPrefs->Read(wxT("/GUI/EmptyCanBeDirty"), &mEmptyCanBeDirty, true );
   gPrefs->Read(wxT("/GUI/Help"), &mHelpPref, wxT("Local") );
   bool bSelectAllIfNone;
   gPrefs->Read(wxT("/GUI/SelectAllOnNone"), &bSelectAllIfNone, false);
   // 0 is grey out, 1 is Autoselect, 2 is Give warnings.
   // Audacity autoselects or warns.
   mWhatIfNoSelection = bSelectAllIfNone ? 1 : 2;
   mStopIfWasPaused = true;  // not configurable for now, but could be later.
   gPrefs->Read(wxT("/GUI/ShowSplashScreen"), &mShowSplashScreen, true);
   gPrefs->Read(wxT("/GUI/Solo"), &mSoloPref, wxT("Simple"));
   // Update the old default to the NEW default.
   if (mSoloPref == wxT("Standard"))
      mSoloPref = wxT("Simple");
   gPrefs->Read(wxT("/GUI/TracksFitVerticallyZoomed"), &mTracksFitVerticallyZoomed, false);
   //   gPrefs->Read(wxT("/GUI/UpdateSpectrogram"), &mViewInfo.bUpdateSpectrogram, true);

   gPrefs->Read(wxT("/SamplingRate/DefaultProjectSampleRate"), &mRate, AudioIO::GetOptimalSupportedSampleRate());
   mDefaultFormat = (sampleFormat) gPrefs->Read(wxT("/SamplingRate/DefaultProjectSampleFormat"), floatSample);

   gPrefs->Read(wxT("/AudioIO/SeekShortPeriod"), &mSeekShort, 1.0);
   gPrefs->Read(wxT("/AudioIO/SeekLongPeriod"), &mSeekLong, 15.0);
}

void AudacityProject::UpdatePrefs()
{
   UpdatePrefsVariables();

   SetProjectTitle();

   gPrefs->Read(wxT("/GUI/CircularTrackNavigation"), &mCircularTrackNavigation,
                false);

   if (mTrackPanel) {
      mTrackPanel->UpdatePrefs();
   }

   if (mToolManager) {
      mToolManager->UpdatePrefs();
   }

   if (mRuler) {
      mRuler->UpdatePrefs();
   }
}

void AudacityProject::SetMissingAliasFileDialog(wxDialog *dialog)
{
   mAliasMissingWarningDialog = dialog;
}

wxDialog *AudacityProject::GetMissingAliasFileDialog()
{
   return mAliasMissingWarningDialog;
}

void AudacityProject::RedrawProject(const bool bForceWaveTracks /*= false*/)
{
   FixScrollbars();
   if (bForceWaveTracks && GetTracks())
   {
      TrackListIterator iter(GetTracks());
      Track* pTrack = iter.First();
      while (pTrack)
      {
         if (pTrack->GetKind() == Track::Wave)
         {
            WaveTrack* pWaveTrack = static_cast<WaveTrack*>(pTrack);
            for (const auto &clip: pWaveTrack->GetClips())
               clip->MarkChanged();
         }
         pTrack = iter.Next();
      }
   }
   mTrackPanel->Refresh(false);
}

void AudacityProject::RefreshCursor()
{
   mTrackPanel->HandleCursorForPresentMouseState();
}

void AudacityProject::SetSel0(double newSel0)
{
   //Bound checking should go on here

   mViewInfo.selectedRegion.setT0(newSel0);
}

void AudacityProject::SetSel1(double newSel1)
{
   //Bound checking should go on here

   mViewInfo.selectedRegion.setT1(newSel1);
}

void AudacityProject::OnCapture(wxCommandEvent& evt)
{
   evt.Skip();

   if (evt.GetInt() != 0)
      mIsCapturing = true;
   else
      mIsCapturing = false;
}


const std::shared_ptr<DirManager> &AudacityProject::GetDirManager()
{
   return mDirManager;
}

TrackFactory *AudacityProject::GetTrackFactory()
{
   return mTrackFactory.get();
}

AdornedRulerPanel *AudacityProject::GetRulerPanel()
{
   return mRuler;
}

int AudacityProject::GetAudioIOToken() const
{
   return mAudioIOToken;
}

void AudacityProject::SetAudioIOToken(int token)
{
   mAudioIOToken = token;
}

bool AudacityProject::IsAudioActive() const
{
   return GetAudioIOToken() > 0 &&
      gAudioIO->IsStreamActive(GetAudioIOToken());
}

const Tags *AudacityProject::GetTags()
{
   return mTags.get();
}

wxString AudacityProject::GetName()
{
   wxString name = wxFileNameFromPath(mFileName);

   // Chop off the extension
   size_t len = name.Len();
   if (len > 4 && name.Mid(len - 4) == wxT(".aup"))
      name = name.Mid(0, len - 4);

   return name;
}

// Pass a number in to show project number, or -1 not to.
void AudacityProject::SetProjectTitle( int number)
{
   wxString name = GetName();

   // If we are showing project numbers, then we also explicitly show "<untitled>" if there
   // is none.
   if( number >= 0 ){
      /* i18n-hint: The %02i is the project number, the %s is the project name.*/
      name = wxString::Format( _TS("[Project %02i] Audacity \"%s\""), number+1 ,
         name.IsEmpty() ? "<untitled>" : (const char *)name );
   }
   // If we are not showing numbers, then <untitled> shows as 'Audacity'.
   else if( name.IsEmpty() )
   {
      mbLoadedFromAup = false;
      name = _TS("Audacity");
   }

   if (mIsRecovered)
   {
      name += wxT(" ");
      /* i18n-hint: E.g this is recovered audio that had been lost.*/
      name += _("(Recovered)");
   }

   SetTitle( name );
   SetName(name);       // to make the nvda screen reader read the correct title
}

bool AudacityProject::GetIsEmpty()
{
   return mTracks->empty();
}

bool AudacityProject::SnapSelection()
{
   if (mSnapTo != SNAP_OFF) {
      SelectedRegion &selectedRegion = mViewInfo.selectedRegion;
      NumericConverter nc(NumericConverter::TIME, GetSelectionFormat(), 0, GetRate());
      const bool nearest = (mSnapTo == SNAP_NEAREST);

      const double oldt0 = selectedRegion.t0();
      const double oldt1 = selectedRegion.t1();

      nc.ValueToControls(oldt0, nearest);
      nc.ControlsToValue();
      const double t0 = nc.GetValue();

      nc.ValueToControls(oldt1, nearest);
      nc.ControlsToValue();
      const double t1 = nc.GetValue();

      if (t0 != oldt0 || t1 != oldt1) {
         selectedRegion.setTimes(t0, t1);
         TP_DisplaySelection();
         return true;
      }
   }

   return false;
}

const wxString & AudacityProject::GetFrequencySelectionFormatName() const
{
   return mFrequencySelectionFormatName;
}

const wxString & AudacityProject::GetBandwidthSelectionFormatName() const
{
   return mBandwidthSelectionFormatName;
}

void AudacityProject::SetSelectionFormat(const wxString & format)
{
   if (GetSelectionBar()) {
      GetSelectionBar()->SetSelectionFormat(format);
   }
}

const wxString & AudacityProject::GetSelectionFormat() const
{
   return mSelectionFormat;
}

void AudacityProject::FinishAutoScroll()
{
   // Set a flag so we don't have to generate two update events
   mAutoScrolling = true;

   // Call our Scroll method which updates our ViewInfo variables
   // to reflect the positions of the scrollbars
   DoScroll();

   mAutoScrolling = false;
}


///
/// This method handles general left-scrolling, either for drag-scrolling
/// or when the scrollbar is clicked to the left of the thumb
///
void AudacityProject::OnScrollLeft()
{
   wxInt64 pos = mHsbar->GetThumbPosition();
   // move at least one scroll increment
   pos -= wxMax((wxInt64)(sbarHjump * mViewInfo.sbarScale), 1);
   pos = wxMax(pos, 0);
   mViewInfo.sbarH -= sbarHjump;
   mViewInfo.sbarH = std::max(mViewInfo.sbarH,
      -(wxInt64) PixelWidthBeforeTime(0.0));


   if (pos != mHsbar->GetThumbPosition()) {
      mHsbar->SetThumbPosition((int)pos);
      FinishAutoScroll();
   }
}
///
/// This method handles general right-scrolling, either for drag-scrolling
/// or when the scrollbar is clicked to the right of the thumb
///

void AudacityProject::OnScrollRight()
{
   wxInt64 pos = mHsbar->GetThumbPosition();
   // move at least one scroll increment
   // use wxInt64 for calculation to prevent temporary overflow
   pos += wxMax((wxInt64)(sbarHjump * mViewInfo.sbarScale), 1);
   wxInt64 max = mHsbar->GetRange() - mHsbar->GetThumbSize();
   pos = wxMin(pos, max);
   mViewInfo.sbarH += sbarHjump;
   mViewInfo.sbarH = std::min(mViewInfo.sbarH,
      mViewInfo.sbarTotal
         - (wxInt64) PixelWidthBeforeTime(0.0) - mViewInfo.sbarScreen);

   if (pos != mHsbar->GetThumbPosition()) {
      mHsbar->SetThumbPosition((int)pos);
      FinishAutoScroll();
   }
}

///
///  This handles the event when the left direction button on the scrollbar is depresssed
///
void AudacityProject::OnScrollLeftButton(wxScrollEvent & /*event*/)
{
   wxInt64 pos = mHsbar->GetThumbPosition();
   // move at least one scroll increment
   pos -= wxMax((wxInt64)(sbarHjump * mViewInfo.sbarScale), 1);
   pos = wxMax(pos, 0);
   mViewInfo.sbarH -= sbarHjump;
   mViewInfo.sbarH = std::max(mViewInfo.sbarH,
      - (wxInt64) PixelWidthBeforeTime(0.0));

   if (pos != mHsbar->GetThumbPosition()) {
      mHsbar->SetThumbPosition((int)pos);
      DoScroll();
   }
}

///
///  This handles  the event when the right direction button on the scrollbar is depresssed
///
void AudacityProject::OnScrollRightButton(wxScrollEvent & /*event*/)
{
   wxInt64 pos = mHsbar->GetThumbPosition();
   // move at least one scroll increment
   // use wxInt64 for calculation to prevent temporary overflow
   pos += wxMax((wxInt64)(sbarHjump * mViewInfo.sbarScale), 1);
   wxInt64 max = mHsbar->GetRange() - mHsbar->GetThumbSize();
   pos = wxMin(pos, max);
   mViewInfo.sbarH += sbarHjump;
   mViewInfo.sbarH = std::min(mViewInfo.sbarH,
      mViewInfo.sbarTotal
         - (wxInt64) PixelWidthBeforeTime(0.0) - mViewInfo.sbarScreen);

   if (pos != mHsbar->GetThumbPosition()) {
      mHsbar->SetThumbPosition((int)pos);
      DoScroll();
   }
}


bool AudacityProject::MayScrollBeyondZero() const
{
   if (mViewInfo.bScrollBeyondZero)
      return true;

   if (IsAudioActive()) {
      if (mPlaybackScroller) {
         auto mode = mPlaybackScroller->GetMode();
         if (mode == PlaybackScroller::Mode::Centered ||
             mode == PlaybackScroller::Mode::Right)
            return true;
      }
   }

   return false;
}

double AudacityProject::ScrollingLowerBoundTime() const
{
   if (!MayScrollBeyondZero())
      return 0;
   const double screen = mTrackPanel->GetScreenEndTime() - mViewInfo.h;
   return std::min(mTracks->GetStartTime(), -screen / 2.0);
}

// PRL: Bug1197: we seem to need to compute all in double, to avoid differing results on Mac
// That's why ViewInfo::TimeRangeToPixelWidth was defined, with some regret.
double AudacityProject::PixelWidthBeforeTime(double scrollto) const
{
   const double lowerBound = ScrollingLowerBoundTime();
   return
      // Ignoring fisheye is correct here
      mViewInfo.TimeRangeToPixelWidth(scrollto - lowerBound);
}

void AudacityProject::SetHorizontalThumb(double scrollto)
{
   const auto unscaled = PixelWidthBeforeTime(scrollto);
   const int max = mHsbar->GetRange() - mHsbar->GetThumbSize();
   const int pos =
      std::min(max,
         std::max(0,
            (int)(floor(0.5 + unscaled * mViewInfo.sbarScale))));
   mHsbar->SetThumbPosition(pos);
   mViewInfo.sbarH = floor(0.5 + unscaled - PixelWidthBeforeTime(0.0));
   mViewInfo.sbarH = std::max(mViewInfo.sbarH,
      - (wxInt64) PixelWidthBeforeTime(0.0));
   mViewInfo.sbarH = std::min(mViewInfo.sbarH,
      mViewInfo.sbarTotal
         - (wxInt64) PixelWidthBeforeTime(0.0) - mViewInfo.sbarScreen);
}

//
// This method, like the other methods prefaced with TP, handles TrackPanel
// 'callback'.
//
void AudacityProject::TP_ScrollWindow(double scrollto)
{
   SetHorizontalThumb(scrollto);

   // Call our Scroll method which updates our ViewInfo variables
   // to reflect the positions of the scrollbars
   DoScroll();
}

//
// Scroll vertically. This is called for example by the mouse wheel
// handler in Track Panel. A positive argument makes the window
// scroll down, while a negative argument scrolls up.
//
bool AudacityProject::TP_ScrollUpDown(int delta)
{
   int oldPos = mVsbar->GetThumbPosition();
   int pos = oldPos + delta;
   int max = mVsbar->GetRange() - mVsbar->GetThumbSize();

   // Can be negative in case of only one track
   if (max < 0)
      max = 0;

   if (pos > max)
      pos = max;
   else if (pos < 0)
      pos = 0;

   if (pos != oldPos)
   {
      mVsbar->SetThumbPosition(pos);

      DoScroll();
      return true;
   }
   else
      return false;
}

void AudacityProject::FixScrollbars()
{
   if (!GetTracks())
      return;

   bool refresh = false;
   bool rescroll = false;

   int totalHeight = (mTracks->GetHeight() + 32);

   int panelWidth, panelHeight;
   mTrackPanel->GetTracksUsableArea(&panelWidth, &panelHeight);

   // (From Debian...at least I think this is the change cooresponding
   // to this comment)
   //
   // (2.) GTK critical warning "IA__gtk_range_set_range: assertion
   // 'min < max' failed" because of negative numbers as result of window
   // size checking. Added a sanity check that straightens up the numbers
   // in edge cases.
   if (panelWidth < 0) {
      panelWidth = 0;
   }
   if (panelHeight < 0) {
      panelHeight = 0;
   }

   auto LastTime = -std::numeric_limits<double>::max();
   auto &tracks = *GetTracks();
   for (auto track : tracks) {
      // Iterate over pending changed tracks if present.
      {
         auto other =
         tracks.FindPendingChangedTrack(track->GetId());
         if (other)
            track = other.get();
      }
      LastTime = std::max( LastTime, track->GetEndTime() );
   }
   LastTime =
      std::max(LastTime, mViewInfo.selectedRegion.t1());

   const double screen = GetScreenEndTime() - mViewInfo.h;
   const double halfScreen = screen / 2.0;

   // If we can scroll beyond zero,
   // Add 1/2 of a screen of blank space to the end
   // and another 1/2 screen before the beginning
   // so that any point within the union of the selection and the track duration
   // may be scrolled to the midline.
   // May add even more to the end, so that you can always scroll the starting time to zero.
   const double lowerBound = ScrollingLowerBoundTime();
   const double additional = MayScrollBeyondZero()
      ? -lowerBound + std::max(halfScreen, screen - LastTime)
      : screen / 4.0;

   mViewInfo.total = LastTime + additional;

   // Don't remove time from total that's still on the screen
   mViewInfo.total = std::max(mViewInfo.total, mViewInfo.h + screen);

   if (mViewInfo.h < lowerBound) {
      mViewInfo.h = lowerBound;
      rescroll = true;
   }

   mViewInfo.sbarTotal = (wxInt64) (mViewInfo.GetTotalWidth());
   mViewInfo.sbarScreen = (wxInt64)(panelWidth);
   mViewInfo.sbarH = (wxInt64) (mViewInfo.GetBeforeScreenWidth());

   // PRL:  Can someone else find a more elegant solution to bug 812, than
   // introducing this boolean member variable?
   // Setting mVSbar earlier, int HandlXMLTag, didn't succeed in restoring
   // the vertical scrollbar to its saved position.  So defer that till now.
   // mbInitializingScrollbar should be true only at the start of the life
   // of an AudacityProject reopened from disk.
   if (!mbInitializingScrollbar) {
      mViewInfo.vpos = mVsbar->GetThumbPosition() * mViewInfo.scrollStep;
   }
   mbInitializingScrollbar = false;

   if (mViewInfo.vpos >= totalHeight)
      mViewInfo.vpos = totalHeight - 1;
   if (mViewInfo.vpos < 0)
      mViewInfo.vpos = 0;

   bool oldhstate;
   bool oldvstate;
   bool newhstate = (GetScreenEndTime() - mViewInfo.h) < mViewInfo.total;
   bool newvstate = panelHeight < totalHeight;

#ifdef __WXGTK__
   oldhstate = mHsbar->IsShown();
   oldvstate = mVsbar->IsShown();
   mHsbar->Show(newhstate);
   mVsbar->Show(panelHeight < totalHeight);
#else
   oldhstate = mHsbar->IsEnabled();
   oldvstate = mVsbar->IsEnabled();
   mHsbar->Enable(newhstate);
   mVsbar->Enable(panelHeight < totalHeight);
#endif

   if (panelHeight >= totalHeight && mViewInfo.vpos != 0) {
      mViewInfo.vpos = 0;

      refresh = true;
      rescroll = false;
   }
   if (!newhstate && mViewInfo.sbarH != 0) {
      mViewInfo.sbarH = 0;

      refresh = true;
      rescroll = false;
   }

   // wxScrollbar only supports int values but we need a greater range, so
   // we scale the scrollbar coordinates on demand. We only do this if we
   // would exceed the int range, so we can always use the maximum resolution
   // available.

   // Don't use the full 2^31 max int range but a bit less, so rounding
   // errors in calculations do not overflow max int
   wxInt64 maxScrollbarRange = (wxInt64)(2147483647 * 0.999);
   if (mViewInfo.sbarTotal > maxScrollbarRange)
      mViewInfo.sbarScale = ((double)maxScrollbarRange) / mViewInfo.sbarTotal;
   else
      mViewInfo.sbarScale = 1.0; // use maximum resolution

   {
      int scaledSbarH = (int)(mViewInfo.sbarH * mViewInfo.sbarScale);
      int scaledSbarScreen = (int)(mViewInfo.sbarScreen * mViewInfo.sbarScale);
      int scaledSbarTotal = (int)(mViewInfo.sbarTotal * mViewInfo.sbarScale);
      const int offset =
         (int)(floor(0.5 + mViewInfo.sbarScale * PixelWidthBeforeTime(0.0)));

      mHsbar->SetScrollbar(scaledSbarH + offset, scaledSbarScreen, scaledSbarTotal,
         scaledSbarScreen, TRUE);
   }

   // Vertical scrollbar
   mVsbar->SetScrollbar(mViewInfo.vpos / mViewInfo.scrollStep,
                        panelHeight / mViewInfo.scrollStep,
                        totalHeight / mViewInfo.scrollStep,
                        panelHeight / mViewInfo.scrollStep, TRUE);

   if (refresh || (rescroll &&
       (GetScreenEndTime() - mViewInfo.h) < mViewInfo.total)) {
      mTrackPanel->Refresh(false);
   }

   UpdateMenus();

   if (oldhstate != newhstate || oldvstate != newvstate) {
      UpdateLayout();
   }

   CallAfter(
      [this]{ if (GetTrackPanel())
         GetTrackPanel()->HandleCursorForPresentMouseState(); } );
}

std::shared_ptr<Track> AudacityProject::GetFirstVisible()
{
   std::shared_ptr<Track> pTrack;
   if (GetTracks()) {
      TrackListIterator iter(GetTracks());
      for (Track *t = iter.First(); t; t = iter.Next()) {
         int y = t->GetY();
         int h = t->GetHeight();
         if (y + h - 1 >= mViewInfo.vpos) {
            // At least the bottom row of pixels is not scrolled away above
            pTrack = Track::Pointer(t);
            break;
         }
      }
   }

   return pTrack;
}

void AudacityProject::UpdateLayout()
{
   if (!mTrackPanel)
      return;

   // 1. Layout panel, to get widths of the docks.
   Layout();
   // 2. Layout toolbars to pack the toolbars correctly in docks which 
   // are now the correct width.
   mToolManager->LayoutToolBars();
   // 3. Layout panel, to resize docks, in particular reducing the height 
   // of any empty docks, or increasing the height of docks that need it.
   Layout();

   // Retrieve size of this projects window
   wxSize mainsz = GetSize();

   // Retrieve position of the track panel to use as the size of the top
   // third of the window
   wxPoint tppos = ClientToScreen(mTrackPanel->GetParent()->GetPosition());

   // Retrieve position of bottom dock to use as the size of the bottom
   // third of the window
   wxPoint sbpos = ClientToScreen(mToolManager->GetBotDock()->GetPosition());

   // The "+ 50" is the minimum height of the TrackPanel
   SetSizeHints(250, (mainsz.y - sbpos.y) + tppos.y + 50, 20000, 20000);
}

void AudacityProject::HandleResize()
{
   if (!mTrackPanel)
      return;

   FixScrollbars();

   UpdateLayout();
}

// How many projects that do not have a name yet?
int AudacityProject::CountUnnamed()
{
   int j = 0;
   for ( size_t i = 0; i < gAudacityProjects.size(); i++) {
      if ( gAudacityProjects[i] )
         if ( gAudacityProjects[i]->GetName().IsEmpty() )
            j++;
   }
   return j;
}

void AudacityProject::RefreshAllTitles(bool bShowProjectNumbers )
{
   for ( size_t i = 0; i < gAudacityProjects.size(); i++) {
      if ( gAudacityProjects[i] ) {
         if ( !gAudacityProjects[i]->mIconized ) {
            AudacityProject * p;
            p = gAudacityProjects[i].get();
            p->SetProjectTitle( bShowProjectNumbers ? p->GetProjectNumber() : -1 );
         }
      }
   }
}

void AudacityProject::OnIconize(wxIconizeEvent &event)
{
   int VisibleProjectCount = 0;

   //JKC: On Iconizing we get called twice.  Don't know
   // why but it does no harm.
   // Should we be returning true/false rather than
   // void return?  I don't know.
   mIconized = event.IsIconized();

   unsigned int i;

   for(i=0;i<gAudacityProjects.size();i++){
      if(gAudacityProjects[i]){
         if( !gAudacityProjects[i]->mIconized )
            VisibleProjectCount++;
      }
   }

   event.Skip();
}

void AudacityProject::OnMove(wxMoveEvent & event)
{
   if (!this->IsMaximized() && !this->IsIconized())
      SetNormalizedWindowState(this->GetRect());
   event.Skip();
}

void AudacityProject::OnSize(wxSizeEvent & event)
{
   // (From Debian)
   //
   // (3.) GTK critical warning "IA__gdk_window_get_origin: assertion
   // 'GDK_IS_WINDOW (window)' failed": Received events of type wxSizeEvent
   // on the main project window cause calls to "ClientToScreen" - which is
   // not available until the window is first shown. So the class has to
   // keep track of wxShowEvent events and inhibit those actions until the
   // window is first shown.
   if (mShownOnce) {
      HandleResize();
      if (!this->IsMaximized() && !this->IsIconized())
         SetNormalizedWindowState(this->GetRect());
   }
   event.Skip();
}

void AudacityProject::OnShow(wxShowEvent & event)
{
   // Remember that the window has been shown at least once
   mShownOnce = true;

   // (From Debian...see also TrackPanel::OnTimer and AudacityTimer::Notify)
   //
   // Description: Workaround for wxWidgets bug: Reentry in clipboard
   //  The wxWidgets bug http://trac.wxwidgets.org/ticket/16636 prevents
   //  us from doing clipboard operations in wxShowEvent and wxTimerEvent
   //  processing because those event could possibly be processed during
   //  the (not sufficiently protected) Yield() of a first clipboard
   //  operation, causing reentry. Audacity had a workaround in place
   //  for this problem (the class "CaptureEvents"), which however isn't
   //  applicable with wxWidgets 3.0 because it's based on changing the
   //  gdk event handler, a change that would be overridden by wxWidgets's
   //  own gdk event handler change.
   //  Instead, as a NEW workaround, specifically protect those processings
   //  of wxShowEvent and wxTimerEvent that try to do clipboard operations
   //  from being executed within Yield(). This is done by delaying their
   //  execution by posting pure wxWidgets events - which are never executed
   //  during Yield().
   // Author: Martin Stegh  fer <martin@steghoefer.eu>
   //  Bug-Debian: https://bugs.debian.org/765341

   // the actual creation/showing of the window).
   // Post the event instead of calling OnSize(..) directly. This ensures that
   // this is a pure wxWidgets event (no GDK event behind it) and that it
   // therefore isn't processed within the YieldFor(..) of the clipboard
   // operations (workaround for Debian bug #765341).
   // QueueEvent() will take ownership of the event
   GetEventHandler()->QueueEvent(safenew wxSizeEvent(GetSize()));

   // Further processing by default handlers
   event.Skip();
}

///
///  A toolbar has been updated, so handle it like a sizing event.
///
void AudacityProject::OnToolBarUpdate(wxCommandEvent & event)
{
   HandleResize();

   event.Skip(false);             /* No need to propagate any further */
}

///Handles the redrawing necessary for tasks as they partially update in the background.
void AudacityProject::OnODTaskUpdate(wxCommandEvent & WXUNUSED(event))
{
   //todo: add track data to the event - check to see if the project contains it before redrawing.
   if(mTrackPanel)
      mTrackPanel->Refresh(false);

}

//redraws the task and does other book keeping after the task is complete.
void AudacityProject::OnODTaskComplete(wxCommandEvent & WXUNUSED(event))
{
  if(mTrackPanel)
      mTrackPanel->Refresh(false);
 }

void AudacityProject::OnScroll(wxScrollEvent & WXUNUSED(event))
{
   const wxInt64 offset = PixelWidthBeforeTime(0.0);
   mViewInfo.sbarH =
      (wxInt64)(mHsbar->GetThumbPosition() / mViewInfo.sbarScale) - offset;
   DoScroll();
}

void AudacityProject::DoScroll()
{
   const double lowerBound = ScrollingLowerBoundTime();

   int width;
   mTrackPanel->GetTracksUsableArea(&width, NULL);
   mViewInfo.SetBeforeScreenWidth(mViewInfo.sbarH, width, lowerBound);


   if (MayScrollBeyondZero()) {
      enum { SCROLL_PIXEL_TOLERANCE = 10 };
      if (std::abs(mViewInfo.TimeToPosition(0.0, 0
                                   )) < SCROLL_PIXEL_TOLERANCE) {
         // Snap the scrollbar to 0
         mViewInfo.h = 0;
         SetHorizontalThumb(0.0);
      }
   }

   mViewInfo.vpos = mVsbar->GetThumbPosition() * mViewInfo.scrollStep;

   //mchinen: do not always set this project to be the active one.
   //a project may autoscroll while playing in the background
   //I think this is okay since OnMouseEvent has one of these.
   //SetActiveProject(this);

   if (!mAutoScrolling) {
      mTrackPanel->Refresh(false);
   }

   CallAfter(
      [this]{ if (GetTrackPanel())
         GetTrackPanel()->HandleCursorForPresentMouseState(); } );
}

bool AudacityProject::ReportIfActionNotAllowed
   ( const wxString & Name, CommandFlag & flags, CommandFlag flagsRqd, CommandFlag mask )
{
   bool bAllowed = TryToMakeActionAllowed( flags, flagsRqd, mask );
   if( bAllowed )
      return true;
   CommandManager* cm = GetCommandManager();
      if (!cm) return false;
   cm->TellUserWhyDisallowed( Name, flags & mask, flagsRqd & mask);
   return false;
}


/// Determines if flags for command are compatible with current state.
/// If not, then try some recovery action to make it so.
/// @return whether compatible or not after any actions taken.
bool AudacityProject::TryToMakeActionAllowed
   ( CommandFlag & flags, CommandFlag flagsRqd, CommandFlag mask )
{
   bool bAllowed;

   if( !flags )
      flags = GetUpdateFlags();

   bAllowed = ((flags & mask) == (flagsRqd & mask));
   if( bAllowed )
      return true;

   // Why is action not allowed?
   // 1's wherever a required flag is missing.
   auto MissingFlags = (~flags & flagsRqd) & mask;

   if( mStopIfWasPaused && (MissingFlags & AudioIONotBusyFlag ) ){
      StopIfPaused();
      // Hope this will now reflect stopped audio.
      flags = GetUpdateFlags();
      bAllowed = ((flags & mask) == (flagsRqd & mask));
      if( bAllowed )
         return true;
   }

   //We can only make the action allowed if we select audio when no selection.
   // IF not set up to select all audio when none, THEN return with failure.
   if( mWhatIfNoSelection != 1 )
      return false;

   // Some effects disallow autoselection.
   if( flagsRqd & NoAutoSelect )
      return false;

   // Why is action still not allowed?
   // 0's wherever a required flag is missing (or is don't care)
   MissingFlags = (flags & ~flagsRqd) & mask;

   // IF selecting all audio won't do any good, THEN return with failure.
   if( !(flags & WaveTracksExistFlag) )
      return false;
   // returns if mask wants a zero in some flag and that's not present.
   // logic seems a bit peculiar and worth revisiting.
   if( (MissingFlags & ~( TimeSelectedFlag | WaveTracksSelectedFlag)) )
      return false;

   // This was 'OnSelectAll'.  Changing it to OnSelectSomething means if
   // selecting all tracks is enough, we just do that.
   OnSelectSomething(*this);
   flags = GetUpdateFlags();
   bAllowed = ((flags & mask) == (flagsRqd & mask));
   return bAllowed;
}

void AudacityProject::OnMenu(wxCommandEvent & event)
{
#ifdef __WXMSW__
   // Bug 1642: We can arrive here with bogus menu IDs, which we
   // proceed to process.  So if bogus, don't.
   // The bogus menu IDs are probably generated by controls on the TrackPanel, 
   // such as the Project Rate.
   // 17000 is the magic number at which we start our menu.
   // This code would probably NOT be OK on Mac, since we assign
   // some specific ID numbers.
   if( event.GetId() < 17000){
      event.Skip();
      return;
   }
#endif
   bool handled = mCommandManager.HandleMenuID(event.GetId(),
                                               GetUpdateFlags(),
                                               NoFlagsSpecifed);

   if (handled)
      event.Skip(false);
   else{
      event.ResumePropagation( 999 );
      event.Skip(true);
   }
}

void AudacityProject::OnUpdateUI(wxUpdateUIEvent & WXUNUSED(event))
{
   UpdateMenus();
}

void AudacityProject::MacShowUndockedToolbars(bool show)
{
   (void)show;//compiler food
#ifdef __WXMAC__
   // Find all the floating toolbars, and show or hide them
   const auto &children = GetChildren();
   for(const auto &child : children) {
      if (auto frame = dynamic_cast<ToolFrame*>(child)) {
         if (!show)
            frame->Hide();
         else if (frame->GetBar() &&
                  frame->GetBar()->IsVisible())
            frame->Show();
      }
   }
#endif
}

void AudacityProject::OnActivate(wxActivateEvent & event)
{
   // Activate events can fire during window teardown, so just
   // ignore them.
   if (mIsDeleting) {
      return;
   }

   mActive = event.GetActive();

   // Under Windows, focus can be "lost" when returning to
   // Audacity from a different application.
   //
   // This was observed by minimizing all windows using WINDOWS+M and
   // then ALT+TAB to return to Audacity.  Focus will be given to the
   // project window frame which is not at all useful.
   //
   // So, we use ToolManager's observation of focus changes in a wxEventFilter.
   // Then, when we receive the
   // activate event, we restore that focus to the child or the track
   // panel if no child had the focus (which probably should never happen).
   if (!mActive) {
#ifdef __WXMAC__
      if (IsIconized())
         MacShowUndockedToolbars(false);
#endif
   }
   else {
      SetActiveProject(this);
      if ( ! GetToolManager()->RestoreFocus() ) {
         if (mTrackPanel) {
            mTrackPanel->SetFocus();
         }
      }

#ifdef __WXMAC__
      MacShowUndockedToolbars(true);
#endif
   }
   event.Skip();
}

bool AudacityProject::IsActive()
{
   return mActive;
}

void AudacityProject::OnMouseEvent(wxMouseEvent & event)
{
   if (event.ButtonDown())
      SetActiveProject(this);
}

// TitleRestorer restores project window titles to what they were, in its destructor.
class TitleRestorer{
public:
   TitleRestorer(AudacityProject * p ){
      if( p->IsIconized() )
         p->Restore();
      p->Raise(); // May help identifying the window on Mac

      // Construct this projects name and number.
      sProjNumber = "";
      sProjName = p->GetName();
      if (sProjName.IsEmpty()){
         sProjName = _("<untitled>");
         UnnamedCount=AudacityProject::CountUnnamed();
         if( UnnamedCount > 1 ){
            sProjNumber.Printf( "[Project %02i] ", p->GetProjectNumber()+1 );
            AudacityProject::RefreshAllTitles( true ); 
         } 
      } else {
         UnnamedCount = 0;
      }
   };
   ~TitleRestorer() { 
      if( UnnamedCount > 1 )
         AudacityProject::RefreshAllTitles( false ); 
   };
   wxString sProjNumber;
   wxString sProjName;
   int UnnamedCount;
};


// LL: All objects that have a reference to the DirManager should
//     be deleted before the final mDirManager->Deref() in this
//     routine.  Failing to do so can cause unwanted recursion
//     and/or attempts to DELETE objects twice.
void AudacityProject::OnCloseWindow(wxCloseEvent & event)
{
   // We are called for the wxEVT_CLOSE_WINDOW, wxEVT_END_SESSION, and
   // wxEVT_QUERY_END_SESSION, so we have to protect against multiple
   // entries.  This is a hack until the whole application termination
   // process can be reviewed and reworked.  (See bug #964 for ways
   // to exercise the bug that instigated this hack.)
   if (mIsBeingDeleted)
   {
      event.Skip();
      return;
   }

   if (event.CanVeto() && (::wxIsBusy() || mbBusyImporting))
   {
      event.Veto();
      return;
   }

   // TODO: consider postponing these steps until after the possible veto
   // below:  closing the two analysis dialogs, and stopping audio streams.
   // Streams can be for play, recording, or monitoring.  But maybe it still
   // makes sense to stop any recording before putting up the dialog.

   mFreqWindow.reset();

   // Check to see if we were playing or recording
   // audio, and if so, make sure Audio I/O is completely finished.
   // The main point of this is to properly push the state
   // and flush the tracks once we've completely finished
   // recording NEW state.
   // This code is derived from similar code in
   // AudacityProject::~AudacityProject() and TrackPanel::OnTimer().
   if (GetAudioIOToken()>0 &&
       gAudioIO->IsStreamActive(GetAudioIOToken())) {

      // We were playing or recording audio, but we've stopped the stream.
      wxCommandEvent dummyEvent;
      GetControlToolBar()->OnStop(dummyEvent);

      FixScrollbars();
      SetAudioIOToken(0);
      RedrawProject();
   }
   else if (gAudioIO->IsMonitoring()) {
      gAudioIO->StopStream();
   }

   // MY: Use routine here so other processes can make same check
   bool bHasTracks = ProjectHasTracks();

   // We may not bother to prompt the user to save, if the
   // project is now empty.
   if (event.CanVeto() && (mEmptyCanBeDirty || bHasTracks)) {
      if (GetUndoManager()->UnsavedChanges()) {
         TitleRestorer Restorer( this );// RAII
         /* i18n-hint: The first %s numbers the project, the second %s is the project name.*/
         wxString Title =  wxString::Format(_("%sSave changes to %s?"), Restorer.sProjNumber, Restorer.sProjName);
         wxString Message = _("Save project before closing?");
         if( !bHasTracks )
         {
          Message += _("\nIf saved, the project will have no tracks.\n\nTo save any previously open tracks:\nCancel, Edit > Undo until all tracks\nare open, then File > Save Project.");
         }
         int result = AudacityMessageBox( Message,
                                    Title,
                                   wxYES_NO | wxCANCEL | wxICON_QUESTION,
                                   this);

         if (result == wxCANCEL || (result == wxYES &&
              !GuardedCall<bool>( [&]{ return Save(); } )
         )) {
            event.Veto();
            return;
         }
      }
   }

#ifdef __WXMAC__
   // Fix bug apparently introduced into 2.1.2 because of wxWidgets 3:
   // closing a project that was made full-screen (as by clicking the green dot
   // or command+/; not merely "maximized" as by clicking the title bar or
   // Zoom in the Window menu) leaves the screen black.
   // Fix it by un-full-screening.
   // (But is there a different way to do this? What do other applications do?
   //  I don't see full screen windows of Safari shrinking, but I do see
   //  momentary blackness.)
   ShowFullScreen(false);
#endif

   ModuleManager::Get().Dispatch(ProjectClosing);

   // Stop the timer since there's no need to update anything anymore
   mTimer.reset();

   // DMM: Save the size of the last window the user closes
   //
   // LL: Save before doing anything else to the window that might make
   //     its size change.
      SaveWindowSize();

   mIsDeleting = true;

   // Mac: we never quit as the result of a close.
   // Other systems: we quit only when the close is the result of an external
   // command (on Windows, those are taskbar closes, "X" box, Alt+F4, etc.)
   bool quitOnClose;
#ifdef __WXMAC__
   quitOnClose = false;
#else
   quitOnClose = !mMenuClose;
#endif

   // DanH: If we're definitely about to quit, DELETE the clipboard.
   //       Doing this after Deref'ing the DirManager causes problems.
   if ((gAudacityProjects.size() == 1) && (quitOnClose || gIsQuitting))
      DeleteClipboard();

   // JKC: For Win98 and Linux do not detach the menu bar.
   // We want wxWidgets to clean it up for us.
   // TODO: Is there a Mac issue here??
   // SetMenuBar(NULL);

   // Lock all blocks in all tracks of the last saved version, so that
   // the blockfiles aren't deleted on disk when we DELETE the blockfiles
   // in memory.  After it's locked, DELETE the data structure so that
   // there's no memory leak.
   if (mLastSavedTracks) {
      TrackListIterator iter(mLastSavedTracks.get());
      Track *t = iter.First();
      while (t) {
         if (t->GetKind() == Track::Wave)
            ((WaveTrack *) t)->CloseLock();
         t = iter.Next();
      }

      mLastSavedTracks->Clear(); // sends an event
      mLastSavedTracks.reset();
   }

   // Get rid of the history window
   // LL:  Destroy it before the TrackPanel and ToolBars since they
   //      may/will get additional wxEVT_PAINT events since window
   //      destruction may be queued.  This seems to only be a problem
   //      on the Mac.
   if (mHistoryWindow) {
      mHistoryWindow->Destroy();
      mHistoryWindow = NULL;
   }

   // Some of the AdornedRulerPanel functions refer to the TrackPanel, so destroy this
   // before the TrackPanel is destroyed. This change was needed to stop Audacity
   // crashing when running with Jaws on Windows 10 1703.
   if (mRuler)
	   mRuler->Destroy();

   // Destroy the TrackPanel early so it's not around once we start
   // deleting things like tracks and such out from underneath it.
   // Check validity of mTrackPanel per bug 584 Comment 1.
   // Deeper fix is in the Import code, but this failsafes against crash.
   if (mTrackPanel)
   {
      mTrackPanel->Destroy();
      mTrackPanel = NULL;              // Make sure this gets set...see HandleResize()
   }

   // Delete the tool manager before the children since it needs
   // to save the state of the toolbars.
   mToolManager.reset();

   DestroyChildren();

   mTrackFactory.reset();

   mTags.reset();

   mImportXMLTagHandler.reset();

   // Delete all the tracks to free up memory and DirManager references.
   mTracks->Clear();
   mTracks.reset();

   // This must be done before the following Deref() since it holds
   // references to the DirManager.
   GetUndoManager()->ClearStates();

   // MM: Tell the DirManager it can now DELETE itself
   // if it finds it is no longer needed. If it is still
   // used (f.e. by the clipboard), it will recognize this
   // and will destroy itself later.
   //
   // LL: All objects with references to the DirManager should
   //     have been deleted before this.
   mDirManager.reset();

   AProjectHolder pSelf;
   {
      ODLocker locker{ &AudacityProject::AllProjectDeleteMutex() };
      auto end = gAudacityProjects.end();
      auto it = std::find_if(gAudacityProjects.begin(), end,
         [this] (const AProjectHolder &p) { return p.get() == this; });
      wxASSERT( it != end );
      pSelf = std::move( *it );
      gAudacityProjects.erase(it);
   }

   if (gActiveProject == this) {
      // Find a NEW active project
      if (gAudacityProjects.size() > 0) {
         SetActiveProject(gAudacityProjects[0].get());
      }
      else {
         SetActiveProject(NULL);
      }
   }

   // Since we're going to be destroyed, make sure we're not to
   // receive audio notifications anymore.
   if (gAudioIO->GetListener() == this) {
      gAudioIO->SetListener(gActiveProject);
   }

   if (gAudacityProjects.empty() && !gIsQuitting) {

#if !defined(__WXMAC__)
      if (quitOnClose) {
         QuitAudacity();
      }
      else {
         wxGetApp().SetWindowRectAlreadySaved(FALSE);
         CreateNewAudacityProject();
      }
#endif
   }

   // Destroys this
   pSelf.reset();
   mRuler = nullptr;

   mIsBeingDeleted = true;

}

void AudacityProject::OnOpenAudioFile(wxCommandEvent & event)
{
   const wxString &cmd = event.GetString();

   if (!cmd.IsEmpty()) {
      OpenFile(cmd);
   }

   RequestUserAttention();
}

// static method, can be called outside of a project
wxArrayString AudacityProject::ShowOpenDialog(const wxString &extraformat, const wxString &extrafilter)
{
   FormatList l;
   wxString filter;  ///< List of file format names and extensions, separated
   /// by | characters between _formats_ and extensions for each _format_, i.e.
   /// format1name | *.ext | format2name | *.ex1;*.ex2
   wxString all;  ///< One long list of all supported file extensions,
   /// semicolon separated

   if (extraformat != wxEmptyString)
   {  // additional format specified
      all = extrafilter + wxT(';');
      // add it to the "all supported files" filter string
   }

   // Construct the filter
   Importer::Get().GetSupportedImportFormats(&l);

   for (const auto &format : l) {
      /* this loop runs once per supported _format_ */
      const Format *f = &format;

      wxString newfilter = f->formatName + wxT("|");
      // bung format name into string plus | separator
      for (size_t i = 0; i < f->formatExtensions.size(); i++) {
         /* this loop runs once per valid _file extension_ for file containing
          * the current _format_ */
         if (!newfilter.Contains(wxT("*.") + f->formatExtensions[i] + wxT(";")))
            newfilter += wxT("*.") + f->formatExtensions[i] + wxT(";");
         if (!all.Contains(wxT("*.") + f->formatExtensions[i] + wxT(";")))
            all += wxT("*.") + f->formatExtensions[i] + wxT(";");
      }
      newfilter.RemoveLast(1);
      filter += newfilter;
      filter += wxT("|");
   }
   all.RemoveLast(1);
   filter.RemoveLast(1);

   /* i18n-hint: The vertical bars and * are essential here.*/
   wxString mask = _("All files|*|All supported files|") +
                   all + wxT("|"); // "all" and "all supported" entries
   if (extraformat != wxEmptyString)
   {  // append caller-defined format if supplied
      mask +=  extraformat + wxT("|") + extrafilter + wxT("|");
   }
   mask += filter;   // put the names and extensions of all the importer formats
   // we built up earlier into the mask

   // Retrieve saved path and type
   auto path = FileNames::FindDefaultPath(FileNames::Operation::Open);
   wxString type = gPrefs->Read(wxT("/DefaultOpenType"),mask.BeforeFirst(wxT('|')));

   // Convert the type to the filter index
   int index = mask.First(type + wxT("|"));
   if (index == wxNOT_FOUND) {
      index = 0;
   }
   else {
      index = mask.Left(index).Freq(wxT('|')) / 2;
      if (index < 0) {
         index = 0;
      }
   }

   // Construct and display the file dialog
   wxArrayString selected;

   FileDialogWrapper dlog(NULL,
                   _("Select one or more files"),
                   path,
                   wxT(""),
                   mask,
                   wxFD_OPEN | wxFD_MULTIPLE | wxRESIZE_BORDER);

   dlog.SetFilterIndex(index);

   int dialogResult = dlog.ShowModal();

   // Convert the filter index to type and save
   index = dlog.GetFilterIndex();
   for (int i = 0; i < index; i++) {
      mask = mask.AfterFirst(wxT('|')).AfterFirst(wxT('|'));
   }
   gPrefs->Write(wxT("/DefaultOpenType"), mask.BeforeFirst(wxT('|')));
   gPrefs->Write(wxT("/LastOpenType"), mask.BeforeFirst(wxT('|')));
   gPrefs->Flush();

   if (dialogResult == wxID_OK) {
      // Return the selected files
      dlog.GetPaths(selected);
   }
   return selected;
}

// static method, can be called outside of a project
bool AudacityProject::IsAlreadyOpen(const wxString & projPathName)
{
   const wxFileName newProjPathName(projPathName);
   size_t numProjects = gAudacityProjects.size();
   for (size_t i = 0; i < numProjects; i++)
   {
      if (newProjPathName.SameAs(gAudacityProjects[i]->mFileName))
      {
         wxString errMsg =
            wxString::Format(_("%s is already open in another window."),
                              newProjPathName.GetName());
         wxLogError(errMsg);
         AudacityMessageBox(errMsg, _("Error Opening Project"), wxOK | wxCENTRE);
         return true;
      }
   }
   return false;
}

// static method, can be called outside of a project
void AudacityProject::OpenFiles(AudacityProject *proj)
{
	/*
   // i18n-hint: This string is a label in the file type filter in the open
   // and save dialogues, for the option that only shows project files created
   // with Audacity. Do not include pipe symbols or .aup (this extension will
   // now be added automatically for the Save Projects dialogues).
   wxArrayString selectedFiles = ShowOpenDialog(_("Audacity projects"), wxT("*.aup"));
   if (selectedFiles.GetCount() == 0) {
      gPrefs->Write(wxT("/LastOpenType"),wxT(""));
      gPrefs->Flush();
      return;
   }

   //sort selected files by OD status.
   //For the open menu we load OD first so user can edit asap.
   //first sort selectedFiles.
   selectedFiles.Sort(CompareNoCaseFileName);
   ODManager::Pauser pauser;

   auto cleanup = finally( [] {
      gPrefs->Write(wxT("/LastOpenType"),wxT(""));
      gPrefs->Flush();
   } );

   for (size_t ff = 0; ff < selectedFiles.GetCount(); ff++) {
      const wxString &fileName = selectedFiles[ff];

      // Make sure it isn't already open.
      if (AudacityProject::IsAlreadyOpen(fileName))
         continue; // Skip ones that are already open.

      FileNames::UpdateDefaultPath(FileNames::Operation::Open, fileName);

      // DMM: If the project is dirty, that means it's been touched at
      // all, and it's not safe to open a NEW project directly in its
      // place.  Only if the project is brand-NEW clean and the user
      // hasn't done any action at all is it safe for Open to take place
      // inside the current project.
      //
      // If you try to Open a NEW project inside the current window when
      // there are no tracks, but there's an Undo history, etc, then
      // bad things can happen, including data files moving to the NEW
      // project directory, etc.
      if ( proj && ( proj->mDirty || !proj->mTracks->empty() ) )
         proj = nullptr;

      // This project is clean; it's never been touched.  Therefore
      // all relevant member variables are in their initial state,
      // and it's okay to open a NEW project inside this window.
      proj = AudacityProject::OpenProject( proj, fileName );
   }
   */
}

AudacityProject *AudacityProject::OpenProject(
   AudacityProject *pProject, const wxString &fileNameArg, bool addtohistory)
{
   AudacityProject *pNewProject = nullptr;
   return pProject;
}

// FIXME:? TRAP_ERR This should return a result that is checked.
//    See comment in AudacityApp::MRUOpen().
void AudacityProject::OpenFile(const wxString &fileNameArg, bool addtohistory)
{
}

void AudacityProject::EnqueueODTasks()
{
   //check the ODManager to see if we should add the tracks to the ODManager.
   //this flag would have been set in the HandleXML calls from above, if there were
   //OD***Blocks.
   if(ODManager::HasLoadedODFlag())
   {
      Track *tr;
      TrackListIterator triter(GetTracks());
      tr = triter.First();

      std::vector<movable_ptr<ODTask>> newTasks;
      //std::vector<ODDecodeTask*> decodeTasks;
      unsigned int createdODTasks=0;
      while (tr) {
         if (tr->GetKind() == Track::Wave) {
            //check the track for blocks that need decoding.
            //There may be more than one type e.g. FLAC/FFMPEG/lame
            unsigned int odFlags;
            odFlags=((WaveTrack*)tr)->GetODFlags();

            //add the track to the already created tasks that correspond to the od flags in the wavetrack.
            for(unsigned int i=0;i<newTasks.size();i++) {
               if(newTasks[i]->GetODType() & odFlags)
                  newTasks[i]->AddWaveTrack((WaveTrack*)tr);
            }

            //create whatever NEW tasks we need to.
            //we want at most one instance of each class for the project
            while((odFlags|createdODTasks) != createdODTasks)
            {
               movable_ptr<ODTask> newTask;
               if(!(createdODTasks&ODTask::eODPCMSummary) && (odFlags & ODTask::eODPCMSummary)) {
                  newTask = make_movable<ODComputeSummaryTask>();
                  createdODTasks= createdODTasks | ODTask::eODPCMSummary;
               }
               else {
                  wxPrintf("unrecognized OD Flag in block file.\n");
                  //TODO:ODTODO: display to user.  This can happen when we build audacity on a system that doesnt have libFLAC
                  break;
               }
               if(newTask)
               {
                  newTask->AddWaveTrack((WaveTrack*)tr);
                  newTasks.push_back(std::move(newTask));
               }
            }
         }
         tr = triter.Next();
      }
      for(unsigned int i=0;i<newTasks.size();i++)
         ODManager::Instance()->AddNewTask(std::move(newTasks[i]));
   }
}

bool AudacityProject::HandleXMLTag(const wxChar *tag, const wxChar **attrs)
{
   bool bFileVersionFound = false;
   wxString fileVersion = _("<unrecognized version -- possibly corrupt project file>");
   wxString audacityVersion = _("<unrecognized version -- possibly corrupt project file>");
   int requiredTags = 0;
   long longVpos = 0;

   // loop through attrs, which is a null-terminated list of
   // attribute-value pairs
   while(*attrs) {
      const wxChar *attr = *attrs++;
      const wxChar *value = *attrs++;

      if (!value || !XMLValueChecker::IsGoodString(value))
         break;

      if (mViewInfo.ReadXMLAttribute(attr, value)) {
         // We need to save vpos now and restore it below
         longVpos = std::max(longVpos, long(mViewInfo.vpos));
         continue;
      }

      if (!wxStrcmp(attr, wxT("datadir")))
      {
         //
         // This is an auto-saved version whose data is in another directory
         //
         // Note: This attribute must currently be written and parsed before
         //       any other attributes
         //
         if ((value[0] != 0) && XMLValueChecker::IsGoodPathString(value))
         {
            // Remember that this is a recovered project
            mIsRecovered = true;
            mRecoveryAutoSaveDataDir = value;
         }
      }

      else if (!wxStrcmp(attr, wxT("version")))
      {
         fileVersion = value;
         bFileVersionFound = true;
         requiredTags++;
      }

      else if (!wxStrcmp(attr, wxT("audacityversion"))) {
         audacityVersion = value;
         requiredTags++;
      }

      else if (!wxStrcmp(attr, wxT("projname"))) {
         wxString projName;
         wxString projPath;

         if (mIsRecovered) {
            // Fake the filename as if we had opened the original file
            // (which was lost by the crash) rather than the one in the
            // auto save folder
            wxFileName realFileDir;
            realFileDir.AssignDir(mRecoveryAutoSaveDataDir);
            realFileDir.RemoveLastDir();

            wxString realFileName = value;
            if (realFileName.Length() >= 5 &&
                realFileName.Right(5) == wxT("_data"))
            {
               realFileName = realFileName.Left(realFileName.Length() - 5);
            }

            if (realFileName.IsEmpty())
            {
               // A previously unsaved project has been recovered, so fake
               // an unsaved project. The data files just stay in the temp
               // directory
               mDirManager->SetLocalTempDir(mRecoveryAutoSaveDataDir);
               mFileName = wxT("");
               projName = wxT("");
               projPath = wxT("");
            } else
            {
               realFileName += wxT(".aup");
               projPath = realFileDir.GetFullPath();
               mFileName = wxFileName(projPath, realFileName).GetFullPath();
               mbLoadedFromAup = true;
               projName = value;
            }

            SetProjectTitle();
         } else {
            projName = value;
            projPath = wxPathOnly(mFileName);
         }

         if (!projName.IsEmpty())
         {
            // First try to load the data files based on the _data dir given in the .aup file
            // If this fails then try to use the filename of the .aup as the base directory
            // This is because unzipped projects e.g. those that get transfered between mac-pc
            // have encoding issues and end up expanding the wrong filenames for certain
            // international characters (such as capital 'A' with an umlaut.)
            if (!mDirManager->SetProject(projPath, projName, false))
            {
               projName = GetName() + wxT("_data");
               if (!mDirManager->SetProject(projPath, projName, false)) {
                  AudacityMessageBox(wxString::Format(_("Couldn't find the project data folder: \"%s\""),
                                             projName),
                                             _("Error Opening Project"),
                                             wxOK | wxCENTRE, this);
                  return false;
               }
            }
         }

         requiredTags++;
      }

      else if (!wxStrcmp(attr, wxT("rate"))) {
         Internat::CompatibleToDouble(value, &mRate);
         GetSelectionBar()->SetRate(mRate);
      }

      else if (!wxStrcmp(attr, wxT("snapto"))) {
         SetSnapTo(wxString(value) == wxT("on") ? true : false);
      }
      else if (!wxStrcmp(attr, wxT("selectionformat")))
         SetSelectionFormat(value);
   } // while

   mViewInfo.UpdatePrefs();

   if (longVpos != 0) {
      // PRL: It seems this must happen after SetSnapTo
       mViewInfo.vpos = longVpos;
       mbInitializingScrollbar = true;
   }

   // Specifically detect newer versions of Audacity
   // WARNING: This will need review/revision if we ever have a version string
   // such as 1.5.10, i.e. with 2 digit numbers.
   // We're able to do a shortcut and use string comparison because we know
   // that does not happen.
   // TODO: Um.  We actually have released 0.98 and 1.3.14 so the comment
   // above is inaccurate.

   if (!bFileVersionFound ||
         (fileVersion.Length() != 5) || // expecting '1.1.0', for example
         // JKC: I commentted out next line.  IsGoodInt is not for
         // checking dotted numbers.
         //!XMLValueChecker::IsGoodInt(fileVersion) ||
         (fileVersion > wxT(AUDACITY_FILE_FORMAT_VERSION)))
   {
      wxString msg;
      /* i18n-hint: %s will be replaced by the version number.*/
      msg.Printf(_("This file was saved using Audacity %s.\nYou are using Audacity %s. You may need to upgrade to a newer version to open this file."),
                 audacityVersion,
                 AUDACITY_VERSION_STRING);
      AudacityMessageBox(msg,
                   _("Can't open project file"),
                   wxOK | wxICON_EXCLAMATION | wxCENTRE, this);
      return false;
   }

   // NOTE: It looks as if there was some confusion about fileversion and audacityversion.
   // fileversion NOT being increased when file formats changed, so unfortunately we're
   // using audacityversion to rescue the situation.

   // KLUDGE: guess the true 'fileversion' by stripping away any '-beta-Rc' stuff on
   // audacityVersion.
   // It's fairly safe to do this as it has already been established that the
   // puported file version was five chars long.
   fileVersion = audacityVersion.Mid(0,5);

   bool bIsOld = fileVersion < wxT(AUDACITY_FILE_FORMAT_VERSION);
   bool bIsVeryOld = fileVersion < wxT("1.1.9" );
   // Very old file versions could even have the file version starting
   // with text: 'AudacityProject Version 0.95'
   // Atoi return zero in this case.
   bIsVeryOld |= wxAtoi( fileVersion )==0;
   // Specifically detect older versions of Audacity
   if ( bIsOld | bIsVeryOld ) {
      wxString msg;
      // msg.Printf(gsLegacyFileWarning(), audacityVersion);

      int icon_choice = wxICON_EXCLAMATION;
      if( bIsVeryOld )
         // Stop icon, and choose 'NO' by default.
         icon_choice = wxICON_STOP | wxNO_DEFAULT;
      int action =
         AudacityMessageBox(msg,
                      _("Warning - Opening Old Project File"),
                      wxYES_NO | icon_choice | wxCENTRE,
                      this);
      if (action == wxNO)
         return false;
   }

   if (wxStrcmp(tag, wxT("audacityproject")) &&
       wxStrcmp(tag, wxT("project"))) {
      // If the tag name is not one of these two (the NEW name is
      // "project" with an Audacity namespace, but we don't detect
      // the namespace yet), then we don't know what the error is
      return false;
   }

   if (requiredTags < 3)
      return false;

   // All other tests passed, so we succeed
   return true;
}

XMLTagHandler *AudacityProject::HandleXMLChild(const wxChar *tag)
{
   if (!wxStrcmp(tag, wxT("tags"))) {
      return mTags.get();
   }

   // Note that TrackList::Add includes assignment of unique in-session TrackId
   // to a reloaded track, though no promise that it equals the id it originally
   // had

   if (!wxStrcmp(tag, wxT("wavetrack"))) {
      return mTracks->Add(mTrackFactory->NewWaveTrack());
   }

   if (!wxStrcmp(tag, wxT("recordingrecovery"))) {
      if (!mRecordingRecoveryHandler)
         mRecordingRecoveryHandler = std::make_unique<RecordingRecoveryHandler>(this);
      return mRecordingRecoveryHandler.get();
   }

   if (!wxStrcmp(tag, wxT("import"))) {
      if (!mImportXMLTagHandler)
         mImportXMLTagHandler = std::make_unique<ImportXMLTagHandler>(this);
      return mImportXMLTagHandler.get();
   }

   return NULL;
}

void AudacityProject::WriteXMLHeader(XMLWriter &xmlFile) const
{
   xmlFile.Write(wxT("<?xml "));
   xmlFile.Write(wxT("version=\"1.0\" "));
   xmlFile.Write(wxT("standalone=\"no\" "));
   xmlFile.Write(wxT("?>\n"));

   xmlFile.Write(wxT("<!DOCTYPE "));
   xmlFile.Write(wxT("project "));
   xmlFile.Write(wxT("PUBLIC "));
   xmlFile.Write(wxT("\"-//audacityproject-1.3.0//DTD//EN\" "));
   xmlFile.Write(wxT("\"http://audacity.sourceforge.net/xml/audacityproject-1.3.0.dtd\" "));
   xmlFile.Write(wxT(">\n"));
}

void AudacityProject::WriteXML(XMLWriter &xmlFile, bool bWantSaveCompressed)
// may throw
{
   //TIMER_START( "AudacityProject::WriteXML", xml_writer_timer );
   // Warning: This block of code is duplicated in Save, for now...
   wxString project = mFileName;
   if (project.Len() > 4 && project.Mid(project.Len() - 4) == wxT(".aup"))
      project = project.Mid(0, project.Len() - 4);
   wxString projName = wxFileNameFromPath(project) + wxT("_data");
   // End Warning -DMM

   xmlFile.StartTag(wxT("project"));
   xmlFile.WriteAttr(wxT("xmlns"), wxT("http://audacity.sourceforge.net/xml/"));

   if (mAutoSaving)
   {
      //
      // When auto-saving, remember full path to data files directory
      //
      // Note: This attribute must currently be written and parsed before
      //       all other attributes
      //
      xmlFile.WriteAttr(wxT("datadir"), mDirManager->GetDataFilesDir());

      // Note that the code at the start assumes that if mFileName has a value
      // then the file has been saved.  This is not neccessarily true when
      // autosaving as it gets set by AddImportedTracks (presumably as a proposal).
      // I don't think that mDirManager.projName gets set without a save so check that.
      if( !IsProjectSaved() )
         projName = wxT("_data");
   }

   xmlFile.WriteAttr(wxT("projname"), projName);
   xmlFile.WriteAttr(wxT("version"), wxT(AUDACITY_FILE_FORMAT_VERSION));
   xmlFile.WriteAttr(wxT("audacityversion"), AUDACITY_VERSION_STRING);

   mViewInfo.WriteXMLAttributes(xmlFile);
   xmlFile.WriteAttr(wxT("rate"), mRate);
   xmlFile.WriteAttr(wxT("snapto"), GetSnapTo() ? wxT("on") : wxT("off"));
   xmlFile.WriteAttr(wxT("selectionformat"), GetSelectionFormat());
   xmlFile.WriteAttr(wxT("frequencyformat"), GetFrequencySelectionFormatName());
   xmlFile.WriteAttr(wxT("bandwidthformat"), GetBandwidthSelectionFormatName());

   mTags->WriteXML(xmlFile);

   const Track *t;
   WaveTrack* pWaveTrack;
   TrackListConstIterator iter(GetTracks());
   t = iter.First();
   unsigned int ndx = 0;
   while (t) {
      if ((t->GetKind() == Track::Wave) && bWantSaveCompressed)
      {
         auto wt = static_cast<const WaveTrack *>(t);

         //vvv This should probably be a method, WaveTrack::WriteCompressedTrackXML().
         xmlFile.StartTag(wxT("import"));
         xmlFile.WriteAttr(wxT("filename"), mStrOtherNamesArray[ndx]); // Assumes mTracks order hasn't changed!

         // Don't store "channel" and "linked" tags because the importer can figure that out,
         // e.g., from stereo Ogg files.
         //    xmlFile.WriteAttr(wxT("channel"), t->GetChannel());
         //    xmlFile.WriteAttr(wxT("linked"), t->GetLinked());

         xmlFile.WriteAttr(wxT("offset"), t->GetOffset(), 8);
         xmlFile.WriteAttr(wxT("mute"), wt->GetMute());
         xmlFile.WriteAttr(wxT("height"), t->GetActualHeight());
         xmlFile.WriteAttr(wxT("minimized"), t->GetMinimized());

         pWaveTrack = (WaveTrack*)t;
         // Don't store "rate" tag because the importer can figure that out.
         //    xmlFile.WriteAttr(wxT("rate"), pWaveTrack->GetRate());
         xmlFile.WriteAttr(wxT("gain"), (double)pWaveTrack->GetGain());
         xmlFile.WriteAttr(wxT("pan"), (double)pWaveTrack->GetPan());
         xmlFile.EndTag(wxT("import"));

         ndx++;
         if (t->GetLinked())
            t = iter.Next();
      }
      else if (t->GetKind() == Track::Wave)
      {
         pWaveTrack = (WaveTrack*)t;
         pWaveTrack->SetAutoSaveIdent(mAutoSaving ? ++ndx : 0);
         t->WriteXML(xmlFile);
      }
      else
      {
         t->WriteXML(xmlFile);
      }

      t = iter.Next();
   }

   if (!mAutoSaving)
   {
      // Only write closing bracket when not auto-saving, since we may add
      // recording log data to the end of the file later
      xmlFile.EndTag(wxT("project"));
   }
   //TIMER_STOP( xml_writer_timer );

}

bool AudacityProject::Save()
{
   if ( !IsProjectSaved() )
      return SaveAs();

   return DoSave(false, false);
}

// Assumes AudacityProject::mFileName has been set to the desired path.
bool AudacityProject::DoSave
   (const bool fromSaveAs, const bool bWantSaveCompressed)
{
   return true;
}

#ifdef USE_LIBVORBIS
   bool AudacityProject::SaveCompressedWaveTracks(const wxString & strProjectPathName) // full path for aup except extension
   {
      // Some of this is similar to code in ExportMultiple::ExportMultipleByTrack
      // but that code is really tied into the dialogs.

      // Copy the tracks because we're going to do some state changes before exporting.
      Track* pTrack;
      WaveTrack* pWaveTrack;
      TrackListOfKindIterator iter(Track::Wave, GetTracks());
      unsigned int numWaveTracks = 0;

      auto ppSavedTrackList = TrackList::Create();
      auto &pSavedTrackList = *ppSavedTrackList;

      for (pTrack = iter.First(); pTrack != NULL; pTrack = iter.Next())
      {
         numWaveTracks++;
         pWaveTrack = (WaveTrack*)pTrack;
         pSavedTrackList.Add(mTrackFactory->DuplicateWaveTrack(*pWaveTrack));
      }
      auto cleanup = finally( [&] {
         // Restore the saved track states and clean up.
         TrackListIterator savedTrackIter(&pSavedTrackList);
         Track *pSavedTrack;
         for (pTrack = iter.First(), pSavedTrack = savedTrackIter.First();
              ((pTrack != NULL) && (pSavedTrack != NULL));
              pTrack = iter.Next(), pSavedTrack = savedTrackIter.Next())
         {
            pWaveTrack = static_cast<WaveTrack*>(pTrack);
            auto pSavedWaveTrack = static_cast<const WaveTrack*>(pSavedTrack);

            pWaveTrack->SetSelected(pSavedTrack->GetSelected());
            pWaveTrack->SetMute(pSavedWaveTrack->GetMute());

            pWaveTrack->SetGain(((WaveTrack*)pSavedTrack)->GetGain());
            pWaveTrack->SetPan(((WaveTrack*)pSavedTrack)->GetPan());
         }
      } );

      if (numWaveTracks == 0)
         // Nothing to save compressed => success. Delete the copies and go.
         return true;

      // Okay, now some bold state-faking to default values.
      for (pTrack = iter.First(); pTrack != NULL; pTrack = iter.Next())
      {
         pWaveTrack = (WaveTrack*)pTrack;

         pWaveTrack->SetSelected(false);
         pWaveTrack->SetMute(false);

         pWaveTrack->SetGain(1.0);
         pWaveTrack->SetPan(0.0);
      }

      wxString strDataDirPathName = strProjectPathName + wxT("_data");
      if (!wxFileName::DirExists(strDataDirPathName) &&
            !wxFileName::Mkdir(strDataDirPathName, 0777, wxPATH_MKDIR_FULL))
         return false;
      strDataDirPathName += wxFileName::GetPathSeparator();

      // Export all WaveTracks to OGG.
      bool bSuccess = true;

      // This accumulates the names of the .ogg files, to be written as
      // dependencies in the .aup file
      mStrOtherNamesArray.clear();

      Exporter theExporter;
      Track* pRightTrack;
      wxFileName uniqueTrackFileName;
      for (pTrack = iter.First(); ((pTrack != NULL) && bSuccess); pTrack = iter.Next())
      {
         if (pTrack->GetKind() == Track::Wave)
         {
            SelectionStateChanger changer{ GetSelectionState(), *GetTracks() };
            pTrack->SetSelected(true);
            if (pTrack->GetLinked())
            {
               pRightTrack = iter.Next();
               pRightTrack->SetSelected(true);
            }
            else
               pRightTrack = NULL;

            uniqueTrackFileName = wxFileName(strDataDirPathName, pTrack->GetName(), wxT("ogg"));
            FileNames::MakeNameUnique(mStrOtherNamesArray, uniqueTrackFileName);
            bSuccess =
               theExporter.Process(this, pRightTrack ? 2 : 1,
                                    wxT("OGG"), uniqueTrackFileName.GetFullPath(), true,
                                    pTrack->GetStartTime(), pTrack->GetEndTime());

            if (!bSuccess)
               // If only some exports succeed, the cleanup is not done here
               // but trusted to the caller
               break;
         }
      }

      return bSuccess;
   }
#endif


std::vector< std::shared_ptr< Track > >
AudacityProject::AddImportedTracks(const wxString &fileName,
                                   TrackHolders &&newTracks)
{
   std::vector< std::shared_ptr< Track > > results;

   const auto numTracks = newTracks.size();
   SelectNone();

   bool initiallyEmpty = mTracks->empty();
   double newRate = 0;
   wxString trackNameBase = fileName.AfterLast(wxFILE_SEP_PATH).BeforeLast('.');
   bool isLinked = false;
   int i = -1;
   for (auto &uNewTrack : newTracks) {
      ++i;

      auto newTrack = mTracks->Add(std::move(uNewTrack));
      results.push_back(Track::Pointer(newTrack));
      if (newRate == 0 && newTrack->GetKind() == Track::Wave) {
         newRate = ((WaveTrack *)newTrack)->GetRate();
      }
      newTrack->SetSelected(true);
      //we need to check link status based on the first channel only.
      if(0==i)
         isLinked = newTrack->GetLinked();
      if (numTracks > 2 || (numTracks > 1 && !isLinked) ) {
         newTrack->SetName(trackNameBase + wxString::Format(wxT(" %d" ), i + 1));
      }
      else {
         newTrack->SetName(trackNameBase);
      }

      // Check if NEW track contains aliased blockfiles and if yes,
      // remember this to show a warning later
      if (newTrack->GetKind() == WaveTrack::Wave)
      {
         if (WaveClip* clip = ((WaveTrack*)newTrack)->GetClipByIndex(0)) {
            BlockArray &blocks = clip->GetSequence()->GetBlockArray();
            if (blocks.size())
            {
               SeqBlock& block = blocks[0];
               if (block.f->IsAlias())
               {
                  mImportedDependencies = true;
               }
            }
         }
      }
   }

   // Automatically assign rate of imported file to whole project,
   // if this is the first file that is imported
   if (initiallyEmpty && newRate > 0) {
      mRate = newRate;
      GetSelectionBar()->SetRate(mRate);
   }

   PushState(wxString::Format(_("Imported '%s'"), fileName),
             _("Import"));

#if defined(__WXGTK__)
   // See bug #1224
   // The track panel hasn't we been fully created, so the OnZoomFit() will not give
   // expected results due to a window width of zero.  Should be safe to yield here to
   // allow the creattion to complete.  If this becomes a problem, it "might" be possible
   // to queue a dummy event to trigger the OnZoomFit().
   wxEventLoopBase::GetActive()->YieldFor(wxEVT_CATEGORY_UI | wxEVT_CATEGORY_USER_INPUT);
#endif

   if (initiallyEmpty && !IsProjectSaved() ) {
      wxString name = fileName.AfterLast(wxFILE_SEP_PATH).BeforeLast(wxT('.'));
      mFileName =::wxPathOnly(fileName) + wxFILE_SEP_PATH + name + wxT(".aup");
      mbLoadedFromAup = false;
      SetProjectTitle();
   }

   // Moved this call to higher levels to prevent flicker redrawing everything on each file.
   //   HandleResize();

   newTracks.clear();

   return results;
}

void AudacityProject::ZoomAfterImport(Track *pTrack)
{
   OnZoomFit(*this);

   mTrackPanel->SetFocus();
   RedrawProject();
   if (!pTrack)
      pTrack = mTrackPanel->GetFirstSelectedTrack();
   mTrackPanel->EnsureVisible(pTrack);
}

// If pNewTrackList is passed in non-NULL, it gets filled with the pointers to NEW tracks.
bool AudacityProject::Import(const wxString &fileName, WaveTrackArray* pTrackArray /*= NULL*/)
{
	// goal: I want this to function as the new "Open File"

   TrackHolders newTracks;
   wxString errorMessage = wxEmptyString;

   {
      // Backup Tags, before the import.  Be prepared to roll back changes.
      auto cleanup = valueRestorer( mTags,
                                   mTags ? mTags->Duplicate() : decltype(mTags){} );

      bool success = Importer::Get().Import(fileName,
                                            GetTrackFactory(),
                                            newTracks,
                                            mTags.get(),
                                            errorMessage);

      if (!errorMessage.IsEmpty()) {
         // Error message derived from Importer::Import
         // Additional help via a Help button links to the manual.
         ShowErrorDialog(this, _("Error Importing"),
                         errorMessage, wxT("Importing_Audio"));
      }
      if (!success)
         return false;

      wxGetApp().AddFileToHistory(fileName);

      // no more errors, commit
      cleanup.release();
   }

   // for LOF ("list of files") files, do not import the file as if it
   // were an audio file itself
   if (fileName.AfterLast('.').IsSameAs(wxT("lof"), false)) {
      // PRL: don't redundantly do the steps below, because we already
      // did it in case of LOF, because of some weird recursion back to this
      // same function.  I think this should be untangled.

      // So Undo history push is not bypassed, despite appearances.
      return false;
   }

   // PRL: Undo history is incremented inside this:
   auto newSharedTracks = AddImportedTracks(fileName, std::move(newTracks));

   if (pTrackArray) {
      for (const auto &newTrack : newSharedTracks) {
         if (newTrack->GetKind() == Track::Wave)
            pTrackArray->push_back(
               std::static_pointer_cast<WaveTrack>(newTrack));
      }
   }

   int mode = gPrefs->Read(wxT("/AudioFiles/NormalizeOnLoad"), 0L);
   if (mode == 1) {
      //TODO: All we want is a SelectAll()
      SelectNone();
      SelectAllIfNone();
      const CommandContext context( *this);
      DoEffect(EffectManager::Get().GetEffectByIdentifier(wxT("Normalize")),
         context,
         OnEffectFlags::kConfigured);
   }

   // This is a no-fail:
   GetDirManager()->FillBlockfilesCache();
   return true;
}

bool AudacityProject::SaveAs(const wxString & newFileName, bool bWantSaveCompressed /*= false*/, bool addToHistory /*= true*/)
{
   // This version of SaveAs is invoked only from scripting and does not
   // prompt for a file name
   wxString oldFileName = mFileName;

   bool bOwnsNewAupName = mbLoadedFromAup && (mFileName==newFileName);
   //check to see if the NEW project file already exists.
   //We should only overwrite it if this project already has the same name, where the user
   //simply chose to use the save as command although the save command would have the effect.
   if( !bOwnsNewAupName && wxFileExists(newFileName)) {
      AudacityMessageDialog m(
         NULL,
         _("The project was not saved because the file name provided would overwrite another project.\nPlease try again and select an original name."),
         _("Error Saving Project"),
         wxOK|wxICON_ERROR);
      m.ShowModal();
      return false;
   }

   mFileName = newFileName;
   bool success = false;
   auto cleanup = finally( [&] {
      if (!success || bWantSaveCompressed)
         // Restore file name on error
         mFileName = oldFileName;
   } );

   //Don't change the title, unless we succeed.
   //SetProjectTitle();

   success = DoSave(!bOwnsNewAupName || bWantSaveCompressed, bWantSaveCompressed);

   if (success && addToHistory) {
      wxGetApp().AddFileToHistory(mFileName);
   }
   if (!success || bWantSaveCompressed) // bWantSaveCompressed doesn't actually change current project.
   {
   }
   else {
      mbLoadedFromAup = true;
      SetProjectTitle();
   }

   return(success);
}


bool AudacityProject::SaveAs(bool bWantSaveCompressed /*= false*/)
{
   return true;
}

//
// Undo/History methods
//

void AudacityProject::InitialState()
{
   GetUndoManager()->ClearStates();

   GetUndoManager()->PushState(GetTracks(), mViewInfo.selectedRegion, mTags,
                          _("Created new project"), wxT(""));

   GetUndoManager()->StateSaved();

   if (mHistoryWindow)
      mHistoryWindow->UpdateDisplay();

   ModifyUndoMenuItems();

   UpdateMenus();
}

bool AudacityProject::UndoAvailable()
{
   TrackList* trackList = GetTracks();
   return GetUndoManager()->UndoAvailable() &&
       !(trackList != nullptr && trackList->HasPendingTracks());
}

bool AudacityProject::RedoAvailable()
{
   TrackList* trackList = GetTracks();
   return GetUndoManager()->RedoAvailable() &&
       !(trackList != nullptr && trackList->HasPendingTracks());
}

void AudacityProject::PushState(const wxString &desc, const wxString &shortDesc)
{
   PushState(desc, shortDesc, UndoPush::AUTOSAVE);
}

void AudacityProject::PushState(const wxString &desc,
                                const wxString &shortDesc,
                                UndoPush flags )
{
   GetUndoManager()->PushState(GetTracks(), mViewInfo.selectedRegion, mTags,
                          desc, shortDesc, flags);

   mDirty = true;

   if (mHistoryWindow)
      mHistoryWindow->UpdateDisplay();

   ModifyUndoMenuItems();

   UpdateMenus();

   if (GetTracksFitVerticallyZoomed())
      this->DoZoomFitV();
   if((flags & UndoPush::AUTOSAVE) != UndoPush::MINIMAL)
      AutoSave();

   GetTrackPanel()->HandleCursorForPresentMouseState();
}

void AudacityProject::RollbackState()
{
}

void AudacityProject::ModifyState()
{
   GetUndoManager()->ModifyState(GetTracks(), mViewInfo.selectedRegion, mTags);
   GetTrackPanel()->HandleCursorForPresentMouseState();
}

// LL:  Is there a memory leak here as "l" and "t" are not deleted???
// Vaughan, 2010-08-29: No, as "l" is a TrackList* of an Undo stack state.
//    Need to keep it and its tracks "t" available for Undo/Redo/SetStateTo.
void AudacityProject::PopState(const UndoState &state)
{
   // Restore tags
   mTags = state.tags;

   TrackList *const tracks = state.tracks.get();

   mTracks->Clear();
   TrackListIterator iter(tracks);
   Track *t = iter.First();
   bool odUsed = false;
   movable_ptr<ODComputeSummaryTask> computeTask;

   while (t)
   {
      auto copyTrack = mTracks->Add(t->Duplicate());

      //add the track to OD if the manager exists.  later we might do a more rigorous check...
      if (copyTrack->GetKind() == Track::Wave)
      {
         //if the ODManager hasn't been initialized, there's no chance this track has OD blocks since this
         //is a "Redo" operation.
         //TODO: update this to look like the update loop in OpenFile that handles general purpose ODTasks.
         //BUT, it is too slow to go thru every blockfile and check the odtype, so maybe put a flag in wavetrack
         //that gets unset on OD Completion, (and we could also update the drawing there too.)  The hard part is that
         //we would need to watch every possible way a OD Blockfile could get inserted into a wavetrack and change the
         //flag there.
         if(ODManager::IsInstanceCreated())
         {
            if(!odUsed)
            {
               computeTask = make_movable<ODComputeSummaryTask>();
               odUsed=true;
            }
            // PRL:  Is it correct to add all tracks to one task, even if they
            // are not partnered channels?  Rather than
            // make one task for each?
            computeTask->AddWaveTrack((WaveTrack*)copyTrack);
         }
      }
      t = iter.Next();
   }

   //add the task.
   if(odUsed)
      ODManager::Instance()->AddNewTask(std::move(computeTask));

   HandleResize();

   UpdateMenus();

   AutoSave();
}

//
// Clipboard methods
//

//static
TrackList *AudacityProject::GetClipboardTracks()
{
   return msClipboard.get();
}

//static
void AudacityProject::DeleteClipboard()
{
   msClipboard.reset();
}

void AudacityProject::ClearClipboard()
{
   msClipT0 = 0.0;
   msClipT1 = 0.0;
   msClipProject = NULL;
   if (msClipboard) {
      msClipboard->Clear();
   }
}

void AudacityProject::Clear()
{
   TrackListIterator iter(GetTracks());

   Track *n = iter.First();

   while (n) {
      if (n->GetSelected() || n->IsSyncLockSelected()) {
         n->Clear(mViewInfo.selectedRegion.t0(), mViewInfo.selectedRegion.t1());
      }
      n = iter.Next();
   }

   double seconds = mViewInfo.selectedRegion.duration();

   mViewInfo.selectedRegion.collapseToT0();

   PushState(wxString::Format(_("Deleted %.2f seconds at t=%.2f"),
                              seconds,
                              mViewInfo.selectedRegion.t0()),
             _("Delete"));

   RedrawProject();
}

// Utility function called by other zoom methods
void AudacityProject::Zoom(double level)
{
   mViewInfo.SetZoom(level);
   FixScrollbars();
   // See if we can center the selection on screen, and have it actually fit.
   // tOnLeft is the amount of time we would need before the selection left edge to center it.
   float t0 = mViewInfo.selectedRegion.t0();
   float t1 = mViewInfo.selectedRegion.t1();
   float tAvailable = GetScreenEndTime() - mViewInfo.h;
   float tOnLeft = (tAvailable - t0 + t1)/2.0;
   // Bug 1292 (Enh) is effectively a request to do this scrolling of  the selection into view.
   // If tOnLeft is positive, then we have room for the selection, so scroll to it.
   if( tOnLeft >=0 )
      TP_ScrollWindow( t0-tOnLeft);
}

// Utility function called by other zoom methods
void AudacityProject::ZoomBy(double multiplier)
{
   mViewInfo.ZoomBy(multiplier);
   FixScrollbars();
}

///////////////////////////////////////////////////////////////////
// This method 'rewinds' the track, by setting the cursor to 0 and
// scrolling the window to fit 0 on the left side of it
// (maintaining  current zoom).
// If shift is held down, it will extend the left edge of the
// selection to 0 (holding right edge constant), otherwise it will
// move both left and right edge of selection to 0
///////////////////////////////////////////////////////////////////
void AudacityProject::Rewind(bool shift)
{
   mViewInfo.selectedRegion.setT0(0, false);
   if (!shift)
      mViewInfo.selectedRegion.setT1(0);

   TP_ScrollWindow(0);
}


///////////////////////////////////////////////////////////////////
// This method 'fast-forwards' the track, by setting the cursor to
// the end of the samples on the selected track and  scrolling the
//  window to fit the end on its right side (maintaining  current zoom).
// If shift is held down, it will extend the right edge of the
// selection to the end (holding left edge constant), otherwise it will
// move both left and right edge of selection to the end
///////////////////////////////////////////////////////////////////
void AudacityProject::SkipEnd(bool shift)
{
   double len = mTracks->GetEndTime();

   mViewInfo.selectedRegion.setT1(len, false);
   if (!shift)
      mViewInfo.selectedRegion.setT0(len);

   // Make sure the end of the track is visible
   mTrackPanel->ScrollIntoView(len);
   mTrackPanel->Refresh(false);
}


////////////////////////////////////////////////////////////
//  This fetches a pointer to the Transport Toolbar.  It may
//  either be docked or floating out in the open.
////////////////////////////////////////////////////////////
ControlToolBar *AudacityProject::GetControlToolBar()
{
   return (ControlToolBar *)
          (mToolManager ?
           mToolManager->GetToolBar(TransportBarID) :
           NULL);
}

ToolsToolBar * AudacityProject::TP_GetToolsToolBar()
{
   return GetToolsToolBar();
}

DeviceToolBar *AudacityProject::GetDeviceToolBar()
{
   return (DeviceToolBar *)
          (mToolManager ?
           mToolManager->GetToolBar(DeviceBarID) :
           NULL);
}

EditToolBar *AudacityProject::GetEditToolBar()
{
   return (EditToolBar *)
          (mToolManager ?
           mToolManager->GetToolBar(EditBarID) :
           NULL);
}

MixerToolBar *AudacityProject::GetMixerToolBar()
{
   return (MixerToolBar *)
          (mToolManager ?
           mToolManager->GetToolBar(MixerBarID) :
           NULL);
}

SelectionBar *AudacityProject::GetSelectionBar()
{
   return (SelectionBar *)
      (mToolManager ?
      mToolManager->GetToolBar(SelectionBarID) :
      NULL);
}

ToolsToolBar *AudacityProject::GetToolsToolBar()
{
   return (ToolsToolBar *)
          (mToolManager ?
           mToolManager->GetToolBar(ToolsBarID) :
           NULL);
}

const ToolsToolBar *AudacityProject::GetToolsToolBar() const
{
   return (ToolsToolBar *)
      (mToolManager ?
      mToolManager->GetToolBar(ToolsBarID) :
      NULL);
}

MeterPanel *AudacityProject::GetPlaybackMeter()
{
   return mPlaybackMeter;
}

void AudacityProject::SetPlaybackMeter(MeterPanel *playback)
{
   mPlaybackMeter = playback;
   if (gAudioIO)
   {
      gAudioIO->SetPlaybackMeter(this, mPlaybackMeter);
   }
}

MeterPanel *AudacityProject::GetCaptureMeter()
{
   return mCaptureMeter;
}

void AudacityProject::SetCaptureMeter(MeterPanel *capture)
{
   mCaptureMeter = capture;

   if (gAudioIO)
   {
      gAudioIO->SetCaptureMeter(this, mCaptureMeter);
   }
}

void AudacityProject::OnTimer(wxTimerEvent& WXUNUSED(event))
{
   MixerToolBar *mixerToolBar = GetMixerToolBar();
   if( mixerToolBar )
      mixerToolBar->UpdateControls();

   if (::wxGetUTCTime() - mLastStatusUpdateTime < 3)
      return;

   // gAudioIO->GetNumCaptureChannels() should only be positive
   // when we are recording.
   if (GetAudioIOToken() > 0 && gAudioIO->GetNumCaptureChannels() > 0) {
      wxLongLong freeSpace = mDirManager->GetFreeDiskSpace();
      if (freeSpace >= 0) {
         wxString sMessage;

         int iRecordingMins = GetEstimatedRecordingMinsLeftOnDisk(gAudioIO->GetNumCaptureChannels());
         sMessage.Printf(_("Disk space remaining for recording: %s"), GetHoursMinsString(iRecordingMins));

         mStatusBar->SetStatusText(sMessage, mainStatusBarField);
      }
   }
   else if(ODManager::IsInstanceCreated())
   {
      //if we have some tasks running, we should say something about it.
      int numTasks = ODManager::Instance()->GetTotalNumTasks();
      if(numTasks)
      {
         wxString msg;
         float ratioComplete= ODManager::Instance()->GetOverallPercentComplete();

         if(ratioComplete>=1.0f)
         {
            //if we are 100 percent complete and there is still a task in the queue, we should wake the ODManager
            //so it can clear it.
            //signal the od task queue loop to wake up so it can remove the tasks from the queue and the queue if it is empty.
            ODManager::Instance()->SignalTaskQueueLoop();


            msg = _("On-demand import and waveform calculation complete.");
            mStatusBar->SetStatusText(msg, mainStatusBarField);

         }
         else if(numTasks>1)
            msg.Printf(_("Import(s) complete. Running %d on-demand waveform calculations. Overall %2.0f%% complete."),
              numTasks,ratioComplete*100.0);
         else
            msg.Printf(_("Import complete. Running an on-demand waveform calculation. %2.0f%% complete."),
             ratioComplete*100.0);


         mStatusBar->SetStatusText(msg, mainStatusBarField);
      }
   }
}

// TrackPanel callback method
void AudacityProject::TP_DisplayStatusMessage(const wxString &msg)
{
   // Bug1756:  If recording, let the status message for remaining disk space
   // prevail
   if (GetAudioIOToken() > 0 && gAudioIO->GetNumCaptureChannels() > 0)
      return;

   mStatusBar->SetStatusText(msg, mainStatusBarField);
   mLastStatusUpdateTime = ::wxGetUTCTime();
}

void AudacityProject::TP_DisplaySelection()
{
   double audioTime;

   if (mRuler) {
      if (!gAudioIO->IsBusy() && !mLockPlayRegion)
         mRuler->SetPlayRegion(mViewInfo.selectedRegion.t0(),
         mViewInfo.selectedRegion.t1());
      else
         // Cause ruler redraw anyway, because we may be zooming or scrolling
         mRuler->Refresh();
   }

   if (gAudioIO->IsBusy())
      audioTime = gAudioIO->GetStreamTime();
   else {
      double playEnd;
      GetPlayRegion(&audioTime, &playEnd);
   }

   GetSelectionBar()->SetTimes(mViewInfo.selectedRegion.t0(),
                               mViewInfo.selectedRegion.t1(), audioTime);
}


// TrackPanel access

wxSize AudacityProject::GetTPTracksUsableArea()
{
   wxSize s;
   mTrackPanel->GetTracksUsableArea(&s.x, &s.y);
   return s;
}

void AudacityProject::RefreshTPTrack(Track* pTrk, bool refreshbacking /*= true*/)
{
   mTrackPanel->RefreshTrack(pTrk, refreshbacking);
}


// TrackPanel callback method
void AudacityProject::TP_PushState(const wxString &desc, const wxString &shortDesc,
                                   UndoPush flags)
{
   PushState(desc, shortDesc, flags);
}

// TrackPanel callback method
void AudacityProject::TP_ModifyState()
{
   ModifyState();
}

// TrackPanel callback method
void AudacityProject::TP_ScrollLeft()
{
   OnScrollLeft();
}

// TrackPanel callback method
void AudacityProject::TP_ScrollRight()
{
   OnScrollRight();
}

// TrackPanel callback method
void AudacityProject::TP_RedrawScrollbars()
{
   FixScrollbars();
}

void AudacityProject::TP_HandleResize()
{
   HandleResize();
}

void AudacityProject::GetPlayRegion(double* playRegionStart,
                                    double *playRegionEnd)
{
   if (mRuler)
      mRuler->GetPlayRegion(playRegionStart, playRegionEnd);
   else
      *playRegionEnd = *playRegionStart = 0;
}

void AudacityProject::AutoSave()
{
}

void AudacityProject::OnAudioIORate(int rate)
{
   wxString display;
   if (rate > 0) {
      display = wxString::Format(_("Actual Rate: %d"), rate);
   }
   else
      // clear the status field
      ;

   int x, y;
   mStatusBar->GetTextExtent(display, &x, &y);
   int widths[] = {0, GetControlToolBar()->WidthForStatusBar(mStatusBar), -1, x+50};
   mStatusBar->SetStatusWidths(4, widths);
   mStatusBar->SetStatusText(display, rateStatusBarField);
}

void AudacityProject::OnAudioIOStartRecording()
{
   // Before recording is started, auto-save the file. The file will have
   // empty tracks at the bottom where the recording will be put into
   AutoSave();
}

// This is called after recording has stopped and all tracks have flushed.
void AudacityProject::OnAudioIOStopRecording()
{
   // Only push state if we were capturing and not monitoring
   if (GetAudioIOToken() > 0)
   {
      auto &intervals = gAudioIO->LostCaptureIntervals();

      // Add to history
      PushState(_("Recorded Audio"), _("Record"));

      // Reset timer record 
      if (IsTimerRecordCancelled())
      {
         OnUndo(*this);
         ResetTimerRecordFlag();
      }

      // Refresh the project window
      FixScrollbars();
      RedrawProject();
   }

   // Write all cached files to disk, if any
   mDirManager->WriteCacheToDisk();

   // Now we auto-save again to get the project to a "normal" state again.
   AutoSave();
}

void AudacityProject::SetSnapTo(int snap)
{
   if (GetSelectionBar()) {
      GetSelectionBar()->SetSnapTo(snap);
   }
}

int AudacityProject::GetSnapTo() const
{
   return mSnapTo;
}

bool AudacityProject::IsSyncLocked()
{
#ifdef EXPERIMENTAL_SYNC_LOCK
   return mIsSyncLocked;
#else
   return false;
#endif
}

void AudacityProject::SetSyncLock(bool flag)
{
   if (flag != mIsSyncLocked) {
      mIsSyncLocked = flag;
      if (GetTrackPanel())
         GetTrackPanel()->Refresh(false);
   }
}

void AudacityProject::SetTrackGain(WaveTrack * wt, LWSlider * slider)
{
   wxASSERT(wt);
   float newValue = slider->Get();

   // Assume linked track is wave or null
   const auto link = static_cast<WaveTrack*>(wt->GetLink());
   wt->SetGain(newValue);
   if (link)
      link->SetGain(newValue);

   PushState(_("Adjusted gain"), _("Gain"), UndoPush::CONSOLIDATE);

   GetTrackPanel()->RefreshTrack(wt);
}

void AudacityProject::SetTrackPan(WaveTrack * wt, LWSlider * slider)
{
   wxASSERT(wt);
   float newValue = slider->Get();

   // Assume linked track is wave or null
   const auto link = static_cast<WaveTrack*>(wt->GetLink());
   wt->SetPan(newValue);
   if (link)
      link->SetPan(newValue);

   PushState(_("Adjusted Pan"), _("Pan"), UndoPush::CONSOLIDATE);

   GetTrackPanel()->RefreshTrack(wt);
}

/// Removes the specified track.  Called from HandleClosing.
void AudacityProject::RemoveTrack(Track * toRemove)
{
   // If it was focused, then NEW focus is the next or, if
   // unavailable, the previous track. (The NEW focus is set
   // after the track has been removed.)
   bool toRemoveWasFocused = mTrackPanel->GetFocusedTrack() == toRemove;
   Track* newFocus{};
   if (toRemoveWasFocused) {
      newFocus = mTracks->GetNext(toRemove, true);
      if (!newFocus) {
         newFocus = mTracks->GetPrev(toRemove, true);
      }
   }

   wxString name = toRemove->GetName();
   Track *partner = toRemove->GetLink();

   mTracks->Remove(toRemove);
   if (partner) {
      mTracks->Remove(partner);
   }
   
   if (toRemoveWasFocused) {
      mTrackPanel->SetFocusedTrack(newFocus);
   }

   PushState(
      wxString::Format(_("Removed track '%s.'"),
      name),
      _("Track Remove"));

   TP_RedrawScrollbars();
   HandleResize();
   GetTrackPanel()->Refresh(false);
}

// Keyboard capture

// static
bool AudacityProject::HasKeyboardCapture(const wxWindow *handler)
{
   return GetKeyboardCaptureHandler() == handler;
}

// static
wxWindow *AudacityProject::GetKeyboardCaptureHandler()
{
   AudacityProject *project = GetActiveProject();
   if (project)
   {
      return project->mKeyboardCaptureHandler;
   }

   return NULL;
}

// static
void AudacityProject::CaptureKeyboard(wxWindow *handler)
{
   AudacityProject *project = GetActiveProject();
   if (project)
   {
//      wxASSERT(project->mKeyboardCaptureHandler == NULL);
      project->mKeyboardCaptureHandler = handler;
   }
}

// static
void AudacityProject::ReleaseKeyboard(wxWindow * /* handler */)
{
   AudacityProject *project = GetActiveProject();
   if (project)
   {
//      wxASSERT(project->mKeyboardCaptureHandler == handler);
      project->mKeyboardCaptureHandler = NULL;
   }

   return;
}

bool AudacityProject::ExportFromTimerRecording(wxFileName fnFile, int iFormat, int iSubFormat, int iFilterIndex)
{
   Exporter e;

   wxGetApp().SetMissingAliasedFileWarningShouldShow(true);
   return e.ProcessFromTimerRecording(this, false, 0.0, mTracks->GetEndTime(), fnFile, iFormat, iSubFormat, iFilterIndex);
}

int AudacityProject::GetOpenProjectCount() {
   return gAudacityProjects.size();
}

bool AudacityProject::IsProjectSaved() {
   // This is true if a project was opened from an .aup
   // Otherwise it becomes true only when a project is first saved successfully
   // in DirManager::SetProject
   wxString sProjectName = mDirManager->GetProjectName();
   return (sProjectName != wxT(""));
}

bool AudacityProject::SaveFromTimerRecording(wxFileName fnFile) {
   // MY: Will save the project to a NEW location a-la Save As
   // and then tidy up after itself.

   wxString sNewFileName = fnFile.GetFullPath();

   // MY: To allow SaveAs from Timer Recording we need to check what
   // the value of mFileName is before we change it.
   wxString sOldFilename = "";
   if (IsProjectSaved()) {
      sOldFilename = mFileName;
   }

   // MY: If the project file already exists then bail out
   // and send populate the message string (pointer) so
   // we can tell the user what went wrong.
   if (wxFileExists(sNewFileName)) {
      return false;
   }

   mFileName = sNewFileName;
   bool bSuccess = false;
   auto cleanup = finally( [&] {
      if (!bSuccess)
         // Restore file name on error
         mFileName = sOldFilename;
   } );

   bSuccess = DoSave(true, false);

   if (bSuccess) {
      wxGetApp().AddFileToHistory(mFileName);
      mbLoadedFromAup = true;
      SetProjectTitle();
   }

   return bSuccess;
}

// Does the project have any tracks?
bool AudacityProject::ProjectHasTracks() {
   // These two lines test for an 'empty' project.
   // of course it could still have a history at this stage.
   TrackListIterator iter2(GetTracks());
   bool bHasTracks = (iter2.First() != NULL);
   return bHasTracks;
}

wxString AudacityProject::GetHoursMinsString(int iMinutes)
{

   wxString sFormatted = wxEmptyString;

   if (iMinutes < 1) {
      // Less than a minute...
      sFormatted = _("Less than 1 minute");
      return sFormatted;
   }

   // Calculate
   int iHours = iMinutes / 60;
   int iMins = iMinutes % 60;

   auto sHours =
      wxString::Format( wxPLURAL("%d hour", "%d hours", iHours), iHours );
   auto sMins =
      wxString::Format( wxPLURAL("%d minute", "%d minutes", iMins), iMins );

   /* i18n-hint: A time in hours and minutes. Only translate the "and". */
   sFormatted.Printf( _("%s and %s."), sHours, sMins);
   return sFormatted;
}

// This routine will give an estimate of how many
// minutes of recording time we have available.
// The calculations made are based on the user's current
// preferences.
int AudacityProject::GetEstimatedRecordingMinsLeftOnDisk(long lCaptureChannels) {

   // Obtain the current settings
   sampleFormat oCaptureFormat = (sampleFormat)
      gPrefs->Read(wxT("/SamplingRate/DefaultProjectSampleFormat"), floatSample);
   if (lCaptureChannels == 0) {
      gPrefs->Read(wxT("/AudioIO/RecordChannels"), &lCaptureChannels, 2L);
   }

   // Find out how much free space we have on disk
   wxLongLong lFreeSpace = mDirManager->GetFreeDiskSpace();
   if (lFreeSpace < 0) {
      return 0;
   }

   // Calculate the remaining time
   double dRecTime = 0.0;
   double bytesOnDiskPerSample = SAMPLE_SIZE_DISK(oCaptureFormat);
   dRecTime = lFreeSpace.GetHi() * 4294967296.0 + lFreeSpace.GetLo();
   dRecTime /= bytesOnDiskPerSample;   
   dRecTime /= lCaptureChannels;
   dRecTime /= GetRate();

   // Convert to minutes before returning
   int iRecMins = (int)round(dRecTime / 60.0);
   return iRecMins;
}


double AudacityProject::GetZoomOfToFit(){
   const double end = mTracks->GetEndTime();
   const double start = mViewInfo.bScrollBeyondZero
      ? std::min(mTracks->GetStartTime(), 0.0)
      : 0;
   const double len = end - start;

   if (len <= 0.0)
      return mViewInfo.GetZoom();

   int w;
   mTrackPanel->GetTracksUsableArea(&w, NULL);
   w -= 10;
   return w/len;
}

double AudacityProject::GetZoomOfSelection(){
   const double lowerBound =
      std::max(mViewInfo.selectedRegion.t0(), ScrollingLowerBoundTime());
   const double denom =
      mViewInfo.selectedRegion.t1() - lowerBound;
   if (denom <= 0.0)
      return mViewInfo.GetZoom();

   // LL:  The "-1" is just a hack to get around an issue where zooming to
   //      selection doesn't actually get the entire selected region within the
   //      visible area.  This causes a problem with scrolling at end of playback
   //      where the selected region may be scrolled off the left of the screen.
   //      I know this isn't right, but until the real rounding or 1-off issue is
   //      found, this will have to work.
   // PRL:  Did I fix this?  I am not sure, so I leave the hack in place.
   //      Fixes might have resulted from commits
   //      1b8f44d0537d987c59653b11ed75a842b48896ea and
   //      e7c7bb84a966c3b3cc4b3a9717d5f247f25e7296
   int width;
   mTrackPanel->GetTracksUsableArea(&width, NULL);
   return (width - 1) / denom;
}

double AudacityProject::GetZoomOfPreset( int preset ){

   // Sets a limit on how far we will zoom out as a factor over zoom to fit.
   const double maxZoomOutFactor = 4.0;
   // Sets how many pixels we allow for one uint, such as seconds.
   const double pixelsPerUnit = 5.0;

   double result = 1.0;
   double zoomToFit = GetZoomOfToFit();
   switch( preset ){
      default:
      case WaveTrack::kZoomDefault:
         result = ZoomInfo::GetDefaultZoom();
         break;
      case WaveTrack::kZoomToFit:
         result = zoomToFit;
         break;
      case WaveTrack::kZoomToSelection:
         result = GetZoomOfSelection();
         break;
      case WaveTrack::kZoomMinutes:
         result = pixelsPerUnit * 1.0/60;
         break;
      case WaveTrack::kZoomSeconds:
         result = pixelsPerUnit * 1.0;
         break;
      case WaveTrack::kZoom5ths:
         result = pixelsPerUnit * 5.0;
         break;
      case WaveTrack::kZoom10ths:
         result = pixelsPerUnit * 10.0;
         break;
      case WaveTrack::kZoom20ths:
         result = pixelsPerUnit * 20.0;
         break;
      case WaveTrack::kZoom50ths:
         result = pixelsPerUnit * 50.0;
         break;
      case WaveTrack::kZoom100ths:
         result = pixelsPerUnit * 100.0;
         break;
      case WaveTrack::kZoom500ths:
         result = pixelsPerUnit * 500.0;
         break;
      case WaveTrack::kZoomMilliSeconds:
         result = pixelsPerUnit * 1000.0;
         break;
      case WaveTrack::kZoomSamples:
         result = 44100.0;
         break;
      case WaveTrack::kZoom4To1:
         result = 44100.0 * 4;
         break;
      case WaveTrack::kMaxZoom:
         result = ZoomInfo::GetMaxZoom();
         break;
   };
   if( result < (zoomToFit/maxZoomOutFactor) )
      result = zoomToFit / maxZoomOutFactor;
   return result;
}

double AudacityProject::GetZoomOfPref( const wxString & PresetPrefName, int defaultPreset ){
   int preset=defaultPreset;
   gPrefs->Read( PresetPrefName, &preset, defaultPreset );
   return GetZoomOfPreset( preset );
}

AudacityProject::PlaybackScroller::PlaybackScroller(AudacityProject *project)
: mProject(project)
{
   mProject->Bind(EVT_TRACK_PANEL_TIMER,
                     &PlaybackScroller::OnTimer,
                     this);
}

void AudacityProject::PlaybackScroller::OnTimer(wxCommandEvent &event)
{
   // Let other listeners get the notification
   event.Skip();

   if(!mProject->IsAudioActive())
      return;
   else if (mMode == Mode::Refresh) {
      // PRL:  see comments in Scrubbing.cpp for why this is sometimes needed.
      // These unnecessary refreshes cause wheel rotation events to be delivered more uniformly
      // to the application, so scrub speed control is smoother.
      // (So I see at least with OS 10.10 and wxWidgets 3.0.2.)
      // Is there another way to ensure that than by refreshing?
      const auto trackPanel = mProject->GetTrackPanel();
      trackPanel->Refresh(false);
   }
   else if (mMode != Mode::Off) {
      // Pan the view, so that we center the play indicator.

      ViewInfo &viewInfo = mProject->GetViewInfo();
      TrackPanel *const trackPanel = mProject->GetTrackPanel();
      const int posX = viewInfo.TimeToPosition(viewInfo.mRecentStreamTime);
      int width;
      trackPanel->GetTracksUsableArea(&width, NULL);
      int deltaX;
      switch (mMode)
      {
         default:
            wxASSERT(false);
            /* fallthru */
         case Mode::Centered:
            deltaX = posX - width / 2;    break;
         case Mode::Right:
            deltaX = posX - width;        break;
      }
      viewInfo.h =
         viewInfo.OffsetTimeByPixels(viewInfo.h, deltaX, true);
      if (!mProject->MayScrollBeyondZero())
         // Can't scroll too far left
         viewInfo.h = std::max(0.0, viewInfo.h);
      trackPanel->Refresh(false);
   }
}
