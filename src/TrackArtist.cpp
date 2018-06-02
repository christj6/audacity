/**********************************************************************

  Audacity: A Digital Audio Editor

  TrackArtist.cpp

  Dominic Mazzoni


*******************************************************************//*!

\class TrackArtist
\brief   This class handles the actual rendering of WaveTracks (both
  waveforms and spectra).

  It's actually a little harder than it looks, because for
  waveforms at least it needs to cache the samples that are
  currently on-screen.

<b>How Audacity Redisplay Works \n
 Roger Dannenberg</b> \n
Oct 2010 \n

This is a brief guide to Audacity redisplay -- it may not be complete. It
is my attempt to understand the complicated graphics strategy.

One basic idea is that redrawing waveforms is rather slow, so Audacity
saves waveform images in bitmaps to make redrawing faster. In particular,
during audio playback (and recording), the vertical time indicator is
drawn over the waveform about 20 times per second. To avoid unnecessary
computation, the indicator is erased by copying a column of pixels from
a bitmap image of the waveform. Notice that this implies a two-stage
process: first, waveforms are drawn to the bitmp; then, the bitmap
(or pieces of it) are copied to the screen, perhaps along with other
graphics.

The bitmap is for the entire track panel, i.e. multiple tracks, and
includes things like the Gain and Pan slders to the left of the
waveform images.

The screen update uses a mixture of direct drawing and indirect paint
events. The "normal" way to update a graphical display is to call
the Refresh() method when something invalidates the screen. Later, the
system calls OnPaint(), which the application overrides to (re)draw the
screen. In wxWidgets, you can also draw directly to the screen without
calling Refresh() and without waiting for OnPaint() to be called.

I would expect there to be a 2-level invalidation scheme: Some changes
invalidate the bitmap, forcing a bitmap redraw *and* a screen redraw.
Other changes merely update the screen using pre-existing bitmaps. In
Audacity, the "2-level" invalidation works like this: Anything
that invalidates the bitmap calls TrackPanel::Refresh(), which
has an eraseBackground parameter. This flag says to redraw the
bitmap when OnPaint() is called. If eraseBackground is false, the
existing bitmap can be used for waveform images. Audacity also
draws directly to the screen to update the time indicator during
playback. To move the indicator, one column of pixels is drawn to
the screen to remove the indicator. Then the indicator is drawn at
a NEW time location.

The track panel consists of many components. The tree of calls that
update the bitmap looks like this:

\code
TrackPanel::DrawTracks(), calls
       TrackArtist::DrawTracks();
       TrackPanel::DrawEverythingElse();
               for each track,
                       TrackPanel::DrawOutside();
                               TrackPanel::DrawOutsideOfTrack();
                               TrackPanel::DrawBordersAroundTrack();
                               TrackPanel::DrawShadow();
                               TrackInfo::DrawBordersWithin();
                               various TrackInfo sliders and buttons
                       TrackArtist::DrawVRuler();
               TrackPanel::DrawZooming();
                       draws horizontal dashed lines during zoom-drag
               TrackPanel::HighlightFocusedTrack();
                       draws yellow highlight on selected track
               draw snap guidelines if any
\endcode

After drawing the bitmap and blitting the bitmap to the screen,
the following calls are (sometimes) made. To keep track of what has
been drawn on screen over the bitmap images,
\li \c mLastCursor is the position of the vertical line representing sel0,
        the selected time position
\li \c mLastIndicator is the position of the moving vertical line during
        playback

\code
TrackPanel::DoDrawIndicator();
        copy pixel column from bitmap to screen to erase indicator line
        TrackPanel::DoDrawCursor(); [if mLastCursor == mLastIndicator]
        TrackPanel::DisplaySelection();
        AdornedRulerPanel::DrawIndicator(); [not part of TrackPanel graphics]
        draw indicator on each track
TrackPanel::DoDrawCursor();
        draw cursor on each track  [at selectedRegion.t0()]
        AdornedRulerPanel::DrawCursor(); [not part of TrackPanel graphics]
        TrackPanel::DisplaySelection();
\endcode

To move the indicator, TrackPanel::OnTimer() calls the following, using
a drawing context (DC) for the screen. (Refresh is not called to create
an OnPaint event. Instead, drawing is direct to the screen.)
\code
TrackPanel::DrawIndicator();
        TrackPanel::DoDrawIndicator();
\endcode

Notice that TrackPanel::DrawZooming(), TrackPanel::HighlightFocusedTrack(),
and snap guidelines could be drawn directly to the screen rather than to
the bitmap, generally eliminating redraw work.

One problem is slider udpates. Sliders are in the left area of the track
panel. They are not wxWindows like wxSliders, but instead are just drawn
on the TrackPanel. When slider state changes, *all* tracks do a full
refresh, including recomputing the backing store. It would make more sense
to just invalidate the region containing the slider. However, doing that
would require either incrementally updating the bitmap (not currently done),
or maintaining the sliders and other track info on the screen and not in
the bitmap.

In my opinion, the bitmap should contain only the waveform, note, and
label images along with gray selection highlights. The track info
(sliders, buttons, title, etc.), track selection highlight, cursor, and
indicator should be drawn in the normal way, and clipping regions should
be used to avoid excessive copying of bitmaps (say, when sliders move),
or excessive redrawing of track info widgets (say, when scrolling occurs).
This is a fairly tricky code change since it requires careful specification
of what and where redraw should take place when any state changes. One
surprising finding is that NoteTrack display is slow compared to WaveTrack
display. Each note takes some time to gather attributes and select colors,
and while audio draws two amplitudes per horizontal pixels, large MIDI
scores can have more notes than horizontal pixels. This can make slider
changes very sluggish, but this can also be a problem with many
audio tracks.

*//*******************************************************************/

#include "Audacity.h"
#include "TrackArtist.h"
#include "float_cast.h"

#include <math.h>
#include <float.h>
#include <limits>

#ifdef HAVE_ALLOCA_H
#include <alloca.h>
#endif

#include <wx/brush.h>
#include <wx/colour.h>
#include <wx/dc.h>
#include <wx/dcmemory.h>
#include <wx/gdicmn.h>
#include <wx/graphics.h>
#include <wx/image.h>
#include <wx/pen.h>
#include <wx/log.h>
#include <wx/datetime.h>

#include "AColor.h"
#include "BlockFile.h"
#include "Envelope.h"
#include "NumberScale.h"
#include "WaveTrack.h"
#include "Prefs.h"
#include "prefs/GUISettings.h"
#include "prefs/WaveformSettings.h"
#include "ViewInfo.h"
#include "widgets/Ruler.h"
#include "Theme.h"
#include "AllThemeResources.h"
#include "Experimental.h"
#include "TrackPanelDrawingContext.h"


#undef PROFILE_WAVEFORM
#ifdef PROFILE_WAVEFORM
   #ifdef __WXMSW__
      #include <time.h>
   #else
      #include <sys/time.h>
   #endif
double gWaveformTimeTotal = 0;
int gWaveformTimeCount = 0;

namespace {
   struct Profiler {
      Profiler()
      {
#   ifdef __WXMSW__
         _time64(&tv0);
#   else
         gettimeofday(&tv0, NULL);
#   endif
      }

      ~Profiler()
      {
#   ifdef __WXMSW__
         _time64(&tv1);
         double elapsed = _difftime64(tv1, tv0);
#   else
         gettimeofday(&tv1, NULL);
         double elapsed =
            (tv1.tv_sec + tv1.tv_usec*0.000001) -
            (tv0.tv_sec + tv0.tv_usec*0.000001);
#   endif
         gWaveformTimeTotal += elapsed;
         gWaveformTimeCount++;
         wxPrintf(wxT("Avg waveform drawing time: %f\n"),
            gWaveformTimeTotal / gWaveformTimeCount);
      }

#   ifdef __WXMSW__
      __time64_t tv0, tv1;
#else
      struct timeval tv0, tv1;
#endif
   };
}
#endif

