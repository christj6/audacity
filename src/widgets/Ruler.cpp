/**********************************************************************

  Audacity: A Digital Audio Editor

  Ruler.cpp

  Dominic Mazzoni

*******************************************************************//**

\class Ruler
\brief Used to display a Ruler.

  This is a generic class which can be used to display just about
  any kind of ruler.

  At a minimum, the user must specify the dimensions of the
  ruler, its orientation (horizontal or vertical), and the
  values displayed at the two ends of the ruler (min and max).
  By default, this class will display tick marks at reasonable
  round numbers and fractions, for example, 100, 50, 10, 5, 1,
  0.5, 0.1, etc.

  The class is designed to display a small handful of
  labeled Major ticks, and a few Minor ticks between each of
  these.  Minor ticks are labeled if there is enough space.
  Labels will never run into each other.

  In addition to Real numbers, the Ruler currently supports
  two other formats for its display:

  Integer - never shows tick marks for fractions of an integer

  Time - Assumes values represent seconds, and labels the tick
         marks in "HH:MM:SS" format, e.g. 4000 seconds becomes
         "1:06:40", for example.  Will display fractions of
         a second, and tick marks are all reasonable round
         numbers for time (i.e. 15 seconds, 30 seconds, etc.)
*//***************************************************************//**

\class RulerPanel
\brief RulerPanel class allows you to work with a Ruler like
  any other wxWindow.

*//***************************************************************//**

\class AdornedRulerPanel
\brief This is an Audacity Specific ruler panel which additionally
  has border, selection markers, play marker.
  
  Once TrackPanel uses wxSizers, we will derive it from some
  wxWindow and the GetSize and SetSize functions
  will then be wxWidgets functions instead.

*//***************************************************************//**

\class Ruler::Label
\brief An array of these created by the Ruler is used to determine
what and where text annotations to the numbers on the Ruler get drawn.

\todo Check whether Ruler is costing too much time in allocation/free of
array of Ruler::Label.

*//******************************************************************/

#include "../Audacity.h"
#include "Ruler.h"

#include <math.h>

#include <wx/app.h>
#include <wx/dcscreen.h>
#include <wx/dcmemory.h>
#include <wx/dcbuffer.h>
#include <wx/settings.h>
#include <wx/menu.h>
#include <wx/menuitem.h>
#include <wx/tooltip.h>

#include "AButton.h"
#include "../AColor.h"
#include "../AudioIO.h"
#include "../Internat.h"
#include "../Project.h"
#include "../toolbars/ControlToolBar.h"
#include "../Theme.h"
#include "../AllThemeResources.h"
#include "../Experimental.h"
#include "../TrackPanel.h"
#include "../TrackPanelCellIterator.h"
#include "../NumberScale.h"
#include "../Prefs.h"
#include "../Snap.h"
#include "../prefs/PlaybackPrefs.h"
#include "../prefs/TracksPrefs.h"
#include "../prefs/TracksBehaviorsPrefs.h"
#include "../widgets/Grabber.h"
#include "../commands/CommandContext.h"

using std::min;
using std::max;

#define SELECT_TOLERANCE_PIXEL 4

#define PLAY_REGION_TRIANGLE_SIZE 6
#define PLAY_REGION_RECT_WIDTH 1
#define PLAY_REGION_RECT_HEIGHT 3
#define PLAY_REGION_GLOBAL_OFFSET_Y 7

//wxColour Ruler::mTickColour{ 153, 153, 153 };

//
// Ruler
//

Ruler::Ruler()
   : mpNumberScale{}
{
   mMin = mHiddenMin = 0.0;
   mMax = mHiddenMax = 100.0;
   mOrientation = wxHORIZONTAL;
   mSpacing = 6;
   mHasSetSpacing = false;
   mFormat = RealFormat;
   mFlip = false;
   mLog = false;
   mLabelEdges = false;
   mUnits = wxT("");

   mLeft = -1;
   mTop = -1;
   mRight = -1;
   mBottom = -1;
   mbTicksOnly = true;
   mbTicksAtExtremes = false;
   mTickColour = wxColour( theTheme.Colour( clrTrackPanelText ));
   mPen.SetColour(mTickColour);

   // Note: the font size is now adjusted automatically whenever
   // Invalidate is called on a horizontal Ruler, unless the user
   // calls SetFonts manually.  So the defaults here are not used
   // often.

   int fontSize = 10;
#ifdef __WXMSW__
   fontSize = 8;
#endif

   mMinorMinorFont = std::make_unique<wxFont>(fontSize - 1, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL);
   mMinorFont = std::make_unique<wxFont>(fontSize, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL);
   mMajorFont = std::make_unique<wxFont>(fontSize, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD);

   mUserFonts = false;

   mLengthOld = 0;
   mLength = 0;
   mUserBitLen = 0;

   mValid = false;

   mCustom = false;
   mbMinor = true;

   mGridLineLength = 0;
   mMajorGrid = false;
   mMinorGrid = false;

   mTwoTone = false;

   mUseZoomInfo = NULL;
}

Ruler::~Ruler()
{
   Invalidate();  // frees up our arrays
}

void Ruler::SetFormat(RulerFormat format)
{
   // IntFormat, RealFormat, RealLogFormat, TimeFormat, or LinearDBFormat

   if (mFormat != format) {
      mFormat = format;

      Invalidate();
   }
}

void Ruler::SetLog(bool log)
{
   // Logarithmic

   if (mLog != log) {
      mLog = log;

      Invalidate();
   }
}

void Ruler::SetRange(double min, double max)
{
   SetRange(min, max, min, max);
}

