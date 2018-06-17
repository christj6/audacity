/**********************************************************************

  Audacity: A Digital Audio Editor

  TrackArtist.h

  Dominic Mazzoni

  This singleton class handles the actual rendering of WaveTracks
  (both waveforms and spectra), NoteTracks, and LabelTracks.

  It's actually a little harder than it looks, because for
  waveforms at least it needs to cache the samples that are
  currently on-screen.

**********************************************************************/

#ifndef __AUDACITY_TRACKARTIST__
#define __AUDACITY_TRACKARTIST__

#include "MemoryX.h"
#include <wx/brush.h>
#include <wx/pen.h>
#include "Experimental.h"
#include "audacity/Types.h"

class wxDC;
class wxRect;
class wxHashTable;

class Track;
class WaveDisplay;
class WaveTrack;
class WaveTrackCache;
class WaveClip;
class TrackList;
class Ruler;
class SelectedRegion;
class ZoomInfo;

struct TrackPanelDrawingContext;

#ifndef uchar
typedef unsigned char uchar;
#endif

class AUDACITY_DLL_API TrackArtist {

 public:
   TrackArtist();
   ~TrackArtist();

   void SetColours(int iColorIndex);
   void DrawTracks(TrackPanelDrawingContext &context,
                   TrackList *tracks, Track *start,
                   const wxRegion & reg,
                   const wxRect & rect, const wxRect & clip,
                   const SelectedRegion &selectedRegion, const ZoomInfo &zoomInfo);

   void DrawTrack(TrackPanelDrawingContext &context,
                  const Track *t,
                  const wxRect & rect,
                  const SelectedRegion &selectedRegion, const ZoomInfo &zoomInfo,
                  bool hasSolo);

   void DrawVRuler(TrackPanelDrawingContext &context,
                   const Track *t, wxRect & rect);

   void UpdateVRuler(const Track *t, wxRect & rect);

   void SetMargins(int left, int top, int right, int bottom);

   void UpdatePrefs();

   void SetBackgroundBrushes(wxBrush unselectedBrush, wxBrush selectedBrush,
                             wxPen unselectedPen, wxPen selectedPen) {
     this->unselectedBrush = unselectedBrush;
     this->selectedBrush = selectedBrush;
     this->unselectedPen = unselectedPen;
     this->selectedPen = selectedPen;
   }

   // Helper: draws background with selection rect
   static void DrawBackgroundWithSelection(wxDC *dc, const wxRect &rect,
         const Track *track, wxBrush &selBrush, wxBrush &unselBrush,
         const SelectedRegion &selectedRegion, const ZoomInfo &zoomInfo);

 private:

   //
   // Lower-level drawing functions
   //

   void DrawWaveform(TrackPanelDrawingContext &context,
                     const WaveTrack *track,
                     const wxRect & rect,
                     const SelectedRegion &selectedRegion, const ZoomInfo &zoomInfo,
                     bool muted);

   void DrawClipWaveform(TrackPanelDrawingContext &context,
                         const WaveTrack *track, const WaveClip *clip,
                         const wxRect & rect,
                         const SelectedRegion &selectedRegion, const ZoomInfo &zoomInfo,
                         bool dB, bool muted);

   // Waveform utility functions

   void DrawWaveformBackground(wxDC & dc, int leftOffset, const wxRect &rect,
                               const double env[],
                               float zoomMin, float zoomMax,
                               int zeroLevelYCoordinate,
                               bool dB, float dBRange,
                               double t0, double t1, const ZoomInfo &zoomInfo);
   void DrawMinMaxRMS(wxDC &dc, const wxRect & rect, const double env[],
                      float zoomMin, float zoomMax,
                      bool dB, float dBRange,
                      const float *min, const float *max, const float *rms, const int *bl,
                      bool /* showProgress */, bool muted);
   void DrawIndividualSamples(wxDC & dc, int leftOffset, const wxRect & rect,
                              float zoomMin, float zoomMax,
                              bool dB, float dBRange,
                              const WaveClip *clip,
                              const ZoomInfo &zoomInfo,
                              bool showPoints, bool muted,
                              bool highlight);

   void DrawNegativeOffsetTrackArrows(wxDC & dc, const wxRect & rect);

   // Preference values
   float mdBrange;            // "/GUI/EnvdBRange"
   int  mSampleDisplay;       // "/GUI/SampleView"
   bool mbShowTrackNameInWaveform;  // "/GUI/ShowTrackNameInWaveform"

   int mMarginLeft;
   int mMarginTop;
   int mMarginRight;
   int mMarginBottom;

   wxBrush blankBrush;
   wxBrush unselectedBrush;
   wxBrush selectedBrush;
   wxBrush sampleBrush;
   wxBrush selsampleBrush;
   wxBrush dragsampleBrush;// for samples which are draggable.
   wxBrush muteSampleBrush;
   wxBrush blankSelectedBrush;
   wxPen blankPen;
   wxPen unselectedPen;
   wxPen selectedPen;
   wxPen samplePen;
   wxPen rmsPen;
   wxPen muteRmsPen;
   wxPen selsamplePen;
   wxPen muteSamplePen;
   wxPen odProgressNotYetPen;
   wxPen odProgressDonePen;
   wxPen shadowPen;
   wxPen clippedPen;
   wxPen muteClippedPen;
   wxPen blankSelectedPen;

   std::unique_ptr<Ruler> vruler;
};

extern int GetWaveYPos(float value, float min, float max,
                       int height, bool dB, bool outer, float dBr,
                       bool clip);
extern float FromDB(float value, double dBRange);
extern float ValueOfPixel(int yy, int height, bool offset,
                          bool dB, double dBRange, float zoomMin, float zoomMax);

#endif                          // define __AUDACITY_TRACKARTIST__