TrackArtist::TrackArtist()
{
   mMarginLeft   = 0;
   mMarginTop    = 0;
   mMarginRight  = 0;
   mMarginBottom = 0;

   mdBrange = ENV_DB_RANGE;
   mShowClipping = false;
   mSampleDisplay = 1;// Stem plots by default.
   UpdatePrefs();

   SetColours(0);
   vruler = std::make_unique<Ruler>();
}

TrackArtist::~TrackArtist()
{
}

void TrackArtist::SetColours( int iColorIndex)
{
   theTheme.SetBrushColour( blankBrush,      clrBlank );
   theTheme.SetBrushColour( unselectedBrush, clrUnselected);
   theTheme.SetBrushColour( selectedBrush,   clrSelected);
   theTheme.SetBrushColour( sampleBrush,     clrSample);
   theTheme.SetBrushColour( selsampleBrush,  clrSelSample);
   theTheme.SetBrushColour( dragsampleBrush, clrDragSample);
   theTheme.SetBrushColour( blankSelectedBrush, clrBlankSelected);

   theTheme.SetPenColour(   blankPen,        clrBlank);
   theTheme.SetPenColour(   unselectedPen,   clrUnselected);
   theTheme.SetPenColour(   selectedPen,     clrSelected);
   theTheme.SetPenColour(   muteSamplePen,   clrMuteSample);
   theTheme.SetPenColour(   odProgressDonePen, clrProgressDone);
   theTheme.SetPenColour(   odProgressNotYetPen, clrProgressNotYet);
   theTheme.SetPenColour(   shadowPen,       clrShadow);
   theTheme.SetPenColour(   clippedPen,      clrClipped);
   theTheme.SetPenColour(   muteClippedPen,  clrMuteClipped);
   theTheme.SetPenColour(   blankSelectedPen,clrBlankSelected);

   theTheme.SetPenColour(   selsamplePen,    clrSelSample);
   theTheme.SetPenColour(   muteRmsPen,      clrMuteRms);

   switch( iColorIndex %4 )
   {
      default:
      case 0:
         theTheme.SetPenColour(   samplePen,       clrSample);
         theTheme.SetPenColour(   rmsPen,          clrRms);
         break;
      case 1: // RED
         samplePen.SetColour( wxColor( 160,10,10 ) );
         rmsPen.SetColour( wxColor( 230,80,80 ) );
         break;
      case 2: // GREEN
         samplePen.SetColour( wxColor( 35,110,35 ) );
         rmsPen.SetColour( wxColor( 75,200,75 ) );
         break;
      case 3: //BLACK
         samplePen.SetColour( wxColor( 0,0,0 ) );
         rmsPen.SetColour( wxColor( 100,100,100 ) );
         break;

   }
}

void TrackArtist::SetMargins(int left, int top, int right, int bottom)
{
   mMarginLeft   = left;
   mMarginTop    = top;
   mMarginRight  = right;
   mMarginBottom = bottom;
}

void TrackArtist::DrawTracks(TrackPanelDrawingContext &context,
                             TrackList * tracks,
                             Track * start,
                             const wxRegion & reg,
                             const wxRect & rect,
                             const wxRect & clip,
                             const SelectedRegion &selectedRegion,
                             const ZoomInfo &zoomInfo)
{
   wxRect trackRect = rect;
   wxRect stereoTrackRect;
   TrackListIterator iter(tracks);
   Track *t;

   bool hasSolo = false;
   for (t = iter.First(); t; t = iter.Next()) {
      auto other = tracks->FindPendingChangedTrack(t->GetId());
      if (other)
         t = other.get();
   }

#if defined(DEBUG_CLIENT_AREA)
   // Change the +0 to +1 or +2 to see the bounding box
   mMarginLeft = 1+0; mMarginTop = 5+0; mMarginRight = 6+0; mMarginBottom = 2+0;

   // This just shows what the passed in rectangles enclose
   dc.SetPen(wxColour(*wxGREEN));
   dc.SetBrush(*wxTRANSPARENT_BRUSH);
   dc.DrawRectangle(rect);
   dc.SetPen(wxColour(*wxBLUE));
   dc.SetBrush(*wxTRANSPARENT_BRUSH);
   dc.DrawRectangle(clip);
#endif

   gPrefs->Read(wxT("/GUI/ShowTrackNameInWaveform"), &mbShowTrackNameInWaveform, false);

   t = iter.StartWith(start);
   while (t) {
      auto other = tracks->FindPendingChangedTrack(t->GetId());
      if (other)
         t = other.get();
      trackRect.y = t->GetY() - zoomInfo.vpos;
      trackRect.height = t->GetHeight();

      if (trackRect.y > clip.GetBottom() && !t->GetLinked()) {
         break;
      }

#if defined(DEBUG_CLIENT_AREA)
      // Filled rectangle to show the interior of the client area
      wxRect zr = trackRect;
      zr.x+=1; zr.y+=5; zr.width-=7; zr.height-=7;
      dc.SetPen(*wxCYAN_PEN);
      dc.SetBrush(*wxRED_BRUSH);
      dc.DrawRectangle(zr);
#endif

      stereoTrackRect = trackRect;

      // For various reasons, the code will break if we display one
      // of a stereo pair of tracks but not the other - for example,
      // if you try to edit the envelope of one track when its linked
      // pair is off the screen, then it won't be able to edit the
      // offscreen envelope.  So we compute the rect of the track and
      // its linked partner, and see if any part of that rect is on-screen.
      // If so, we draw both.  Otherwise, we can safely draw neither.

      Track *link = t->GetLink();
      if (link) {
         if (t->GetLinked()) {
            // If we're the first track
            stereoTrackRect.height += link->GetHeight();
         }
         else {
            // We're the second of two
            stereoTrackRect.y -= link->GetHeight();
            stereoTrackRect.height += link->GetHeight();
         }
      }

      if (stereoTrackRect.Intersects(clip) && reg.Contains(stereoTrackRect)) {
         wxRect rr = trackRect;
         rr.x += mMarginLeft;
         rr.y += mMarginTop;
         rr.width -= (mMarginLeft + mMarginRight);
         rr.height -= (mMarginTop + mMarginBottom);
         DrawTrack(context, t, rr,
                   selectedRegion, zoomInfo, hasSolo);
      }

      t = iter.Next();
   }
}