void Ruler::SetRange
   (double min, double max, double hiddenMin, double hiddenMax)
{
   // For a horizontal ruler,
   // min is the value in the center of pixel "left",
   // max is the value in the center of pixel "right".

   // In the special case of a time ruler,
   // hiddenMin and hiddenMax are values that would be shown with the fisheye
   // turned off.  In other cases they equal min and max respectively.

   if (mMin != min || mMax != max ||
      mHiddenMin != hiddenMin || mHiddenMax != hiddenMax) {
      mMin = min;
      mMax = max;
      mHiddenMin = hiddenMin;
      mHiddenMax = hiddenMax;

      Invalidate();
   }
}

void Ruler::SetSpacing(int spacing)
{
   mHasSetSpacing = true;

   if (mSpacing != spacing) {
      mSpacing = spacing;

      Invalidate();
   }
}

void Ruler::SetLabelEdges(bool labelEdges)
{
   // If this is true, the edges of the ruler will always
   // receive a label.  If not, the nearest round number is
   // labeled (which may or may not be the edge).

   if (mLabelEdges != labelEdges) {
      mLabelEdges = labelEdges;

      Invalidate();
   }
}

void Ruler::SetFlip(bool flip)
{
   // If this is true, the orientation of the tick marks
   // is reversed from the default; eg. above the line
   // instead of below

   if (mFlip != flip) {
      mFlip = flip;

      Invalidate();
   }
}

void Ruler::SetMinor(bool value)
{
   mbMinor = value;
}

void Ruler::SetFonts(const wxFont &minorFont, const wxFont &majorFont, const wxFont &minorMinorFont)
{
   *mMinorMinorFont = minorMinorFont;
   *mMinorFont = minorFont;
   *mMajorFont = majorFont;

   // Won't override these fonts
   mUserFonts = true;

   Invalidate();
}

void Ruler::SetNumberScale(const NumberScale *pScale)
{
   if (!pScale) {
      if (mpNumberScale) {
         mpNumberScale.reset();
         Invalidate();
      }
   }
   else {
      if (!mpNumberScale || *mpNumberScale != *pScale) {
         mpNumberScale = std::make_unique<NumberScale>(*pScale);
         Invalidate();
      }
   }
}

void Ruler::OfflimitsPixels(int start, int end)
{
   if (!mUserBits) {
      if (mOrientation == wxHORIZONTAL)
         mLength = mRight-mLeft;
      else
         mLength = mBottom-mTop;
      if( mLength < 0 )
         return;
      mUserBits.reinit(static_cast<size_t>(mLength+1), true);
      mUserBitLen  = mLength+1;
   }

   if (end < start)
      std::swap( start, end );

   if (start < 0)
      start = 0;
   if (end > mLength)
      end = mLength;

   for(int i = start; i <= end; i++)
      mUserBits[i] = 1;
}

void Ruler::SetBounds(int left, int top, int right, int bottom)
{
   if (mLeft != left || mTop != top ||
       mRight != right || mBottom != bottom) {
      mLeft = left;
      mTop = top;
      mRight = right;
      mBottom = bottom;

      Invalidate();
   }
}

void Ruler::Invalidate()
{
   mValid = false;

   if (mOrientation == wxHORIZONTAL)
      mLength = mRight-mLeft;
   else
      mLength = mBottom-mTop;

   mBits.reset();
   if (mUserBits && mLength+1 != mUserBitLen) {
      mUserBits.reset();
      mUserBitLen = 0;
   }
}

wxString Ruler::LabelString(double d, bool major)
{
   // Given a value, turn it into a string according
   // to the current ruler format.  The number of digits of
   // accuracy depends on the resolution of the ruler,
   // i.e. how far zoomed in or out you are.

   wxString s;

   // Replace -0 with 0
   if (d < 0.0 && (d+mMinor > 0.0) && ( mFormat != RealLogFormat ))
      d = 0.0;

   switch(mFormat) {
   case IntFormat:
      s.Printf(wxT("%d"), (int)floor(d+0.5));
      break;
   case LinearDBFormat:
      if (mMinor >= 1.0)
         s.Printf(wxT("%d"), (int)floor(d+0.5));
      else {
         int precision = -log10(mMinor);
         s.Printf(wxT("%.*f"), precision, d);
      }
      break;
   case RealFormat:
      if (mMinor >= 1.0)
         s.Printf(wxT("%d"), (int)floor(d+0.5));
      else {
         s.Printf(wxString::Format(wxT("%%.%df"), mDigits), d);
      }
      break;
   case RealLogFormat:
      if (mMinor >= 1.0)
         s.Printf(wxT("%d"), (int)floor(d+0.5));
      else {
         s.Printf(wxString::Format(wxT("%%.%df"), mDigits), d);
      }
      break;
   case TimeFormat:
      if (major) {
         if (d < 0) {
            s = wxT("-");
            d = -d;
         }

         #if ALWAYS_HH_MM_SS
         int secs = (int)(d + 0.5);
         if (mMinor >= 1.0) {
            s.Printf(wxT("%d:%02d:%02d"), secs/3600, (secs/60)%60, secs%60);
         }
         else {
            wxString t1, t2, format;
            t1.Printf(wxT("%d:%02d:"), secs/3600, (secs/60)%60);
            format.Printf(wxT("%%0%d.%dlf"), mDigits+3, mDigits);
            t2.Printf(format, fmod(d, 60.0));
            s += t1 + t2;
         }
         break;
         #endif

         if (mMinor >= 3600.0) {
            int hrs = (int)(d / 3600.0 + 0.5);
            wxString h;
            h.Printf(wxT("%d:00:00"), hrs);
            s += h;
         }
         else if (mMinor >= 60.0) {
            int minutes = (int)(d / 60.0 + 0.5);
            wxString m;
            if (minutes >= 60)
               m.Printf(wxT("%d:%02d:00"), minutes/60, minutes%60);
            else
               m.Printf(wxT("%d:00"), minutes);
            s += m;
         }
         else if (mMinor >= 1.0) {
            int secs = (int)(d + 0.5);
            wxString t;
            if (secs >= 3600)
               t.Printf(wxT("%d:%02d:%02d"), secs/3600, (secs/60)%60, secs%60);
            else if (secs >= 60)
               t.Printf(wxT("%d:%02d"), secs/60, secs%60);
            else
               t.Printf(wxT("%d"), secs);
            s += t;
         }
         else {
            // For d in the range of hours, d is just very slightly below the value it should 
            // have, because of using a double, which in turn yields values like 59:59:999999 
            // mins:secs:nanosecs when we want 1:00:00:000000
            // so adjust by less than a nano second per hour to get nicer number formatting.
            double dd = d * 1.000000000000001;
            int secs = (int)(dd);
            wxString t1, t2, format;

            if (secs >= 3600)
               t1.Printf(wxT("%d:%02d:"), secs/3600, (secs/60)%60);
            else if (secs >= 60)
               t1.Printf(wxT("%d:"), secs/60);

            if (secs >= 60)
               format.Printf(wxT("%%0%d.%dlf"), mDigits+3, mDigits);
            else
               format.Printf(wxT("%%%d.%dlf"), mDigits+3, mDigits);
            // dd will be reduced to just the seconds and fractional part.
            dd = dd - secs + (secs%60);
            // truncate to appropriate number of digits, so that the print formatting 
            // doesn't round up 59.9999999 to 60.
            double multiplier = pow( 10, mDigits);
            dd = ((int)(dd * multiplier))/multiplier;
            t2.Printf(format, dd);
            s += t1 + t2;
         }
      }
      else {
      }
   }

   if (mUnits != wxT(""))
      s = (s + mUnits);

   return s;
}