void TrackArtist::DrawTrack(TrackPanelDrawingContext &context,
                            const Track * t,
                            const wxRect & rect,
                            const SelectedRegion &selectedRegion,
                            const ZoomInfo &zoomInfo,
                            bool hasSolo)
{
   auto &dc = context.dc;
   switch (t->GetKind()) {
   case Track::Wave:
   {
      const WaveTrack* wt = static_cast<const WaveTrack*>(t);
      for (const auto &clip : wt->GetClips()) {
         clip->ClearDisplayRect();
      }

      bool muted = (hasSolo || wt->GetMute());

#if defined(__WXMAC__)
      wxAntialiasMode aamode = dc.GetGraphicsContext()->GetAntialiasMode();
      dc.GetGraphicsContext()->SetAntialiasMode(wxANTIALIAS_NONE);
#endif

	  // note: if Waveform is all we're doing, it's pretty redundant to have one-case switch statements everywhere.
	  // Consider removing the WaveTrack::GetDisplay() and WaveTrack::Waveform.
      switch (wt->GetDisplay()) {
      case WaveTrack::Waveform: 
         DrawWaveform(context, wt, rect, selectedRegion, zoomInfo, muted);
         break;
      default:
         wxASSERT(false);
      }

#if defined(__WXMAC__)
      dc.GetGraphicsContext()->SetAntialiasMode(aamode);
#endif

      if (mbShowTrackNameInWaveform &&
          // Exclude right channel of stereo track 
          !(!wt->GetLinked() && wt->GetLink())) {
         wxBrush Brush;
         wxCoord x,y;
         wxFont labelFont(12, wxSWISS, wxNORMAL, wxNORMAL);
         dc.SetFont(labelFont);
         dc.GetTextExtent( wt->GetName(), &x, &y );
         dc.SetTextForeground(theTheme.Colour( clrTrackPanelText ));
         // A nice improvement would be to draw the shield / background translucently.
         AColor::UseThemeColour( &dc, clrTrackInfoSelected, clrTrackPanelText );
         dc.DrawRoundedRectangle( wxPoint( rect.x+7, rect.y+1 ), wxSize( x+16, y+4), 8.0 );
         dc.DrawText (wt->GetName(), rect.x+15, rect.y+3);  // move right 15 pixels to avoid overwriting <- symbol
      }
      break;              // case Wave
   }
   }
}

void TrackArtist::DrawVRuler
(TrackPanelDrawingContext &context, const Track *t, wxRect & rect)
{
   auto dc = &context.dc;
   bool highlight = false;

   int kind = t->GetKind();

   // Label and Time tracks do not have a vruler
   // But give it a beveled area
   if (kind == Track::Label) {
      wxRect bev = rect;
      bev.Inflate(-1, 0);
      bev.width += 1;
      AColor::BevelTrackInfo(*dc, true, bev);

      return;
   }

   // Time tracks
   if (kind == Track::Time) {
      wxRect bev = rect;
      bev.Inflate(-1, 0);
      bev.width += 1;
      AColor::BevelTrackInfo(*dc, true, bev);

      // Right align the ruler
      wxRect rr = rect;
      rr.width--;
      if (t->vrulerSize.GetWidth() < rect.GetWidth()) {
         int adj = rr.GetWidth() - t->vrulerSize.GetWidth();
         rr.x += adj;
         rr.width -= adj;
      }

      UpdateVRuler(t, rr);
      vruler->SetTickColour( theTheme.Colour( clrTrackPanelText ));

      return;
   }

   // All waves have a ruler in the info panel
   // The ruler needs a bevelled surround.
   if (kind == Track::Wave) {
      wxRect bev = rect;
      bev.Inflate(-1, 0);
      bev.width += 1;
      AColor::BevelTrackInfo(*dc, true, bev, highlight);

      // Right align the ruler
      wxRect rr = rect;
      rr.width--;
      if (t->vrulerSize.GetWidth() < rect.GetWidth()) {
         int adj = rr.GetWidth() - t->vrulerSize.GetWidth();
         rr.x += adj;
         rr.width -= adj;
      }

      UpdateVRuler(t, rr);
      vruler->SetTickColour( theTheme.Colour( clrTrackPanelText ));

      return;
   }
}

void TrackArtist::UpdateVRuler(const Track *t, wxRect & rect)
{
   // Label tracks do not have a vruler
   if (t->GetKind() == Track::Label) {
      return;
   }

   // All waves have a ruler in the info panel
   // The ruler needs a bevelled surround.
   if (t->GetKind() == Track::Wave) {
      const WaveTrack *wt = static_cast<const WaveTrack*>(t);
      const float dBRange =
         wt->GetWaveformSettings().dBRange;

      const int display = wt->GetDisplay();

      if (display == WaveTrack::Waveform) {
         WaveformSettings::ScaleType scaleType =
            wt->GetWaveformSettings().scaleType;

         if (scaleType == WaveformSettings::stLinear) {
            // Waveform

            float min, max;
            wt->GetDisplayBounds(&min, &max);
            if (wt->GetLastScaleType() != scaleType &&
                wt->GetLastScaleType() != -1)
            {
               // do a translation into the linear space
               wt->SetLastScaleType();
               wt->SetLastdBRange();
               float sign = (min >= 0 ? 1 : -1);
               if (min != 0.) {
                  min = DB_TO_LINEAR(fabs(min) * dBRange - dBRange);
                  if (min < 0.0)
                     min = 0.0;
                  min *= sign;
               }
               sign = (max >= 0 ? 1 : -1);

               if (max != 0.) {
                  max = DB_TO_LINEAR(fabs(max) * dBRange - dBRange);
                  if (max < 0.0)
                     max = 0.0;
                  max *= sign;
               }
               wt->SetDisplayBounds(min, max);
            }

            vruler->SetBounds(rect.x, rect.y, rect.x + rect.width, rect.y + rect.height - 1);
            vruler->SetRange(max, min);
            vruler->SetFormat(Ruler::RealFormat);
            vruler->SetLabelEdges(false);
            vruler->SetLog(false);
         }
         else {
            wxASSERT(scaleType == WaveformSettings::stLogarithmic);
            scaleType = WaveformSettings::stLogarithmic;

            float min, max;
            wt->GetDisplayBounds(&min, &max);
            float lastdBRange;

            if (wt->GetLastScaleType() != scaleType &&
                wt->GetLastScaleType() != -1)
            {
               // do a translation into the dB space
               wt->SetLastScaleType();
               wt->SetLastdBRange();
               float sign = (min >= 0 ? 1 : -1);
               if (min != 0.) {
                  min = (LINEAR_TO_DB(fabs(min)) + dBRange) / dBRange;
                  if (min < 0.0)
                     min = 0.0;
                  min *= sign;
               }
               sign = (max >= 0 ? 1 : -1);

               if (max != 0.) {
                  max = (LINEAR_TO_DB(fabs(max)) + dBRange) / dBRange;
                  if (max < 0.0)
                     max = 0.0;
                  max *= sign;
               }
               wt->SetDisplayBounds(min, max);
            }
            else if (dBRange != (lastdBRange = wt->GetLastdBRange())) {
               wt->SetLastdBRange();
               // Remap the max of the scale
               const float sign = (max >= 0 ? 1 : -1);
               float newMax = max;
               if (max != 0.) {

// Ugh, duplicating from TrackPanel.cpp
#define ZOOMLIMIT 0.001f

                  const float extreme = LINEAR_TO_DB(2);
                  // recover dB value of max
                  const float dB = std::min(extreme, (float(fabs(max)) * lastdBRange - lastdBRange));
                  // find NEW scale position, but old max may get trimmed if the db limit rises
                  // Don't trim it to zero though, but leave max and limit distinct
                  newMax = sign * std::max(ZOOMLIMIT, (dBRange + dB) / dBRange);
                  // Adjust the min of the scale if we can,
                  // so the db Limit remains where it was on screen, but don't violate extremes
                  if (min != 0.)
                     min = std::max(-extreme, newMax * min / max);
               }

               wt->SetDisplayBounds(min, newMax);
            }

            if (max > 0) {
               int top = 0;
               float topval = 0;
               int bot = rect.height;
               float botval = -dBRange;

               if (min < 0) {
                  bot = top + (int)((max / (max - min))*(bot - top));
                  min = 0;
               }

               if (max > 1) {
                  top += (int)((max - 1) / (max - min) * (bot - top));
                  max = 1;
               }

               if (max < 1 && max > 0)
                  topval = -((1 - max) * dBRange);

               if (min > 0) {
                  botval = -((1 - min) * dBRange);
               }

               vruler->SetBounds(rect.x, rect.y + top, rect.x + rect.width, rect.y + bot - 1);
               vruler->SetRange(topval, botval);
            }
            else
               vruler->SetBounds(0.0, 0.0, 0.0, 0.0); // A.C.H I couldn't find a way to just disable it?
            vruler->SetFormat(Ruler::RealLogFormat);
            vruler->SetLabelEdges(true);
            vruler->SetLog(false);
         }
      }
   }
   vruler->GetMaxSize(&t->vrulerSize.x, &t->vrulerSize.y);
}

/// Takes a value between min and max and returns a value between
/// height and 0
/// \todo  Should this function move int GuiWaveTrack where it can
/// then use the zoomMin, zoomMax and height values without having
/// to have them passed in to it??
int GetWaveYPos(float value, float min, float max,
                int height, bool dB, bool outer,
                float dBr, bool clip)
{
   if (dB) {
      if (height == 0) {
         return 0;
      }

      float sign = (value >= 0 ? 1 : -1);

      if (value != 0.) {
         float db = LINEAR_TO_DB(fabs(value));
         value = (db + dBr) / dBr;
         if (!outer) {
            value -= 0.5;
         }
         if (value < 0.0) {
            value = 0.0;
         }
         value *= sign;
      }
   }
   else {
      if (!outer) {
         if (value >= 0.0) {
            value -= 0.5;
         }
         else {
            value += 0.5;
         }
      }
   }

   if (clip) {
      if (value < min) {
         value = min;
      }
      if (value > max) {
         value = max;
      }
   }

   value = (max - value) / (max - min);
   return (int) (value * (height - 1) + 0.5);
}

float FromDB(float value, double dBRange)
{
   if (value == 0)
      return 0;

   double sign = (value >= 0 ? 1 : -1);
   return DB_TO_LINEAR((fabs(value) * dBRange) - dBRange) * sign;
}

float ValueOfPixel(int yy, int height, bool offset,
   bool dB, double dBRange, float zoomMin, float zoomMax)
{
   wxASSERT(height > 0);
   // Map 0 to max and height - 1 (not height) to min
   float v =
      height == 1 ? (zoomMin + zoomMax) / 2 :
      zoomMax - (yy / (float)(height - 1)) * (zoomMax - zoomMin);
   if (offset) {
      if (v > 0.0)
         v += .5;
      else
         v -= .5;
   }

   if (dB)
      v = FromDB(v, dBRange);

   return v;
}

void TrackArtist::DrawNegativeOffsetTrackArrows(wxDC &dc, const wxRect &rect)
{
   // Draws two black arrows on the left side of the track to
   // indicate the user that the track has been time-shifted
   // to the left beyond t=0.0.

   dc.SetPen(*wxBLACK_PEN);
   AColor::Line(dc,
                rect.x + 2, rect.y + 6,
                rect.x + 8, rect.y + 6);
   AColor::Line(dc,
                rect.x + 2, rect.y + 6,
                rect.x + 6, rect.y + 2);
   AColor::Line(dc,
                rect.x + 2, rect.y + 6,
                rect.x + 6, rect.y + 10);
   AColor::Line(dc,
                rect.x + 2, rect.y + rect.height - 8,
                rect.x + 8, rect.y + rect.height - 8);
   AColor::Line(dc,
                rect.x + 2, rect.y + rect.height - 8,
                rect.x + 6, rect.y + rect.height - 4);
   AColor::Line(dc,
                rect.x + 2, rect.y + rect.height - 8,
                rect.x + 6, rect.y + rect.height - 12);
}

void TrackArtist::DrawWaveformBackground(wxDC &dc, int leftOffset, const wxRect &rect,
                                         const double env[],
                                         float zoomMin, float zoomMax,
                                         int zeroLevelYCoordinate,
                                         bool dB, float dBRange,
                                         double t0, double t1,
                                         const ZoomInfo &zoomInfo)
{

   // Visually (one vertical slice of the waveform background, on its side;
   // the "*" is the actual waveform background we're drawing
   //
   //1.0                              0.0                             -1.0
   // |--------------------------------|--------------------------------|
   //      ***************                           ***************
   //      |             |                           |             |
   //    maxtop        maxbot                      mintop        minbot

   int h = rect.height;
   int halfHeight = wxMax(h / 2, 1);
   int maxtop, lmaxtop = 0;
   int mintop, lmintop = 0;
   int maxbot, lmaxbot = 0;
   int minbot, lminbot = 0;
   bool sel, lsel = false;
   int xx, lx = 0;
   int l, w;

   dc.SetPen(*wxTRANSPARENT_PEN);
   dc.SetBrush(blankBrush);
   dc.DrawRectangle(rect);

   double time = zoomInfo.PositionToTime(0, -leftOffset), nextTime;
   for (xx = 0; xx < rect.width; ++xx, time = nextTime) {
      nextTime = zoomInfo.PositionToTime(xx + 1, -leftOffset);
      // First we compute the truncated shape of the waveform background.
      // If drawEnvelope is true, then we compute the lower border of the
      // envelope.

      maxtop = GetWaveYPos(env[xx], zoomMin, zoomMax,
                               h, dB, true, dBRange, true);
      maxbot = GetWaveYPos(env[xx], zoomMin, zoomMax,
                               h, dB, false, dBRange, true);

      mintop = GetWaveYPos(-env[xx], zoomMin, zoomMax,
                               h, dB, false, dBRange, true);
      minbot = GetWaveYPos(-env[xx], zoomMin, zoomMax,
                               h, dB, true, dBRange, true);

      // Make sure it's odd so that a that max and min mirror each other
      mintop +=1;
      minbot +=1;

      maxbot = halfHeight;
      mintop = halfHeight;

      // We don't draw selection color for sync-lock selected tracks.
      sel = (t0 <= time && nextTime < t1);

      if (lmaxtop == maxtop &&
          lmintop == mintop &&
          lmaxbot == maxbot &&
          lminbot == minbot &&
          lsel == sel) {
         continue;
      }

      dc.SetBrush(lsel ? selectedBrush : unselectedBrush);

      l = rect.x + lx;
      w = xx - lx;
      if (lmaxbot < lmintop - 1) {
         dc.DrawRectangle(l, rect.y + lmaxtop, w, lmaxbot - lmaxtop);
         dc.DrawRectangle(l, rect.y + lmintop, w, lminbot - lmintop);
      }
      else {
         dc.DrawRectangle(l, rect.y + lmaxtop, w, lminbot - lmaxtop);
      }

      lmaxtop = maxtop;
      lmintop = mintop;
      lmaxbot = maxbot;
      lminbot = minbot;
      lsel = sel;
      lx = xx;
   }

   dc.SetBrush(lsel ? selectedBrush : unselectedBrush);
   l = rect.x + lx;
   w = xx - lx;
   if (lmaxbot < lmintop - 1) {
      dc.DrawRectangle(l, rect.y + lmaxtop, w, lmaxbot - lmaxtop);
      dc.DrawRectangle(l, rect.y + lmintop, w, lminbot - lmintop);
   }
   else {
      dc.DrawRectangle(l, rect.y + lmaxtop, w, lminbot - lmaxtop);
   }

   //OK, the display bounds are between min and max, which
   //is spread across rect.height.  Draw the line at the proper place.
   if (zeroLevelYCoordinate >= rect.GetTop() &&
       zeroLevelYCoordinate <= rect.GetBottom()) {
      dc.SetPen(*wxBLACK_PEN);
      AColor::Line(dc, rect.x, zeroLevelYCoordinate,
                   rect.x + rect.width, zeroLevelYCoordinate);
   }
}