void Ruler::Tick(int pos, double d, bool major, bool minor)
{
   wxString l;
   wxCoord strW, strH, strD, strL;
   int strPos, strLen, strLeft, strTop;

   // FIXME: We don't draw a tick if of end of our label arrays
   // But we shouldn't have an array of labels.
   if( mNumMinorMinor >= mLength )
      return;
   if( mNumMinor >= mLength )
      return;
   if( mNumMajor >= mLength )
      return;

   Label *label;
   if (major)
      label = &mMajorLabels[mNumMajor++];
   else if (minor)
      label = &mMinorLabels[mNumMinor++];
   else
      label = &mMinorMinorLabels[mNumMinorMinor++];

   label->value = d;
   label->pos = pos;
   label->lx = mLeft - 1000; // don't display
   label->ly = mTop - 1000;  // don't display
   label->text = wxT("");

   mDC->SetFont(major? *mMajorFont: minor? *mMinorFont : *mMinorMinorFont);
   l = LabelString(d, major);
   mDC->GetTextExtent(l, &strW, &strH, &strD, &strL);

   if (mOrientation == wxHORIZONTAL) {
      strLen = strW;
      strPos = pos - strW/2;
      if (strPos < 0)
         strPos = 0;
      if (strPos + strW >= mLength)
         strPos = mLength - strW;
      strLeft = mLeft + strPos;
      if (mFlip) {
         strTop = mTop + 4;
         mMaxHeight = max(mMaxHeight, strH + 4);
      }
      else {
         strTop =-strH-mLead;
         mMaxHeight = max(mMaxHeight, strH + 6);
      }
   }
   else {
      strLen = strH;
      strPos = pos - strH/2;
      if (strPos < 0)
         strPos = 0;
      if (strPos + strH >= mLength)
         strPos = mLength - strH;
      strTop = mTop + strPos;
      if (mFlip) {
         strLeft = mLeft + 5;
         mMaxWidth = max(mMaxWidth, strW + 5);
      }
      else
         strLeft =-strW-6;
   }


   // FIXME: we shouldn't even get here if strPos < 0.
   // Ruler code currently does  not handle very small or
   // negative sized windows (i.e. don't draw) properly.
   if( strPos < 0 )
      return;

   // See if any of the pixels we need to draw this
   // label is already covered

   int i;
   for(i=0; i<strLen; i++)
      if (mBits[strPos+i])
         return;

   // If not, position the label and give it text

   label->lx = strLeft;
   label->ly = strTop;
   label->text = l;

   // And mark these pixels, plus some surrounding
   // ones (the spacing between labels), as covered
   int leftMargin = mSpacing;
   if (strPos < leftMargin)
      leftMargin = strPos;
   strPos -= leftMargin;
   strLen += leftMargin;

   int rightMargin = mSpacing;
   if (strPos + strLen > mLength - mSpacing)
      rightMargin = mLength - strPos - strLen;
   strLen += rightMargin;

   for(i=0; i<strLen; i++)
      mBits[strPos+i] = 1;

   wxRect r(strLeft, strTop, strW, strH);
   mRect.Union(r);

}