void TrackArtist::DrawMinMaxRMS(wxDC &dc, const wxRect & rect, const double env[],
   float zoomMin, float zoomMax,
   bool dB, float dBRange,
   const float *min, const float *max, const float *rms, const int *bl,
   bool /* showProgress */, bool muted)
{
   // Display a line representing the
   // min and max of the samples in this region
   int lasth1 = std::numeric_limits<int>::max();
   int lasth2 = std::numeric_limits<int>::min();
   int h1;
   int h2;
   ArrayOf<int> r1{ size_t(rect.width) };
   ArrayOf<int> r2{ size_t(rect.width) };
   ArrayOf<int> clipped;
   int clipcnt = 0;

   if (mShowClipping) {
      clipped.reinit( size_t(rect.width) );
   }

   long pixAnimOffset = (long)fabs((double)(wxDateTime::Now().GetTicks() * -10)) +
      wxDateTime::Now().GetMillisecond() / 100; //10 pixels a second

   bool drawStripes = true;
   bool drawWaveform = true;

   dc.SetPen(muted ? muteSamplePen : samplePen);
   for (int x0 = 0; x0 < rect.width; ++x0) {
      int xx = rect.x + x0;
      double v;
      v = min[x0] * env[x0];
      if (clipped && mShowClipping && (v <= -MAX_AUDIO))
      {
         if (clipcnt == 0 || clipped[clipcnt - 1] != xx) {
            clipped[clipcnt++] = xx;
         }
      }
      h1 = GetWaveYPos(v, zoomMin, zoomMax,
                       rect.height, dB, true, dBRange, true);

      v = max[x0] * env[x0];
      if (clipped && mShowClipping && (v >= MAX_AUDIO))
      {
         if (clipcnt == 0 || clipped[clipcnt - 1] != xx) {
            clipped[clipcnt++] = xx;
         }
      }
      h2 = GetWaveYPos(v, zoomMin, zoomMax,
                       rect.height, dB, true, dBRange, true);

      // JKC: This adjustment to h1 and h2 ensures that the drawn
      // waveform is continuous.
      if (x0 > 0) {
         if (h1 < lasth2) {
            h1 = lasth2 - 1;
         }
         if (h2 > lasth1) {
            h2 = lasth1 + 1;
         }
      }
      lasth1 = h1;
      lasth2 = h2;

      r1[x0] = GetWaveYPos(-rms[x0] * env[x0], zoomMin, zoomMax,
                          rect.height, dB, true, dBRange, true);
      r2[x0] = GetWaveYPos(rms[x0] * env[x0], zoomMin, zoomMax,
                          rect.height, dB, true, dBRange, true);
      // Make sure the rms isn't larger than the waveform min/max
      if (r1[x0] > h1 - 1) {
         r1[x0] = h1 - 1;
      }
      if (r2[x0] < h2 + 1) {
         r2[x0] = h2 + 1;
      }
      if (r2[x0] > r1[x0]) {
         r2[x0] = r1[x0];
      }

      if (bl[x0] <= -1) {
         if (drawStripes) {
            // TODO:unify with buffer drawing.
            dc.SetPen((bl[x0] % 2) ? muteSamplePen : samplePen);
            for (int yy = 0; yy < rect.height / 25 + 1; ++yy) {
               // we are drawing over the buffer, but I think DrawLine takes care of this.
               AColor::Line(dc,
                            xx,
                            rect.y + 25 * yy + (x0 /*+pixAnimOffset*/) % 25,
                            xx,
                            rect.y + 25 * yy + (x0 /*+pixAnimOffset*/) % 25 + 6); //take the min so we don't draw past the edge
            }
         }

         // draw a dummy waveform - some kind of sinusoid.  We want to animate it so the user knows it's a dummy.  Use the second's unit of a get time function.
         // Lets use a triangle wave for now since it's easier - I don't want to use sin() or make a wavetable just for this.
         if (drawWaveform) {
            int triX;
            dc.SetPen(samplePen);
            triX = fabs((double)((x0 + pixAnimOffset) % (2 * rect.height)) - rect.height) + rect.height;
            for (int yy = 0; yy < rect.height; ++yy) {
               if ((yy + triX) % rect.height == 0) {
                  dc.DrawPoint(xx, rect.y + yy);
               }
            }
         }

         // Restore the pen for remaining pixel columns!
         dc.SetPen(muted ? muteSamplePen : samplePen);
      }
      else {
         AColor::Line(dc, xx, rect.y + h2, xx, rect.y + h1);
      }
   }

   // Stroke rms over the min-max
   dc.SetPen(muted ? muteRmsPen : rmsPen);
   for (int x0 = 0; x0 < rect.width; ++x0) {
      int xx = rect.x + x0;
      if (bl[x0] <= -1) {
      }
      else if (r1[x0] != r2[x0]) {
         AColor::Line(dc, xx, rect.y + r2[x0], xx, rect.y + r1[x0]);
      }
   }

   // Draw the clipping lines
   if (clipcnt) {
      dc.SetPen(muted ? muteClippedPen : clippedPen);
      while (--clipcnt >= 0) {
         int xx = clipped[clipcnt];
         AColor::Line(dc, xx, rect.y, xx, rect.y + rect.height);
      }
   }
}

void TrackArtist::DrawIndividualSamples(wxDC &dc, int leftOffset, const wxRect &rect,
                                        float zoomMin, float zoomMax,
                                        bool dB, float dBRange,
                                        const WaveClip *clip,
                                        const ZoomInfo &zoomInfo,
                                        bool showPoints, bool muted,
                                        bool highlight)
{
   const double toffset = clip->GetOffset();
   double rate = clip->GetRate();
   const double t0 = std::max(0.0, zoomInfo.PositionToTime(0, -leftOffset) - toffset);
   const auto s0 = sampleCount(floor(t0 * rate));
   const auto snSamples = clip->GetNumSamples();
   if (s0 > snSamples)
      return;

   const double t1 = zoomInfo.PositionToTime(rect.width - 1, -leftOffset) - toffset;
   const auto s1 = sampleCount(ceil(t1 * rate));

   // Assume size_t will not overflow, else we wouldn't be here drawing the
   // few individual samples
   auto slen = std::min(snSamples - s0, s1 - s0 + 1).as_size_t();

   if (slen <= 0)
      return;

   Floats buffer{ size_t(slen) };
   clip->GetSamples((samplePtr)buffer.get(), floatSample, s0, slen,
                    // Suppress exceptions in this drawing operation:
                    false);

   ArrayOf<int> xpos{ size_t(slen) };
   ArrayOf<int> ypos{ size_t(slen) };
   ArrayOf<int> clipped;
   int clipcnt = 0;

   if (mShowClipping)
      clipped.reinit( size_t(slen) );

   auto &pen = highlight ? AColor::uglyPen : muted ? muteSamplePen : samplePen;
   dc.SetPen( pen );

   for (decltype(slen) s = 0; s < slen; s++) {
      const double time = toffset + (s + s0).as_double() / rate;
      const int xx = // An offset into the rectangle rect
         std::max(-10000, std::min(10000,
            (int)(zoomInfo.TimeToPosition(time, -leftOffset))));
      xpos[s] = xx;

      // Calculate sample as it would be rendered, so quantize time
      double value =
         clip->GetEnvelope()->GetValue( time, 1.0 / clip->GetRate() );
      const double tt = buffer[s] * value;

      if (clipped && mShowClipping && ((tt <= -MAX_AUDIO) || (tt >= MAX_AUDIO)))
         clipped[clipcnt++] = xx;
      ypos[s] =
         std::max(-1,
            std::min(rect.height,
               GetWaveYPos(tt, zoomMin, zoomMax,
                                  rect.height, dB, true, dBRange, false)));
   }


   if (showPoints) {
      // Draw points where spacing is enough
      const int tickSize = 3; // Bigger ellipses when draggable.
      wxRect pr;
      pr.width = tickSize;
      pr.height = tickSize;
      //different colour when draggable.
      auto &brush = highlight
         ? AColor::uglyBrush
         : sampleBrush;
      dc.SetBrush( brush );
      for (decltype(slen) s = 0; s < slen; s++) {
         if (ypos[s] >= 0 && ypos[s] < rect.height) {
            pr.x = rect.x + xpos[s] - tickSize/2;
            pr.y = rect.y + ypos[s] - tickSize/2;
            dc.DrawEllipse(pr);
         }
      }
   }

   if (showPoints && (mSampleDisplay == (int) WaveTrack::StemPlot)) {
      // Draw vertical lines
      int yZero = GetWaveYPos(0.0, zoomMin, zoomMax, rect.height, dB, true, dBRange, false);
      yZero = rect.y + std::max(-1, std::min(rect.height, yZero));
      for (decltype(slen) s = 0; s < slen; s++) {
         AColor::Line(dc,
                     rect.x + xpos[s], rect.y + ypos[s],
                     rect.x + xpos[s], yZero);
      }
   }
   else {
      // Connect samples with straight lines
      for (decltype(slen) s = 0; s < slen - 1; s++) {
         AColor::Line(dc,
                     rect.x + xpos[s], rect.y + ypos[s],
                     rect.x + xpos[s + 1], rect.y + ypos[s + 1]);
      }
   }

   // Draw clipping
   if (clipcnt) {
      dc.SetPen(muted ? muteClippedPen : clippedPen);
      while (--clipcnt >= 0) {
         auto s = clipped[clipcnt];
         AColor::Line(dc, rect.x + s, rect.y, rect.x + s, rect.y + rect.height);
      }
   }
}