void Ruler::TickCustom(int labelIdx, bool major, bool minor)
{
   //This should only used in the mCustom case
   // Many code comes from 'Tick' method: this should
   // be optimized.

   int pos;
   wxString l;
   wxCoord strW, strH, strD, strL;
   int strPos, strLen, strLeft, strTop;

   // FIXME: We don't draw a tick if of end of our label arrays
   // But we shouldn't have an array of labels.
   if( mNumMinor >= mLength )
      return;
   if( mNumMajor >= mLength )
      return;

   Label *label;
   if (major)
      label = &mMajorLabels[labelIdx];
   else if (minor)
      label = &mMinorLabels[labelIdx];
   else
      label = &mMinorMinorLabels[labelIdx];

   label->value = 0.0;
   pos = label->pos;         // already stored in label class
   l   = label->text;
   label->lx = mLeft - 1000; // don't display
   label->ly = mTop - 1000;  // don't display

   mDC->SetFont(major? *mMajorFont: minor? *mMinorFont : *mMinorMinorFont);

   mDC->GetTextExtent(l, &strW, &strH, &strD, &strL);

   if (mOrientation == wxHORIZONTAL) {
      strLen = strW;
      strPos = pos - strW/2;
      if (strPos < 0)
         strPos = 0;
      if (strPos + strW >= mLength)
         strPos = mLength - strW;
      strLeft = mLeft + strPos;
      if (mFlip) {
         strTop = mTop + 4;
         mMaxHeight = max(mMaxHeight, strH + 4);
      }
      else {

         strTop = mTop- mLead+4;// More space was needed...
         mMaxHeight = max(mMaxHeight, strH + 6);
      }
   }
   else {
      strLen = strH;
      strPos = pos - strH/2;
      if (strPos < 0)
         strPos = 0;
      if (strPos + strH >= mLength)
         strPos = mLength - strH;
      strTop = mTop + strPos;
      if (mFlip) {
         strLeft = mLeft + 5;
         mMaxWidth = max(mMaxWidth, strW + 5);
      }
      else {

         strLeft =-strW-6;
       }
   }


   // FIXME: we shouldn't even get here if strPos < 0.
   // Ruler code currently does  not handle very small or
   // negative sized windows (i.e. don't draw) properly.
   if( strPos < 0 )
      return;

   // See if any of the pixels we need to draw this
   // label is already covered

   int i;
   for(i=0; i<strLen; i++)
      if (mBits[strPos+i])
         return;

   // If not, position the label

   label->lx = strLeft;
   label->ly = strTop;

   // And mark these pixels, plus some surrounding
   // ones (the spacing between labels), as covered
   int leftMargin = mSpacing;
   if (strPos < leftMargin)
      leftMargin = strPos;
   strPos -= leftMargin;
   strLen += leftMargin;

   int rightMargin = mSpacing;
   if (strPos + strLen > mLength - mSpacing)
      rightMargin = mLength - strPos - strLen;
   strLen += rightMargin;

   for(i=0; i<strLen; i++)
      mBits[strPos+i] = 1;


   wxRect r(strLeft, strTop, strW, strH);
   mRect.Union(r);

}

// ********** Draw grid ***************************
void Ruler::DrawGrid(wxDC& dc, int length, bool minor, bool major, int xOffset, int yOffset)
{
   mGridLineLength = length;
   mMajorGrid = major;
   mMinorGrid = minor;
   mDC = &dc;

   int gridPos;
   wxPen gridPen;

   if(mbMinor && (mMinorGrid && (mGridLineLength != 0 ))) {
      gridPen.SetColour(178, 178, 178); // very light grey
      mDC->SetPen(gridPen);
      for(int i=0; i<mNumMinor; i++) {
         gridPos = mMinorLabels[i].pos;
         if(mOrientation == wxHORIZONTAL) {
            if((gridPos != 0) && (gridPos != mGridLineLength))
               mDC->DrawLine(gridPos+xOffset, yOffset, gridPos+xOffset, mGridLineLength+yOffset);
         }
         else {
            if((gridPos != 0) && (gridPos != mGridLineLength))
               mDC->DrawLine(xOffset, gridPos+yOffset, mGridLineLength+xOffset, gridPos+yOffset);
         }
      }
   }

   if(mMajorGrid && (mGridLineLength != 0 )) {
      gridPen.SetColour(127, 127, 127); // light grey
      mDC->SetPen(gridPen);
      for(int i=0; i<mNumMajor; i++) {
         gridPos = mMajorLabels[i].pos;
         if(mOrientation == wxHORIZONTAL) {
            if((gridPos != 0) && (gridPos != mGridLineLength))
               mDC->DrawLine(gridPos+xOffset, yOffset, gridPos+xOffset, mGridLineLength+yOffset);
         }
         else {
            if((gridPos != 0) && (gridPos != mGridLineLength))
               mDC->DrawLine(xOffset, gridPos+yOffset, mGridLineLength+xOffset, gridPos+yOffset);
         }
      }

      int zeroPosition = GetZeroPosition();
      if(zeroPosition > 0) {
         // Draw 'zero' grid line in black
         mDC->SetPen(*wxBLACK_PEN);
         if(mOrientation == wxHORIZONTAL) {
            if(zeroPosition != mGridLineLength)
               mDC->DrawLine(zeroPosition+xOffset, yOffset, zeroPosition+xOffset, mGridLineLength+yOffset);
         }
         else {
            if(zeroPosition != mGridLineLength)
               mDC->DrawLine(xOffset, zeroPosition+yOffset, mGridLineLength+xOffset, zeroPosition+yOffset);
         }
      }
   }
}

int Ruler::FindZero(Label * label, const int len)
{
   int i = 0;
   double d = 1.0;   // arbitrary

   do {
      d = label[i].value;
      i++;
   } while( (i < len) && (d != 0.0) );

   if(d == 0.0)
      return (label[i - 1].pos) ;
   else
      return -1;
}

int Ruler::GetZeroPosition()
{
   int zero;
   if((zero = FindZero(mMajorLabels.get(), mNumMajor)) < 0)
      zero = FindZero(mMinorLabels.get(), mNumMinor);
   // PRL: don't consult minor minor??
   return zero;
}

void Ruler::GetMaxSize(wxCoord *width, wxCoord *height)
{
   if (!mValid) {
      wxScreenDC sdc;
      mDC = &sdc;
   }

   if (width)
      *width = mRect.GetWidth(); //mMaxWidth;

   if (height)
      *height = mRect.GetHeight(); //mMaxHeight;
}


void Ruler::SetCustomMode(bool value) { mCustom = value; }

void Ruler::SetCustomMajorLabels(wxArrayString *label, size_t numLabel, int start, int step)
{
   mNumMajor = numLabel;
   mMajorLabels.reinit(numLabel);

   for(size_t i = 0; i<numLabel; i++) {
      mMajorLabels[i].text = label->Item(i);
      mMajorLabels[i].pos  = start + i*step;
   }
   //Remember: DELETE majorlabels....
}

void Ruler::SetCustomMinorLabels(wxArrayString *label, size_t numLabel, int start, int step)
{
   mNumMinor = numLabel;
   mMinorLabels.reinit(numLabel);

   for(size_t i = 0; i<numLabel; i++) {
      mMinorLabels[i].text = label->Item(i);
      mMinorLabels[i].pos  = start + i*step;
   }
   //Remember: DELETE majorlabels....
}

void Ruler::Label::Draw(wxDC&dc, bool twoTone, wxColour c) const
{
   if (text != wxT("")) {
      bool altColor = twoTone && value < 0.0;
      dc.SetTextForeground(altColor ? theTheme.Colour( clrTextNegativeNumbers) : c);
      dc.DrawText(text, lx, ly);
   }
}

void Ruler::SetUseZoomInfo(int leftOffset, const ZoomInfo *zoomInfo)
{
   mLeftOffset = leftOffset;
   mUseZoomInfo = zoomInfo;
}

//
// RulerPanel
//

BEGIN_EVENT_TABLE(RulerPanel, wxPanelWrapper)
   EVT_ERASE_BACKGROUND(RulerPanel::OnErase)
   EVT_PAINT(RulerPanel::OnPaint)
   EVT_SIZE(RulerPanel::OnSize)
END_EVENT_TABLE()

IMPLEMENT_CLASS(RulerPanel, wxPanelWrapper)

RulerPanel::RulerPanel(wxWindow* parent, wxWindowID id,
                       wxOrientation orientation,
                       const wxSize &bounds,
                       const Range &range,
                       Ruler::RulerFormat format,
                       const Options &options,
                       const wxPoint& pos /*= wxDefaultPosition*/,
                       const wxSize& size /*= wxDefaultSize*/):
   wxPanelWrapper(parent, id, pos, size)
{
   ruler.SetBounds( 0, 0, bounds.x, bounds.y );
   ruler.SetRange( range.first, range.second );
   ruler.SetLog( options.log );
   ruler.SetFormat(format);
   ruler.SetFlip( options.flip );
   ruler.SetLabelEdges( options.labelEdges );
   ruler.mbTicksAtExtremes = options.ticksAtExtremes;
   if (orientation == wxVERTICAL) {
      wxCoord w;
      ruler.GetMaxSize(&w, NULL);
      SetMinSize(wxSize(w, 150));  // height needed for wxGTK
   }
   else if (orientation == wxHORIZONTAL) {
      wxCoord h;
      ruler.GetMaxSize(NULL, &h);
      SetMinSize(wxSize(wxDefaultCoord, h));
   }
   if (options.hasTickColour)
      ruler.SetTickColour( options.tickColour );
}

RulerPanel::~RulerPanel()
{
}

void RulerPanel::OnErase(wxEraseEvent & WXUNUSED(evt))
{
   // Ignore it to prevent flashing
}

void RulerPanel::OnPaint(wxPaintEvent & WXUNUSED(evt))
{
   wxPaintDC dc(this);

#if defined(__WXMSW__)
   dc.Clear();
#endif
}

void RulerPanel::OnSize(wxSizeEvent & WXUNUSED(evt))
{
   Refresh();
}

/*********************************************************************/
enum : int {
   IndicatorSmallWidth = 9,
   IndicatorMediumWidth = 13,
   IndicatorOffset = 1,

   TopMargin = 1,
   BottomMargin = 2, // for bottom bevel and bottom line
   LeftMargin = 1, 

   RightMargin = 1,
};

enum {
   ScrubHeight = 14,
   ProperRulerHeight = 29
};

inline int IndicatorHeightForWidth(int width)
{
   return ((width / 2) * 3) / 2;
}

inline int IndicatorWidthForHeight(int height)
{
   // Not an exact inverse of the above, with rounding, but good enough
   return std::max(static_cast<int>(IndicatorSmallWidth),
                   (((height) * 2) / 3) * 2
                   );
}

inline int IndicatorBigHeight()
{
   return std::max((int)(ScrubHeight - TopMargin),
                   (int)(IndicatorMediumWidth));
}

inline int IndicatorBigWidth()
{
   return IndicatorWidthForHeight(IndicatorBigHeight());
}

/**********************************************************************

  Implementation of AdornedRulerPanel.
  Either we find a way to make this more generic, Or it will move
  out of the widgets subdirectory into its own source file.

**********************************************************************/

#include "../ViewInfo.h"
#include "../AColor.h"

BEGIN_EVENT_TABLE(AdornedRulerPanel, OverlayPanel)
   EVT_PAINT(AdornedRulerPanel::OnPaint)
   EVT_SIZE(AdornedRulerPanel::OnSize)
   EVT_MOUSE_CAPTURE_LOST(AdornedRulerPanel::OnCaptureLost)

   // Pop up menus on Windows
   EVT_CONTEXT_MENU(AdornedRulerPanel::OnContextMenu)

END_EVENT_TABLE()

AdornedRulerPanel::AdornedRulerPanel(AudacityProject* project,
                                     wxWindow *parent,
                                     wxWindowID id,
                                     const wxPoint& pos,
                                     const wxSize& size,
                                     ViewInfo *viewinfo)