#include "tracks/playabletrack/wavetrack/ui/CutlineHandle.h"
void TrackArtist::DrawWaveform(TrackPanelDrawingContext &context,
                               const WaveTrack *track,
                               const wxRect & rect,
                               const SelectedRegion &selectedRegion,
                               const ZoomInfo &zoomInfo,
                               bool muted)
{
   auto &dc = context.dc;

   const bool dB = !track->GetWaveformSettings().isLinear();

   DrawBackgroundWithSelection(&dc, rect, track, blankSelectedBrush, blankBrush,
         selectedRegion, zoomInfo);

   for (const auto &clip: track->GetClips())
      DrawClipWaveform(context, track, clip.get(), rect, selectedRegion, zoomInfo,
                       dB, muted);

   // Update cache for locations, e.g. cutlines and merge points
   track->UpdateLocationsCache();

   for (const auto loc : track->GetCachedLocations()) {
      bool highlight = false;
      const int xx = zoomInfo.TimeToPosition(loc.pos);
      if (xx >= 0 && xx < rect.width) {
         dc.SetPen( highlight ? AColor::uglyPen : *wxGREY_PEN );
         AColor::Line(dc, (int) (rect.x + xx - 1), rect.y, (int) (rect.x + xx - 1), rect.y + rect.height);
         if (loc.typ == WaveTrackLocation::locationCutLine) {
            dc.SetPen( highlight ? AColor::uglyPen : *wxRED_PEN );
         }
         else {
            dc.SetPen(highlight ? AColor::uglyPen : *wxBLACK_PEN);
         }
         AColor::Line(dc, (int) (rect.x + xx), rect.y, (int) (rect.x + xx), rect.y + rect.height);
         dc.SetPen( highlight ? AColor::uglyPen : *wxGREY_PEN );
         AColor::Line(dc, (int) (rect.x + xx + 1), rect.y, (int) (rect.x + xx + 1), rect.y + rect.height);
      }
   }
}

namespace {
struct ClipParameters
{
   // Do a bunch of calculations common to waveform and spectrum drawing.
   ClipParameters
      (bool spectrum, const WaveTrack *track, const WaveClip *clip, const wxRect &rect,
      const SelectedRegion &selectedRegion, const ZoomInfo &zoomInfo)
   {
      tOffset = clip->GetOffset();
      rate = clip->GetRate();

      h = zoomInfo.PositionToTime(0, 0
         , true
      );
      h1 = zoomInfo.PositionToTime(rect.width, 0
         , true
      );

      double sel0 = selectedRegion.t0();    //left selection bound
      double sel1 = selectedRegion.t1();    //right selection bound

      //If the track isn't selected, make the selection empty
      if (!track->GetSelected() && spectrum) { // PRL: why was there a difference for spectrum?
         sel0 = sel1 = 0.0;
      }

      const double trackLen = clip->GetEndTime() - clip->GetStartTime();

      tpre = h - tOffset;                 // offset corrected time of
      //  left edge of display
      tpost = h1 - tOffset;               // offset corrected time of
      //  right edge of display

      const double sps = 1. / rate;            //seconds-per-sample

      // Determine whether we should show individual samples
      // or draw circular points as well
      averagePixelsPerSample = rect.width / (rate * (h1 - h));
      showIndividualSamples = averagePixelsPerSample > 0.5;

      // Calculate actual selection bounds so that t0 > 0 and t1 < the
      // end of the track
      t0 = (tpre >= 0.0 ? tpre : 0.0);
      t1 = (tpost < trackLen - sps * .99 ? tpost : trackLen - sps * .99);
      if (showIndividualSamples) {
         // adjustment so that the last circular point doesn't appear
         // to be hanging off the end
         t1 += 2. / (averagePixelsPerSample * rate);
      }

      // Make sure t1 (the right bound) is greater than 0
      if (t1 < 0.0) {
         t1 = 0.0;
      }

      // Make sure t1 is greater than t0
      if (t0 > t1) {
         t0 = t1;
      }

      // Use the WaveTrack method to show what is selected and 'should' be copied, pasted etc.
      ssel0 = std::max(sampleCount(0), spectrum
         ? sampleCount((sel0 - tOffset) * rate + .99) // PRL: why?
         : track->TimeToLongSamples(sel0 - tOffset)
      );
      ssel1 = std::max(sampleCount(0), spectrum
         ? sampleCount((sel1 - tOffset) * rate + .99) // PRL: why?
         : track->TimeToLongSamples(sel1 - tOffset)
      );

      //trim selection so that it only contains the actual samples
      if (ssel0 != ssel1 && ssel1 > (sampleCount)(0.5 + trackLen * rate)) {
         ssel1 = sampleCount( 0.5 + trackLen * rate );
      }

      // The variable "hiddenMid" will be the rectangle containing the
      // actual waveform, as opposed to any blank area before
      // or after the track, as it would appear without the fisheye.
      hiddenMid = rect;

      // If the left edge of the track is to the right of the left
      // edge of the display, then there's some unused area to the
      // left of the track.  Reduce the "hiddenMid"
      hiddenLeftOffset = 0;
      if (tpre < 0) {
         // Fix Bug #1296 caused by premature conversion to (int).
         wxInt64 time64 = zoomInfo.TimeToPosition(tOffset, 0 , true);
         if( time64 < 0 )
            time64 = 0;
         hiddenLeftOffset = (time64 < rect.width) ? (int)time64 : rect.width;

         hiddenMid.x += hiddenLeftOffset;
         hiddenMid.width -= hiddenLeftOffset;
      }

      // If the right edge of the track is to the left of the the right
      // edge of the display, then there's some unused area to the right
      // of the track.  Reduce the "hiddenMid" rect by the
      // size of the blank area.
      if (tpost > t1) {
         wxInt64 time64 = zoomInfo.TimeToPosition(tOffset+t1, 0 , true);
         if( time64 < 0 )
            time64 = 0;
         const int hiddenRightOffset = (time64 < rect.width) ? (int)time64 : rect.width;

         hiddenMid.width = std::max(0, hiddenRightOffset - hiddenLeftOffset);
      }
      // The variable "mid" will be the rectangle containing the
      // actual waveform, as distorted by the fisheye,
      // as opposed to any blank area before or after the track.
      mid = rect;

      // If the left edge of the track is to the right of the left
      // edge of the display, then there's some unused area to the
      // left of the track.  Reduce the "mid"
      leftOffset = 0;
      if (tpre < 0) {
         wxInt64 time64 = zoomInfo.TimeToPosition(tOffset, 0 , false);
         if( time64 < 0 )
            time64 = 0;
         leftOffset = (time64 < rect.width) ? (int)time64 : rect.width;

         mid.x += leftOffset;
         mid.width -= leftOffset;
      }

      // If the right edge of the track is to the left of the the right
      // edge of the display, then there's some unused area to the right
      // of the track.  Reduce the "mid" rect by the
      // size of the blank area.
      if (tpost > t1) {
         wxInt64 time64 = zoomInfo.TimeToPosition(tOffset+t1, 0 , false);
         if( time64 < 0 )
            time64 = 0;
         const int distortedRightOffset = (time64 < rect.width) ? (int)time64 : rect.width;

         mid.width = std::max(0, distortedRightOffset - leftOffset);
      }
   }

   double tOffset;
   double rate;
   double h; // absolute time of left edge of display
   double tpre; // offset corrected time of left edge of display
   double h1;
   double tpost; // offset corrected time of right edge of display

   // Calculate actual selection bounds so that t0 > 0 and t1 < the
   // end of the track
   double t0;
   double t1;

   double averagePixelsPerSample;
   bool showIndividualSamples;

   sampleCount ssel0;
   sampleCount ssel1;

   wxRect hiddenMid;
   int hiddenLeftOffset;

   wxRect mid;
   int leftOffset;
};
}

#ifdef __GNUC__
#define CONST
#else
#define CONST const
#endif

namespace {
struct WavePortion {
   wxRect rect;
   CONST double averageZoom;
   CONST bool inFisheye;
   WavePortion(int x, int y, int w, int h, double zoom, bool i)
      : rect(x, y, w, h), averageZoom(zoom), inFisheye(i)
   {}
};

void FindWavePortions
   (std::vector<WavePortion> &portions, const wxRect &rect, const ZoomInfo &zoomInfo,
    const ClipParameters &params)
{
   // If there is no fisheye, then only one rectangle has nonzero width.
   // If there is a fisheye, make rectangles for before and after
   // (except when they are squeezed to zero width), and at least one for inside
   // the fisheye.

   ZoomInfo::Intervals intervals;
   zoomInfo.FindIntervals(params.rate, intervals, rect.width, rect.x);
   ZoomInfo::Intervals::const_iterator it = intervals.begin(), end = intervals.end(), prev;
   wxASSERT(it != end && it->position == rect.x);
   const int rightmost = rect.x + rect.width;
   for (int left = rect.x; left < rightmost;) {
      while (it != end && it->position <= left)
         prev = it++;
      if (it == end)
         break;
      const int right = std::max(left, (int)(it->position));
      const int width = right - left;
      if (width > 0)
         portions.push_back(
            WavePortion(left, rect.y, width, rect.height,
                        prev->averageZoom, prev->inFisheye)
         );
      left = right;
   }
}
}