:  OverlayPanel(parent, id, pos, size)
, mProject(project)
, mViewInfo(viewinfo)
{
   for (auto &button : mButtons)
      button = nullptr;

   SetLabel( _("Timeline") );
   SetName(GetLabel());
   SetBackgroundStyle(wxBG_STYLE_PAINT);

   mCursorDefault = wxCursor(wxCURSOR_DEFAULT);
   mCursorHand = wxCursor(wxCURSOR_HAND);
   mCursorSizeWE = wxCursor(wxCURSOR_SIZEWE);
   mIsWE = false;

   mLeftOffset = 0;
   mIndTime = -1;

   mPlayRegionStart = -1;
   mPlayRegionLock = false;
   mPlayRegionEnd = -1;
   mOldPlayRegionStart = -1;
   mOldPlayRegionEnd = -1;
   mLeftDownClick = -1;
   mMouseEventState = mesNone;
   mIsDragging = false;

   mOuter = GetClientRect();

   mRuler.SetUseZoomInfo(mLeftOffset, mViewInfo);
   mRuler.SetLabelEdges( false );
   mRuler.SetFormat( Ruler::TimeFormat );

   mTracks = project->GetTracks();

   mSnapManager = NULL;
   mIsSnapped = false;

   mIsRecording = false;

#if wxUSE_TOOLTIPS
   wxToolTip::Enable(true);
#endif

   wxTheApp->Bind(EVT_AUDIOIO_CAPTURE,
                     &AdornedRulerPanel::OnCapture,
                     this);
}

AdornedRulerPanel::~AdornedRulerPanel()
{
   if(HasCapture())
      ReleaseMouse();
}

void AdornedRulerPanel::UpdatePrefs()
{
   // Affected by the last
   UpdateRects();
   SetPanelSize();
}

void AdornedRulerPanel::ReCreateButtons()
{
   // TODO: Should we do this to destroy the grabber??
   // Get rid of any children we may have
   // DestroyChildren();

   SetBackgroundColour(theTheme.Colour( clrMedium ));

   for (auto & button : mButtons) {
      if (button)
         button->Destroy();
      button = nullptr;
   }

   size_t iButton = 0;
   // Make the short row of time ruler pushbottons.
   // Don't bother with sizers.  Their sizes and positions are fixed.
   // Add a grabber converted to a spacer.
   // This makes it visually clearer that the button is a button.

   wxPoint position( 1, 0 );

   Grabber * pGrabber = safenew Grabber(this, this->GetId());
   pGrabber->SetAsSpacer( true );
   //pGrabber->SetSize( 10, 27 ); // default is 10,27
   pGrabber->SetPosition( position );

   position.x = 12;

   auto size = theTheme.ImageSize( bmpRecoloredUpSmall );
   size.y = std::min(size.y, GetRulerHeight());

   auto buttonMaker = [&]
   (wxWindowID id, teBmps bitmap, bool toggle)
   {
      const auto button =
      ToolBar::MakeButton(
         this,
         bmpRecoloredUpSmall, bmpRecoloredDownSmall, 
         bmpRecoloredUpHiliteSmall, bmpRecoloredHiliteSmall, 
         bitmap, bitmap, bitmap,
         id, position, toggle, size
      );

      position.x += size.GetWidth();
      mButtons[iButton++] = button;
      return button;
   };
}

void AdornedRulerPanel::OnCapture(wxCommandEvent & evt)
{
   evt.Skip();

   if (evt.GetInt() != 0)
   {
      // Set cursor immediately  because OnMouseEvents is not called
      // if recording is initiated by a modal window (Timer Record).
      SetCursor(mCursorDefault);
      mIsRecording = true;
   }
   else {
      SetCursor(mCursorHand);
      mIsRecording = false;
   }
}

void AdornedRulerPanel::OnPaint(wxPaintEvent & WXUNUSED(evt))
{
   if (mNeedButtonUpdate) {
      // Visit this block once only in the lifetime of this panel
      mNeedButtonUpdate = false;
      // Do this first time setting of button status texts
      // when we are sure the CommandManager is initialized.
      ReCreateButtons();
      // Sends a resize event, which will cause a second paint.
      UpdatePrefs();
   }

   wxPaintDC dc(this);

   auto &backDC = GetBackingDCForRepaint();

   DoDrawBackground(&backDC);

   if (!mViewInfo->selectedRegion.isPoint())
   {
      DoDrawSelection(&backDC);
   }

   DoDrawMarks(true);

   DoDrawEdge(&backDC);

   DisplayBitmap(dc);

   // Stroke extras direct to the client area,
   // maybe outside of the damaged area
   // As with TrackPanel, do not make a NEW wxClientDC or else Mac flashes badly!
   dc.DestroyClippingRegion();
   DrawOverlays(true, &dc);
}

void AdornedRulerPanel::OnSize(wxSizeEvent &evt)
{
   mOuter = GetClientRect();
   if (mOuter.GetWidth() == 0 || mOuter.GetHeight() == 0)
   {
      return;
   }

   UpdateRects();

   OverlayPanel::OnSize(evt);
}

void AdornedRulerPanel::UpdateRects()
{
   mInner = mOuter;
   mInner.x += LeftMargin;
   mInner.width -= (LeftMargin + RightMargin);

   auto top = &mInner;
   auto bottom = &mInner;

   top->y += TopMargin;
   top->height -= TopMargin;

   bottom->height -= BottomMargin;

   mRuler.SetBounds(mInner.GetLeft(),
                    mInner.GetTop(),
                    mInner.GetRight(),
                    mInner.GetBottom());

}

double AdornedRulerPanel::Pos2Time(int p, bool ignoreFisheye)
{
   return mViewInfo->PositionToTime(p, mLeftOffset, ignoreFisheye);
}

int AdornedRulerPanel::Time2Pos(double t, bool ignoreFisheye)
{
   return mViewInfo->TimeToPosition(t, mLeftOffset, ignoreFisheye);
}