void TrackArtist::DrawClipWaveform(TrackPanelDrawingContext &context,
                                   const WaveTrack *track,
                                   const WaveClip *clip,
                                   const wxRect & rect,
                                   const SelectedRegion &selectedRegion,
                                   const ZoomInfo &zoomInfo,
                                   bool dB,
                                   bool muted)
{
   auto &dc = context.dc;
#ifdef PROFILE_WAVEFORM
   Profiler profiler;
#endif

   const ClipParameters params(false, track, clip, rect, selectedRegion, zoomInfo);
   const wxRect &hiddenMid = params.hiddenMid;
   // The "hiddenMid" rect contains the part of the display actually
   // containing the waveform, as it appears without the fisheye.  If it's empty, we're done.
   if (hiddenMid.width <= 0) {
      return;
   }

   const double &t0 = params.t0;
   const double &tOffset = params.tOffset;
   const double &h = params.h;
   const double &tpre = params.tpre;
   const double &tpost = params.tpost;
   const double &t1 = params.t1;
   const double &averagePixelsPerSample = params.averagePixelsPerSample;
   const double &rate = params.rate;
   double leftOffset = params.leftOffset;
   const wxRect &mid = params.mid;

   const float dBRange = track->GetWaveformSettings().dBRange;

   dc.SetPen(*wxTRANSPARENT_PEN);
   int iColorIndex = clip->GetColourIndex();
   SetColours( iColorIndex );

   // If we get to this point, the clip is actually visible on the
   // screen, so remember the display rectangle.
   clip->SetDisplayRect(hiddenMid);

   // The bounds (controlled by vertical zooming; -1.0...1.0
   // by default)
   float zoomMin, zoomMax;
   track->GetDisplayBounds(&zoomMin, &zoomMax);

   std::vector<double> vEnv(mid.width);
   double *const env = &vEnv[0];
   clip->GetEnvelope()->GetValues
      ( tOffset,

        // PRL: change back to make envelope evaluate only at sample times
        // and then interpolate the display
        0, // 1.0 / rate,

        env, mid.width, leftOffset, zoomInfo );

   // Draw the background of the track, outlining the shape of
   // the envelope and using a colored pen for the selected
   // part of the waveform
   {
      double t0, t1;
      if (track->GetSelected()) {
         t0 = track->LongSamplesToTime(track->TimeToLongSamples(selectedRegion.t0())),
            t1 = track->LongSamplesToTime(track->TimeToLongSamples(selectedRegion.t1()));
      }
      else
         t0 = t1 = 0.0;
      DrawWaveformBackground(dc, leftOffset, mid,
         env,
         zoomMin, zoomMax,
         track->ZeroLevelYCoordinate(mid),
         dB, dBRange,
         t0, t1, zoomInfo);
   }

   WaveDisplay display(hiddenMid.width);
   bool isLoadingOD = false;//true if loading on demand block in sequence.

   const double pps =
      averagePixelsPerSample * rate;

   // For each portion separately, we will decide to draw
   // it as min/max/rms or as individual samples.
   std::vector<WavePortion> portions;
   FindWavePortions(portions, rect, zoomInfo, params);
   const unsigned nPortions = portions.size();

   // Require at least 1/2 pixel per sample for drawing individual samples.
   const double threshold1 = 0.5 * rate;
   // Require at least 3 pixels per sample for drawing the draggable points.
   const double threshold2 = 3 * rate;

   {
      bool showIndividualSamples = false;
      for (unsigned ii = 0; !showIndividualSamples && ii < nPortions; ++ii) {
         const WavePortion &portion = portions[ii];
         showIndividualSamples =
            !portion.inFisheye && portion.averageZoom > threshold1;
      }

      if (!showIndividualSamples) {
         // The WaveClip class handles the details of computing the shape
         // of the waveform.  The only way GetWaveDisplay will fail is if
         // there's a serious error, like some of the waveform data can't
         // be loaded.  So if the function returns false, we can just exit.

         // Note that we compute the full width display even if there is a
         // fisheye hiding part of it, because of the caching.  If the
         // fisheye moves over the background, there is then less to do when
         // redrawing.

         if (!clip->GetWaveDisplay(display,
            t0, pps, isLoadingOD))
            return;
      }
   }

   for (unsigned ii = 0; ii < nPortions; ++ii) {
      WavePortion &portion = portions[ii];
      const bool showIndividualSamples = portion.averageZoom > threshold1;
      const bool showPoints = portion.averageZoom > threshold2;
      wxRect& rect = portion.rect;
      rect.Intersect(mid);
      wxASSERT(rect.width >= 0);

      float *useMin = 0, *useMax = 0, *useRms = 0;
      int *useBl = 0;
      WaveDisplay fisheyeDisplay(rect.width);
      int skipped = 0, skippedLeft = 0, skippedRight = 0;
      if (portion.inFisheye) {
         if (!showIndividualSamples) {
            fisheyeDisplay.Allocate();
            const auto numSamples = clip->GetNumSamples();
            // Get wave display data for different magnification
            int jj = 0;
            for (; jj < rect.width; ++jj) {
               const double time =
                  zoomInfo.PositionToTime(jj, -leftOffset) - tOffset;
               const auto sample = (sampleCount)floor(time * rate + 0.5);
               if (sample < 0) {
                  ++rect.x;
                  ++skippedLeft;
                  continue;
               }
               if (sample >= numSamples)
                  break;
               fisheyeDisplay.where[jj - skippedLeft] = sample;
            }

            skippedRight = rect.width - jj;
            skipped = skippedRight + skippedLeft;
            rect.width -= skipped;

            // where needs a sentinel
            if (jj > 0)
               fisheyeDisplay.where[jj - skippedLeft] =
               1 + fisheyeDisplay.where[jj - skippedLeft - 1];
            fisheyeDisplay.width -= skipped;
            // Get a wave display for the fisheye, uncached.
            if (rect.width > 0)
               if (!clip->GetWaveDisplay(
                     fisheyeDisplay, t0, -1.0, // ignored
                     isLoadingOD))
                  continue; // serious error.  just don't draw??
            useMin = fisheyeDisplay.min;
            useMax = fisheyeDisplay.max;
            useRms = fisheyeDisplay.rms;
            useBl = fisheyeDisplay.bl;
         }
      }
      else {
         const int pos = leftOffset - params.hiddenLeftOffset;
         useMin = display.min + pos;
         useMax = display.max + pos;
         useRms = display.rms + pos;
         useBl = display.bl + pos;
      }

      leftOffset += skippedLeft;

      if (rect.width > 0) {
         if (!showIndividualSamples) {
            std::vector<double> vEnv2(rect.width);
            double *const env2 = &vEnv2[0];
            clip->GetEnvelope()->GetValues
               ( tOffset,

                 // PRL: change back to make envelope evaluate only at sample times
                 // and then interpolate the display
                 0, // 1.0 / rate,

                 env2, rect.width, leftOffset, zoomInfo );
            DrawMinMaxRMS(dc, rect, env2,
               zoomMin, zoomMax,
               dB, dBRange,
               useMin, useMax, useRms, useBl,
               isLoadingOD, muted);
         }
         else {
            bool highlight = false;
            DrawIndividualSamples(dc, leftOffset, rect, zoomMin, zoomMax,
               dB, dBRange,
               clip, zoomInfo,
               showPoints, muted, highlight);
         }
      }

      leftOffset += rect.width + skippedRight;
   }

   // Draw arrows on the left side if the track extends to the left of the
   // beginning of time.  :)
   if (h == 0.0 && tOffset < 0.0) {
      DrawNegativeOffsetTrackArrows(dc, rect);
   }

   // Draw clip edges
   dc.SetPen(*wxGREY_PEN);
   if (tpre < 0) {
      AColor::Line(dc,
                   mid.x - 1, mid.y,
                   mid.x - 1, mid.y + rect.height);
   }
   if (tpost > t1) {
      AColor::Line(dc,
                   mid.x + mid.width, mid.y,
                   mid.x + mid.width, mid.y + rect.height);
   }
}

static inline float findValue
(const float *spectrum, float bin0, float bin1, unsigned nBins,
 bool autocorrelation, int gain, int range)
{
   float value;

   // Maximum method, and no apportionment of any single bins over multiple pixel rows
   // See Bug971
   int index, limitIndex;
   if (autocorrelation) {
      // bin = 2 * nBins / (nBins - 1 - array_index);
      // Solve for index
      index = std::max(0.0f, std::min(float(nBins - 1),
         (nBins - 1) - (2 * nBins) / (std::max(1.0f, bin0))
      ));
      limitIndex = std::max(0.0f, std::min(float(nBins - 1),
         (nBins - 1) - (2 * nBins) / (std::max(1.0f, bin1))
      ));
   }
   else {
      index = std::min<int>(nBins - 1, (int)(floor(0.5 + bin0)));
      limitIndex = std::min<int>(nBins, (int)(floor(0.5 + bin1)));
   }
   value = spectrum[index];
   while (++index < limitIndex)
      value = std::max(value, spectrum[index]);
   if (!autocorrelation) {
      // Last step converts dB to a 0.0-1.0 range
      value = (value + range + gain) / (double)range;
   }
   value = std::min(1.0f, std::max(0.0f, value));
   return value;
}

void TrackArtist::UpdatePrefs()
{
   mdBrange = gPrefs->Read(ENV_DB_KEY, mdBrange);
   mShowClipping = gPrefs->Read(wxT("/GUI/ShowClipping"), mShowClipping);
   gPrefs->Read(wxT("/GUI/SampleView"), &mSampleDisplay, 1);
   SetColours(0);
}

void TrackArtist::DrawBackgroundWithSelection(wxDC *dc, const wxRect &rect,
   const Track *track, wxBrush &selBrush, wxBrush &unselBrush,
   const SelectedRegion &selectedRegion, const ZoomInfo &zoomInfo)
{
   //MM: Draw background. We should optimize that a bit more.
   const double sel0 = selectedRegion.t0();
   const double sel1 = selectedRegion.t1();

   dc->SetPen(*wxTRANSPARENT_PEN);
   if (track->GetSelected())
   {
      // Rectangles before, within, after the selction
      wxRect before = rect;
      wxRect within = rect;
      wxRect after = rect;

      before.width = (int)(zoomInfo.TimeToPosition(sel0) );
      if (before.GetRight() > rect.GetRight()) {
         before.width = rect.width;
      }

      if (before.width > 0) {
         dc->SetBrush(unselBrush);
         dc->DrawRectangle(before);

         within.x = 1 + before.GetRight();
      }
      within.width = rect.x + (int)(zoomInfo.TimeToPosition(sel1) ) - within.x -1;

      if (within.GetRight() > rect.GetRight()) {
         within.width = 1 + rect.GetRight() - within.x;
      }

      if (within.width > 0) {
         if (track->GetSelected()) {
            dc->SetBrush(selBrush);
            dc->DrawRectangle(within);
         }
         else {
            // Per condition above, track must be sync-lock selected
            dc->SetBrush(unselBrush);
            dc->DrawRectangle(within);
         }

         after.x = 1 + within.GetRight();
      }
      else {
         // `within` not drawn; start where it would have gone
         after.x = within.x;
      }

      after.width = 1 + rect.GetRight() - after.x;
      if (after.width > 0) {
         dc->SetBrush(unselBrush);
         dc->DrawRectangle(after);
      }
   }
   else
   {
      // Track not selected; just draw background
      dc->SetBrush(unselBrush);
      dc->DrawRectangle(rect);
   }
}