bool AdornedRulerPanel::IsWithinMarker(int mousePosX, double markerTime)
{
   if (markerTime < 0)
      return false;

   int pixelPos = Time2Pos(markerTime);
   int boundLeft = pixelPos - SELECT_TOLERANCE_PIXEL;
   int boundRight = pixelPos + SELECT_TOLERANCE_PIXEL;

   return mousePosX >= boundLeft && mousePosX < boundRight;
}

void AdornedRulerPanel::SetPanelSize()
{
   wxSize size { GetSize().GetWidth(), GetRulerHeight() };
   SetSize(size);
   SetMinSize(size);
   GetParent()->PostSizeEventToParent();
}

void AdornedRulerPanel::OnContextMenu(wxContextMenuEvent & WXUNUSED(event))
{
   ShowContextMenu(MenuChoice::QuickPlay, nullptr);
}

void AdornedRulerPanel::OnCaptureLost(wxMouseCaptureLostEvent & WXUNUSED(evt))
{
   wxMouseEvent e(wxEVT_LEFT_UP);
   e.m_x = mLastMouseX;
}

void AdornedRulerPanel::UpdateQuickPlayPos(wxCoord &mousePosX)
{
   // Keep Quick-Play within usable track area.
   TrackPanel *tp = mProject->GetTrackPanel();
   int width;
   tp->GetTracksUsableArea(&width, NULL);
   mousePosX = std::max(mousePosX, tp->GetLeftOffset());
   mousePosX = std::min(mousePosX, tp->GetLeftOffset() + width - 1);

   mLastMouseX = mousePosX;
   mQuickPlayPosUnsnapped = mQuickPlayPos = Pos2Time(mousePosX);
}

// Pop-up menus

void AdornedRulerPanel::ShowMenu(const wxPoint & pos)
{
   wxMenu rulerMenu;

   PopupMenu(&rulerMenu, pos);
}

void AdornedRulerPanel::OnToggleQuickPlay(wxCommandEvent&)
{
   mQuickPlayEnabled = (mQuickPlayEnabled)? false : true;
   gPrefs->Write(wxT("/QuickPlay/QuickPlayEnabled"), mQuickPlayEnabled);
   gPrefs->Flush();
}

void AdornedRulerPanel::OnSyncSelToQuickPlay(wxCommandEvent&)
{
   mPlayRegionDragsSelection = (mPlayRegionDragsSelection)? false : true;
   gPrefs->Write(wxT("/QuickPlay/DragSelection"), mPlayRegionDragsSelection);
   gPrefs->Flush();
}

void AdornedRulerPanel::DragSelection()
{
   mViewInfo->selectedRegion.setT0(mPlayRegionStart, false);
   mViewInfo->selectedRegion.setT1(mPlayRegionEnd, true);
}

void AdornedRulerPanel::HandleSnapping()
{
   if (!mSnapManager) {
      mSnapManager = std::make_unique<SnapManager>(mTracks, mViewInfo);
   }

   auto results = mSnapManager->Snap(NULL, mQuickPlayPos, false);
   mQuickPlayPos = results.outTime;
   mIsSnapped = results.Snapped();
}

void AdornedRulerPanel::OnTimelineToolTips(wxCommandEvent&)
{
   mTimelineToolTip = (mTimelineToolTip)? false : true;
   gPrefs->Write(wxT("/QuickPlay/ToolTips"), mTimelineToolTip);
   gPrefs->Flush();
}

void AdornedRulerPanel::OnAutoScroll(wxCommandEvent&)
{
   if (mViewInfo->bUpdateTrackIndicator)
      gPrefs->Write(wxT("/GUI/AutoScroll"), false);
   else
      gPrefs->Write(wxT("/GUI/AutoScroll"), true);
   mProject->UpdatePrefs();
   gPrefs->Flush();
}


void AdornedRulerPanel::OnLockPlayRegion(wxCommandEvent&)
{
   if (mProject->IsPlayRegionLocked())
      mProject->OnUnlockPlayRegion(*mProject);
   else
      mProject->OnLockPlayRegion(*mProject);
}

void AdornedRulerPanel::ShowContextMenu( MenuChoice choice, const wxPoint *pPosition)
{
   wxPoint position;
   if(pPosition)
      position = *pPosition;
   else
   {
      auto rect = GetRect();
      position = { rect.GetLeft() + 1, rect.GetBottom() + 1 };
   }

   switch (choice) {
      case MenuChoice::QuickPlay:
         ShowMenu(position); break;
      default:
         return;
   }

   if (HasCapture())
      ReleaseMouse();
}

void AdornedRulerPanel::DoDrawBackground(wxDC * dc)
{
   // Draw AdornedRulerPanel border
   AColor::UseThemeColour( dc, clrTrackInfo );
   dc->DrawRectangle( mInner );
}

void AdornedRulerPanel::DoDrawEdge(wxDC *dc)
{
   wxRect r = mOuter;
   r.width -= RightMargin;
   r.height -= BottomMargin;
   AColor::BevelTrackInfo( *dc, true, r );

   // Black stroke at bottom
   dc->SetPen( *wxBLACK_PEN );
   dc->DrawLine( mOuter.x,
                mOuter.y + mOuter.height - 1,
                mOuter.x + mOuter.width,
                mOuter.y + mOuter.height - 1 );
}

void AdornedRulerPanel::DoDrawMarks(bool /*text */ )
{
   const double min = Pos2Time(0);
   const double hiddenMin = Pos2Time(0, true);
   const double max = Pos2Time(mInner.width);
   const double hiddenMax = Pos2Time(mInner.width, true);

   mRuler.SetTickColour( theTheme.Colour( clrTrackPanelText ) );
   mRuler.SetRange( min, max, hiddenMin, hiddenMax );
}

void AdornedRulerPanel::DrawSelection()
{
   Refresh();
}

void AdornedRulerPanel::DoDrawSelection(wxDC * dc)
{
   // Draw selection
   const int p0 = max(1, Time2Pos(mViewInfo->selectedRegion.t0()));
   const int p1 = min(mInner.width, Time2Pos(mViewInfo->selectedRegion.t1()));

   dc->SetBrush( wxBrush( theTheme.Colour( clrRulerBackground )) );
   dc->SetPen(   wxPen(   theTheme.Colour( clrRulerBackground )) );

   wxRect r;
   r.x = p0;
   r.y = mInner.y;
   r.width = p1 - p0 - 1;
   r.height = mInner.height;
   dc->DrawRectangle( r );
}

int AdornedRulerPanel::GetRulerHeight()
{
   return ProperRulerHeight;
}

void AdornedRulerPanel::SetLeftOffset(int offset)
{
   mLeftOffset = offset;
   mRuler.SetUseZoomInfo(offset, mViewInfo);
}

// Draws the play/recording position indicator.
void AdornedRulerPanel::DoDrawIndicator
   (wxDC * dc, wxCoord xx, bool playing, int width, bool scrub, bool seek)
{
   ADCChanger changer(dc); // Undo pen and brush changes at function exit

   AColor::IndicatorColor( dc, playing );

   wxPoint tri[ 3 ];
   if (seek) {
      auto height = IndicatorHeightForWidth(width);
      // Make four triangles
      const int TriangleWidth = width * 3 / 8;

      // Double-double headed, left-right
      auto yy = (mInner.GetBottom() + 1) - 1 /* bevel */ - height;
      tri[ 0 ].x = xx - IndicatorOffset;
      tri[ 0 ].y = yy;
      tri[ 1 ].x = xx - IndicatorOffset;
      tri[ 1 ].y = yy + height;
      tri[ 2 ].x = xx - TriangleWidth;
      tri[ 2 ].y = yy + height / 2;
      dc->DrawPolygon( 3, tri );

      tri[ 0 ].x -= TriangleWidth;
      tri[ 1 ].x -= TriangleWidth;
      tri[ 2 ].x -= TriangleWidth;
      dc->DrawPolygon( 3, tri );

      tri[ 0 ].x = tri[ 1 ].x = xx + IndicatorOffset;
      tri[ 2 ].x = xx + TriangleWidth;
      dc->DrawPolygon( 3, tri );


      tri[ 0 ].x += TriangleWidth;
      tri[ 1 ].x += TriangleWidth;
      tri[ 2 ].x += TriangleWidth;
      dc->DrawPolygon( 3, tri );
   }
   else if (scrub) {
      auto height = IndicatorHeightForWidth(width);
      const int IndicatorHalfWidth = width / 2;

      // Double headed, left-right
      auto yy = (mInner.GetBottom() + 1) - 1 /* bevel */ - height;
      tri[ 0 ].x = xx - IndicatorOffset;
      tri[ 0 ].y = yy;
      tri[ 1 ].x = xx - IndicatorOffset;
      tri[ 1 ].y = yy + height;
      tri[ 2 ].x = xx - IndicatorHalfWidth;
      tri[ 2 ].y = yy + height / 2;
      dc->DrawPolygon( 3, tri );
      tri[ 0 ].x = tri[ 1 ].x = xx + IndicatorOffset;
      tri[ 2 ].x = xx + IndicatorHalfWidth;
      dc->DrawPolygon( 3, tri );
   }
   else {
      bool pinned = TracksPrefs::GetPinnedHeadPreference();
      wxBitmap & bmp = theTheme.Bitmap( pinned ? 
         (playing ? bmpPlayPointerPinned : bmpRecordPointerPinned) :
         (playing ? bmpPlayPointer : bmpRecordPointer) 
      );
      const int IndicatorHalfWidth = bmp.GetWidth() / 2;
      dc->DrawBitmap( bmp, xx - IndicatorHalfWidth -1, mInner.y );
   }
}

void AdornedRulerPanel::SetPlayRegion(double playRegionStart,
                                      double playRegionEnd)
{
   // This is called by AudacityProject to make the play region follow
   // the current selection. But while the user is selecting a play region
   // with the mouse directly in the ruler, changes from outside are blocked.
   if (mMouseEventState != mesNone)
      return;

   mPlayRegionStart = playRegionStart;
   mPlayRegionEnd = playRegionEnd;

   Refresh();
}

void AdornedRulerPanel::ClearPlayRegion()
{
   ControlToolBar* ctb = mProject->GetControlToolBar();
   ctb->StopPlaying();

   mPlayRegionStart = -1;
   mPlayRegionEnd = -1;

   Refresh();
}

void AdornedRulerPanel::GetPlayRegion(double* playRegionStart,
                                      double* playRegionEnd)
{
   if (mPlayRegionStart >= 0 && mPlayRegionEnd >= 0 &&
       mPlayRegionEnd < mPlayRegionStart)
   {
      // swap values to make sure end > start
      *playRegionStart = mPlayRegionEnd;
      *playRegionEnd = mPlayRegionStart;
   } else
   {
      *playRegionStart = mPlayRegionStart;
      *playRegionEnd = mPlayRegionEnd;
   }
}

void AdornedRulerPanel::GetMaxSize(wxCoord *width, wxCoord *height)
{
   mRuler.GetMaxSize(width, height);
}

bool AdornedRulerPanel::s_AcceptsFocus{ false };

auto AdornedRulerPanel::TemporarilyAllowFocus() -> TempAllowFocus {
   s_AcceptsFocus = true;
   return TempAllowFocus{ &s_AcceptsFocus };
}

void AdornedRulerPanel::SetFocusFromKbd()
{
   auto temp = TemporarilyAllowFocus();
   SetFocus();
}
