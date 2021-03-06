/**********************************************************************

  Audacity: A Digital Audio Editor

  WaveTrack.cpp

  Dominic Mazzoni

*******************************************************************//**

\class WaveTrack
\brief A Track that contains audio waveform data.

*//****************************************************************//**

\class WaveTrack::Location
\brief Used only by WaveTrack, a special way to hold location that
can accommodate merged regions.

*//****************************************************************//**

\class TrackFactory
\brief Used to create a WaveTrack..  Implementation
of the functions of this class are dispersed through the different
Track classes.

*//*******************************************************************/


#include "WaveTrack.h"
#include <wx/defs.h>
#include <wx/intl.h>
#include <wx/debug.h>

#include <float.h>
#include <math.h>
#include <algorithm>
#include "MemoryX.h"

#include "float_cast.h"

#include "Envelope.h"
#include "Sequence.h"

#include "Project.h"
#include "Internat.h"

#include "AudioIO.h"
#include "Prefs.h"

#include "ondemand/ODManager.h"

#include "prefs/TracksPrefs.h"
#include "prefs/WaveformPrefs.h"

#include "InconsistencyException.h"

#include "Experimental.h"

#include "TrackPanel.h" // for TrackInfo

using std::max;

WaveTrack::Holder TrackFactory::DuplicateWaveTrack(const WaveTrack &orig)
{
   return std::unique_ptr<WaveTrack>
   { static_cast<WaveTrack*>(orig.Duplicate().release()) };
}

WaveTrack::Holder TrackFactory::NewWaveTrack(sampleFormat format, double rate)
{
   return std::unique_ptr<WaveTrack>
   { safenew WaveTrack(mDirManager, format, rate) };
}

WaveTrack::WaveTrack(const std::shared_ptr<DirManager> &projDirManager, sampleFormat format, double rate) :
   PlayableTrack(projDirManager)
{
   if (format == (sampleFormat)0)
   {
      format = GetActiveProject()->GetDefaultFormat();
   }
   if (rate == 0)
   {
      rate = GetActiveProject()->GetRate();
   }

   // Force creation always:
   WaveformSettings &settings = GetIndependentWaveformSettings();

   mDisplay = FindDefaultViewMode();
   if (mDisplay == obsoleteWaveformDBDisplay) {
      mDisplay = Waveform;
      settings.scaleType = WaveformSettings::stLogarithmic;
   }

   mFormat = format;
   mRate = (int) rate;
   mPan = 0.0;
   mWaveColorIndex = 0;
   SetDefaultName(TracksPrefs::GetDefaultAudioTrackNamePreference());
   SetName(GetDefaultName());
   mDisplayMin = -1.0;
   mDisplayMax = 1.0;
   mLastScaleType = -1;
   mLastdBRange = -1;

   SetHeight( TrackInfo::DefaultWaveTrackHeight() );
}

WaveTrack::WaveTrack(const WaveTrack &orig):
   PlayableTrack(orig), mpWaveformSettings(orig.mpWaveformSettings 
      ? std::make_unique<WaveformSettings>(*orig.mpWaveformSettings)
      : nullptr)
{
   mLastScaleType = -1;
   mLastdBRange = -1;

   Init(orig);

   for (const auto &clip : orig.mClips)
      mClips.push_back
         ( make_movable<WaveClip>( *clip, mDirManager, true ) );
}

// Copy the track metadata but not the contents.
void WaveTrack::Init(const WaveTrack &orig)
{
   PlayableTrack::Init(orig);
   mFormat = orig.mFormat;
   mWaveColorIndex = orig.mWaveColorIndex;
   mRate = orig.mRate;
   mPan = orig.mPan;
   SetDefaultName(orig.GetDefaultName());
   SetName(orig.GetName());
   mDisplay = orig.mDisplay;
   mDisplayMin = orig.mDisplayMin;
   mDisplayMax = orig.mDisplayMax;
   mDisplayLocationsCache.clear();
}

void WaveTrack::Reinit(const WaveTrack &orig)
{
   Init(orig);

   {
      auto &settings = orig.mpWaveformSettings;
      if (settings)
         mpWaveformSettings = std::make_unique<WaveformSettings>(*settings);
      else
         mpWaveformSettings.reset();
   }

   this->SetOffset(orig.GetOffset());
}

void WaveTrack::Merge(const Track &orig)
{
   if (orig.GetKind() == Wave)
   {
      const WaveTrack &wt = static_cast<const WaveTrack&>(orig);
      mDisplay = wt.mDisplay;
      mPan     = wt.mPan;
      mDisplayMin = wt.mDisplayMin;
      mDisplayMax = wt.mDisplayMax;
      SetWaveformSettings
         (wt.mpWaveformSettings ? std::make_unique<WaveformSettings>(*wt.mpWaveformSettings) : nullptr);
   }
   PlayableTrack::Merge(orig);
}

WaveTrack::~WaveTrack()
{
   //Let the ODManager know this WaveTrack is disappearing.
   //Deschedules tasks associated with this track.
   if(ODManager::IsInstanceCreated())
      ODManager::Instance()->RemoveWaveTrack(this);
}

double WaveTrack::GetOffset() const
{
   return GetStartTime();
}

void WaveTrack::SetOffset(double o)
// NOFAIL-GUARANTEE
{
   double delta = o - GetOffset();

   for (const auto &clip : mClips)
      // assume NOFAIL-GUARANTEE
      clip->SetOffset(clip->GetOffset() + delta);

   mOffset = o;
}

int WaveTrack::GetChannel() const 
{
   if( mChannel != Track::MonoChannel )
      return mChannel; 
   auto pan = GetPan();
   if( pan < -0.99 )
      return Track::LeftChannel;
   if( pan >  0.99 )
      return Track::RightChannel;
   return mChannel;
}

//static
WaveTrack::WaveTrackDisplay WaveTrack::FindDefaultViewMode()
{
   // PRL:  Bugs 1043, 1044
   // 2.1.1 writes a NEW key for this preference, which got NEW values,
   // to avoid confusing version 2.1.0 if it reads the preference file afterwards.
   // Prefer the NEW preference key if it is present

   WaveTrack::WaveTrackDisplay viewMode;
   gPrefs->Read(wxT("/GUI/DefaultViewModeNew"), &viewMode, -1);

   // Default to the old key only if not, default the value if it's not there either
   wxASSERT(WaveTrack::MinDisplay >= 0);
   if (viewMode < 0) {
      int oldMode;
      gPrefs->Read(wxT("/GUI/DefaultViewMode"), &oldMode,
         (int)(WaveTrack::Waveform));
      viewMode = WaveTrack::ConvertLegacyDisplayValue(oldMode);
   }

   // Now future-proof 2.1.1 against a recurrence of this sort of bug!
   viewMode = WaveTrack::ValidateWaveTrackDisplay(viewMode);

   return viewMode;
}

// static
WaveTrack::WaveTrackDisplay
WaveTrack::ConvertLegacyDisplayValue(int oldValue)
{
   // Remap old values.
   enum OldValues {
      Waveform,
      WaveformDB,
   };

   WaveTrackDisplay newValue;
   switch (oldValue) {
   default:
   case Waveform:
      newValue = WaveTrack::Waveform; break;
   case WaveformDB:
      newValue = WaveTrack::obsoleteWaveformDBDisplay; break;
   }
   return newValue;
}

// static
WaveTrack::WaveTrackDisplay
WaveTrack::ValidateWaveTrackDisplay(WaveTrackDisplay display)
{
   switch (display) {
      // non-obsolete codes
   case Waveform:
   case obsoleteWaveformDBDisplay:
      return display;

      // codes out of bounds (from future prefs files?)
   default:
      return MinDisplay;
   }
}

void WaveTrack::SetLastScaleType() const
{
   mLastScaleType = GetWaveformSettings().scaleType;
}

void WaveTrack::SetLastdBRange() const
{
   mLastdBRange = GetWaveformSettings().dBRange;
}

void WaveTrack::GetDisplayBounds(float *min, float *max) const
{
   *min = mDisplayMin;
   *max = mDisplayMax;
}

void WaveTrack::SetDisplayBounds(float min, float max) const
{
   mDisplayMin = min;
   mDisplayMax = max;
}

int WaveTrack::ZeroLevelYCoordinate(wxRect rect) const
{
   return rect.GetTop() +
      (int)((mDisplayMax / (mDisplayMax - mDisplayMin)) * rect.height);
}

Track::Holder WaveTrack::Duplicate() const
{
   return Track::Holder{ safenew WaveTrack{ *this } };
}

double WaveTrack::GetRate() const
{
   return mRate;
}

void WaveTrack::SetRate(double newRate)
{
   wxASSERT( newRate > 0 );
   newRate = std::max( 1.0, newRate );
   auto ratio = mRate / newRate;
   mRate = (int) newRate;
   for (const auto &clip : mClips) {
      clip->SetOffset( clip->GetOffset() * ratio );
   }
}

float WaveTrack::GetPan() const
{
   return mPan;
}

void WaveTrack::SetPan(float newPan)
{
   if (newPan > 1.0)
      mPan = 1.0;
   else if (newPan < -1.0)
      mPan = -1.0;
   else
      mPan = newPan;
}

void WaveTrack::SetWaveColorIndex(int colorIndex)
// STRONG-GUARANTEE
{
   for (const auto &clip : mClips)
      clip->SetColourIndex( colorIndex );
   mWaveColorIndex = colorIndex;
}


void WaveTrack::ConvertToSampleFormat(sampleFormat format)
// WEAK-GUARANTEE
// might complete on only some tracks
{
   for (const auto &clip : mClips)
      clip->ConvertToSampleFormat(format);
   mFormat = format;
}

bool WaveTrack::IsEmpty(double t0, double t1) const
{
   if (t0 > t1)
      return true;

   //wxPrintf("Searching for overlap in %.6f...%.6f\n", t0, t1);
   for (const auto &clip : mClips)
   {
      if (!clip->BeforeClip(t1) && !clip->AfterClip(t0)) {
         //wxPrintf("Overlapping clip: %.6f...%.6f\n",
         //       clip->GetStartTime(),
         //       clip->GetEndTime());
         // We found a clip that overlaps this region
         return false;
      }
   }
   //wxPrintf("No overlap found\n");

   // Otherwise, no clips overlap this region
   return true;
}

Track::Holder WaveTrack::Cut(double t0, double t1)
{
   if (t1 < t0)
      THROW_INCONSISTENCY_EXCEPTION;

   auto tmp = Copy(t0, t1);

   Clear(t0, t1);

   return tmp;
}

Track::Holder WaveTrack::Copy(double t0, double t1, bool forClipboard) const
{
   if (t1 < t0)
      THROW_INCONSISTENCY_EXCEPTION;

   WaveTrack *newTrack;
   Track::Holder result
   { newTrack = safenew WaveTrack{ mDirManager } };

   newTrack->Init(*this);

   // PRL:  Why shouldn't cutlines be copied and pasted too?  I don't know, but
   // that was the old behavior.  But this function is also used by the
   // Duplicate command and I changed its behavior in that case.

   for (const auto &clip : mClips)
   {
      if (t0 <= clip->GetStartTime() && t1 >= clip->GetEndTime())
      {
         // Whole clip is in copy region
         //wxPrintf("copy: clip %i is in copy region\n", (int)clip);

         newTrack->mClips.push_back
            (make_movable<WaveClip>(*clip, mDirManager, ! forClipboard));
         WaveClip *const newClip = newTrack->mClips.back().get();
         newClip->Offset(-t0);
      }
      else if (t1 > clip->GetStartTime() && t0 < clip->GetEndTime())
      {
         // Clip is affected by command
         //wxPrintf("copy: clip %i is affected by command\n", (int)clip);

         const double clip_t0 = std::max(t0, clip->GetStartTime());
         const double clip_t1 = std::min(t1, clip->GetEndTime());

         auto newClip = make_movable<WaveClip>
            (*clip, mDirManager, ! forClipboard, clip_t0, clip_t1);

         //wxPrintf("copy: clip_t0=%f, clip_t1=%f\n", clip_t0, clip_t1);

         newClip->Offset(-t0);
         if (newClip->GetOffset() < 0)
            newClip->SetOffset(0);

         newTrack->mClips.push_back(std::move(newClip)); // transfer ownership
      }
   }

   // AWD, Oct 2009: If the selection ends in whitespace, create a placeholder
   // clip representing that whitespace
   // PRL:  Only if we want the track for pasting into other tracks.  Not if it
   // goes directly into a project as in the Duplicate command.
   if (forClipboard &&
       newTrack->GetEndTime() + 1.0 / newTrack->GetRate() < t1 - t0)
   {
      auto placeholder = make_movable<WaveClip>(mDirManager,
            newTrack->GetSampleFormat(),
            static_cast<int>(newTrack->GetRate()),
            0 /*colourindex*/);
      placeholder->SetIsPlaceholder(true);
      placeholder->InsertSilence(0, (t1 - t0) - newTrack->GetEndTime());
      placeholder->Offset(newTrack->GetEndTime());
      newTrack->mClips.push_back(std::move(placeholder)); // transfer ownership
   }

   return result;
}

Track::Holder WaveTrack::CopyNonconst(double t0, double t1)
{
   return Copy(t0, t1);
}

void WaveTrack::Clear(double t0, double t1)
// STRONG-GUARANTEE
{
   HandleClear(t0, t1, false, false);
}

void WaveTrack::ClearAndAddCutLine(double t0, double t1)
// STRONG-GUARANTEE
{
   HandleClear(t0, t1, true, false);
}

const WaveformSettings &WaveTrack::GetWaveformSettings() const
{
   if (mpWaveformSettings)
      return *mpWaveformSettings;
   else
      return WaveformSettings::defaults();
}

WaveformSettings &WaveTrack::GetWaveformSettings()
{
   if (mpWaveformSettings)
      return *mpWaveformSettings;
   else
      return WaveformSettings::defaults();
}

WaveformSettings &WaveTrack::GetIndependentWaveformSettings()
{
   if (!mpWaveformSettings)
      mpWaveformSettings = std::make_unique<WaveformSettings>(WaveformSettings::defaults());
   return *mpWaveformSettings;
}

void WaveTrack::SetWaveformSettings(std::unique_ptr<WaveformSettings> &&pSettings)
{
   if (mpWaveformSettings != pSettings) {
      mpWaveformSettings = std::move(pSettings);
   }
}

//
// ClearAndPaste() is a specialized version of HandleClear()
// followed by Paste() and is used mostly by effects that
// can't replace track data directly using Get()/Set().
//
// HandleClear() removes any cut/split lines lines with the
// cleared range, but, in most cases, effects want to preserve
// the existing cut/split lines, so they are saved before the
// HandleClear()/Paste() and restored after.
//
// If the pasted track overlaps two or more clips, then it will
// be pasted with visible split lines.  Normally, effects do not
// want these extra lines, so they may be merged out.
//
void WaveTrack::ClearAndPaste(double t0, // Start of time to clear
                              double t1, // End of time to clear
                              const Track *src, // What to paste
                              bool merge // Whether to remove 'extra' splits
                              )
// WEAK-GUARANTEE
// this WaveTrack remains destructible in case of AudacityException.
// But some of its cutline clips may have been destroyed.
{
   double dur = std::min(t1 - t0, src->GetEndTime());

   // If duration is 0, then it's just a plain paste
   if (dur == 0.0) {
      // use WEAK-GUARANTEE
      Paste(t0, src);
      return;
   }

   std::vector<double> splits;
   WaveClipHolders cuts;

   // Align to a sample
   t0 = LongSamplesToTime(TimeToLongSamples(t0));
   t1 = LongSamplesToTime(TimeToLongSamples(t1));

   // Save the cut/split lines whether preserving or not since merging
   // needs to know if a clip boundary is being crossed since Paste()
   // will add split lines around the pasted clip if so.
   for (const auto &clip : mClips) {
      double st;

      // Remember clip boundaries as locations to split
      st = LongSamplesToTime(TimeToLongSamples(clip->GetStartTime()));
      if (st >= t0 && st <= t1 && !make_iterator_range(splits).contains(st)) {
         splits.push_back(st);
      }

      st = LongSamplesToTime(TimeToLongSamples(clip->GetEndTime()));
      if (st >= t0 && st <= t1 && !make_iterator_range(splits).contains(st)) {
         splits.push_back(st);
      }

      // Search for cut lines
      auto &cutlines = clip->GetCutLines();
      // May erase from cutlines, so don't use range-for
      for (auto it = cutlines.begin(); it != cutlines.end(); ) {
         WaveClip *cut = it->get();
         double cs = LongSamplesToTime(TimeToLongSamples(clip->GetOffset() +
                                                         cut->GetOffset()));

         // Remember cut point
         if (cs >= t0 && cs <= t1) {

            // Remember the absolute offset and add to our cuts array.
            cut->SetOffset(cs);
            cuts.push_back(std::move(*it)); // transfer ownership!
            it = cutlines.erase(it);
         }
         else
            ++it;
      }
   }

   const auto tolerance = 2.0 / GetRate();

   // Now, clear the selection
   HandleClear(t0, t1, false, false);
   {

      // And paste in the NEW data
      Paste(t0, src);
      {
         // First, merge the NEW clip(s) in with the existing clips
         if (merge && splits.size() > 0)
         {
            // Now t1 represents the absolute end of the pasted data.
            t1 = t0 + src->GetEndTime();

            // Get a sorted array of the clips
            auto clips = SortedClipArray();

            // Scan the sorted clips for the first clip whose start time
            // exceeds the pasted regions end time.
            {
               WaveClip *prev = nullptr;
               for (const auto clip : clips) {
                  // Merge this clip and the previous clip if the end time
                  // falls within it and this isn't the first clip in the track.
                  if (fabs(t1 - clip->GetStartTime()) < tolerance) {
                     if (prev)
                        MergeClips(GetClipIndex(prev), GetClipIndex(clip));
                     break;
                  }
                  prev = clip;
               }
            }
         }

         // Refill the array since clips have changed.
         auto clips = SortedClipArray();

         {
            // Scan the sorted clips to look for the start of the pasted
            // region.
            WaveClip *prev = nullptr;
            for (const auto clip : clips) {
               if (prev) {
                  // It must be that clip is what was pasted and it begins where
                  // prev ends.
                  // use WEAK-GUARANTEE
                  MergeClips(GetClipIndex(prev), GetClipIndex(clip));
                  break;
               }
               if (fabs(t0 - clip->GetEndTime()) < tolerance)
                  // Merge this clip and the next clip if the start time
                  // falls within it and this isn't the last clip in the track.
                  prev = clip;
               else
                  prev = nullptr;
            }
         }
      }
   }
}

namespace
{
   WaveClipHolders::const_iterator
      FindClip(const WaveClipHolders &list, const WaveClip *clip, int *distance = nullptr)
   {
      if (distance)
         *distance = 0;
      auto it = list.begin();
      for (const auto end = list.end(); it != end; ++it)
      {
         if (it->get() == clip)
            break;
         if (distance)
            ++*distance;
      }
      return it;
   }

   WaveClipHolders::iterator
      FindClip(WaveClipHolders &list, const WaveClip *clip, int *distance = nullptr)
   {
      if (distance)
         *distance = 0;
      auto it = list.begin();
      for (const auto end = list.end(); it != end; ++it)
      {
         if (it->get() == clip)
            break;
         if (distance)
            ++*distance;
      }
      return it;
   }
}

std::shared_ptr<WaveClip> WaveTrack::RemoveAndReturnClip(WaveClip* clip)
{
   // Be clear about who owns the clip!!
   auto it = FindClip(mClips, clip);
   if (it != mClips.end()) {
      auto result = std::move(*it); // Array stops owning the clip, before we shrink it
      mClips.erase(it);
      return result;
   }
   else
      return {};
}

void WaveTrack::AddClip(std::shared_ptr<WaveClip> &&clip)
{
   // Uncomment the following line after we correct the problem of zero-length clips
   //if (CanInsertClip(clip))
      mClips.push_back(std::move(clip)); // transfer ownership
}

void WaveTrack::HandleClear(double t0, double t1,
                            bool addCutLines, bool split)
// STRONG-GUARANTEE
{
   if (t1 < t0)
      THROW_INCONSISTENCY_EXCEPTION;

   bool editClipCanMove = true;
   gPrefs->Read(wxT("/GUI/EditClipCanMove"), &editClipCanMove);

   WaveClipPointers clipsToDelete;
   WaveClipHolders clipsToAdd;

   // We only add cut lines when deleting in the middle of a single clip
   // The cut line code is not really prepared to handle other situations
   if (addCutLines)
   {
      for (const auto &clip : mClips)
      {
         if (!clip->BeforeClip(t1) && !clip->AfterClip(t0) &&
               (clip->BeforeClip(t0) || clip->AfterClip(t1)))
         {
            addCutLines = false;
            break;
         }
      }
   }

   for (const auto &clip : mClips)
   {
      if (clip->BeforeClip(t0) && clip->AfterClip(t1))
      {
         // Whole clip must be deleted - remember this
         clipsToDelete.push_back(clip.get());
      }
      else if (!clip->BeforeClip(t1) && !clip->AfterClip(t0))
      {
         // Clip data is affected by command
         if (addCutLines)
         {
            // Don't modify this clip in place, because we want a strong
            // guarantee, and might modify another clip
            clipsToDelete.push_back( clip.get() );
            auto newClip = make_movable<WaveClip>( *clip, mDirManager, true );
            newClip->ClearAndAddCutLine( t0, t1 );
            clipsToAdd.push_back( std::move( newClip ) );
         }
         else
         {
            if (split) {
               // Three cases:

               if (clip->BeforeClip(t0)) {
                  // Delete from the left edge

                  // Don't modify this clip in place, because we want a strong
                  // guarantee, and might modify another clip
                  clipsToDelete.push_back( clip.get() );
                  auto newClip = make_movable<WaveClip>( *clip, mDirManager, true );
                  newClip->Clear(clip->GetStartTime(), t1);
                  newClip->Offset(t1-clip->GetStartTime());

                  clipsToAdd.push_back( std::move( newClip ) );
               }
               else if (clip->AfterClip(t1)) {
                  // Delete to right edge

                  // Don't modify this clip in place, because we want a strong
                  // guarantee, and might modify another clip
                  clipsToDelete.push_back( clip.get() );
                  auto newClip = make_movable<WaveClip>( *clip, mDirManager, true );
                  newClip->Clear(t0, clip->GetEndTime());

                  clipsToAdd.push_back( std::move( newClip ) );
               }
               else {
                  // Delete in the middle of the clip...we actually create two
                  // NEW clips out of the left and right halves...

                  // left
                  clipsToAdd.push_back
                     ( make_movable<WaveClip>( *clip, mDirManager, true ) );
                  clipsToAdd.back()->Clear(t0, clip->GetEndTime());

                  // right
                  clipsToAdd.push_back
                     ( make_movable<WaveClip>( *clip, mDirManager, true ) );
                  WaveClip *const right = clipsToAdd.back().get();
                  right->Clear(clip->GetStartTime(), t1);
                  right->Offset(t1 - clip->GetStartTime());

                  clipsToDelete.push_back(clip.get());
               }
            }
            else {
               // (We are not doing a split cut)

               // Don't modify this clip in place, because we want a strong
               // guarantee, and might modify another clip
               clipsToDelete.push_back( clip.get() );
               auto newClip = make_movable<WaveClip>( *clip, mDirManager, true );

               // clip->Clear keeps points < t0 and >= t1 via Envelope::CollapseRegion
               newClip->Clear(t0,t1);

               clipsToAdd.push_back( std::move( newClip ) );
            }
         }
      }
   }

   // Only now, change the contents of this track
   // use NOFAIL-GUARANTEE for the rest

   for (const auto &clip : mClips)
   {
      if (clip->BeforeClip(t1))
      {
         // Clip is "behind" the region -- offset it unless we're splitting
         // or we're using the "don't move other clips" mode
         if (!split && editClipCanMove)
            clip->Offset(-(t1-t0));
      }
   }

   for (const auto &clip: clipsToDelete)
   {
      auto myIt = FindClip(mClips, clip);
      if (myIt != mClips.end())
         mClips.erase(myIt); // deletes the clip!
      else
         wxASSERT(false);
   }

   for (auto &clip: clipsToAdd)
      mClips.push_back(std::move(clip)); // transfer ownership
}

void WaveTrack::Paste(double t0, const Track *src)
// WEAK-GUARANTEE
{
   bool editClipCanMove = true;
   gPrefs->Read(wxT("/GUI/EditClipCanMove"), &editClipCanMove);

   if( src == NULL )
      // THROW_INCONSISTENCY_EXCEPTION; // ?
      return;

   if (src->GetKind() != Track::Wave)
      // THROW_INCONSISTENCY_EXCEPTION; // ?
      return;

   const WaveTrack* other = static_cast<const WaveTrack*>(src);

   //
   // Pasting is a bit complicated, because with the existence of multiclip mode,
   // we must guess the behaviour the user wants.
   //
   // Currently, two modes are implemented:
   //
   // - If a single clip should be pasted, and it should be pasted inside another
   //   clip, no NEW clips are generated. The audio is simply inserted.
   //   This resembles the old (pre-multiclip support) behaviour. However, if
   //   the clip is pasted outside of any clip, a NEW clip is generated. This is
   //   the only behaviour which is different to what was done before, but it
   //   shouldn't confuse users too much.
   //
   // - If multiple clips should be pasted, or a single clip that does not fill
   // the duration of the pasted track, these are always pasted as single
   // clips, and the current clip is splitted, when necessary. This may seem
   // strange at first, but it probably is better than trying to auto-merge
   // anything. The user can still merge the clips by hand (which should be a
   // simple command reachable by a hotkey or single mouse click).
   //

   if (other->GetNumClips() == 0)
      return;

   //wxPrintf("paste: we have at least one clip\n");

   bool singleClipMode = (other->GetNumClips() == 1 &&
         other->GetStartTime() == 0.0);

   const double insertDuration = other->GetEndTime();
   if( insertDuration != 0 && insertDuration < 1.0/mRate )
      // PRL:  I added this check to avoid violations of preconditions in other WaveClip and Sequence
      // methods, but allow the value 0 so I don't subvert the purpose of commit
      // 739422ba70ceb4be0bb1829b6feb0c5401de641e which causes append-recording always to make
      // a new clip.
      return;

   //wxPrintf("Check if we need to make room for the pasted data\n");

   // Make room for the pasted data
   if (editClipCanMove) {
      if (!singleClipMode) {
         // We need to insert multiple clips, so split the current clip and
         // move everything to the right, then try to paste again
         if (!IsEmpty(t0, GetEndTime())) {
            auto tmp = Cut(t0, GetEndTime()+1.0/mRate);
            Paste(t0 + insertDuration, tmp.get());
         }
      }
      else {
         // We only need to insert one single clip, so just move all clips
         // to the right of the paste point out of the way
         for (const auto &clip : mClips)
         {
            if (clip->GetStartTime() > t0-(1.0/mRate))
               clip->Offset(insertDuration);
         }
      }
   }

   if (singleClipMode)
   {
      // Single clip mode
      // wxPrintf("paste: checking for single clip mode!\n");

      WaveClip *insideClip = NULL;

      for (const auto &clip : mClips)
      {
         if (editClipCanMove)
         {
            if (clip->WithinClip(t0))
            {
               //wxPrintf("t0=%.6f: inside clip is %.6f ... %.6f\n",
               //       t0, clip->GetStartTime(), clip->GetEndTime());
               insideClip = clip.get();
               break;
            }
         }
         else
         {
            // If clips are immovable we also allow prepending to clips
            if (clip->WithinClip(t0) ||
                  TimeToLongSamples(t0) == clip->GetStartSample())
            {
               insideClip = clip.get();
               break;
            }
         }
      }

      if (insideClip)
      {
         // Exhibit traditional behaviour
         //wxPrintf("paste: traditional behaviour\n");
         if (!editClipCanMove)
         {
            // We did not move other clips out of the way already, so
            // check if we can paste without having to move other clips
            for (const auto &clip : mClips)
            {
               if (clip->GetStartTime() > insideClip->GetStartTime() &&
                   insideClip->GetEndTime() + insertDuration >
                                                      clip->GetStartTime())
                  // STRONG-GUARANTEE in case of this path
                  // not that it matters.
                  throw SimpleMessageBoxException{
                     _("There is not enough room available to paste the selection")
                  };
            }
         }

         insideClip->Paste(t0, other->GetClipByIndex(0));
         return;
      }

      // Just fall through and exhibit NEW behaviour
   }

   // Insert NEW clips
   //wxPrintf("paste: multi clip mode!\n");

   if (!editClipCanMove && !IsEmpty(t0, t0+insertDuration-1.0/mRate))
      // STRONG-GUARANTEE in case of this path
      // not that it matters.
      throw SimpleMessageBoxException{
         _("There is not enough room available to paste the selection")
      };

   for (const auto &clip : other->mClips)
   {
      // AWD Oct. 2009: Don't actually paste in placeholder clips
      if (!clip->GetIsPlaceholder())
      {
         auto newClip =
            make_movable<WaveClip>( *clip, mDirManager, true );
         newClip->Resample(mRate);
         newClip->Offset(t0);
         newClip->MarkChanged();
         mClips.push_back(std::move(newClip)); // transfer ownership
      }
   }
}

void WaveTrack::Silence(double t0, double t1)
{
   if (t1 < t0)
      THROW_INCONSISTENCY_EXCEPTION;

   auto start = (sampleCount)floor(t0 * mRate + 0.5);
   auto len = (sampleCount)floor(t1 * mRate + 0.5) - start;

   for (const auto &clip : mClips)
   {
      auto clipStart = clip->GetStartSample();
      auto clipEnd = clip->GetEndSample();

      if (clipEnd > start && clipStart < start+len)
      {
         // Clip sample region and Get/Put sample region overlap
         auto samplesToCopy = start+len - clipStart;
         if (samplesToCopy > clip->GetNumSamples())
            samplesToCopy = clip->GetNumSamples();
         auto startDelta = clipStart - start;
         decltype(startDelta) inclipDelta = 0;
         if (startDelta < 0)
         {
            inclipDelta = -startDelta; // make positive value
            samplesToCopy -= inclipDelta;
            startDelta = 0;
         }

         clip->GetSequence()->SetSilence(inclipDelta, samplesToCopy);
         clip->MarkChanged();
      }
   }
}

void WaveTrack::InsertSilence(double t, double len)
// STRONG-GUARANTEE
{
   // Nothing to do, if length is zero.
   // Fixes Bug 1626
   if( len == 0 )
      return;
   if (len <= 0)
      THROW_INCONSISTENCY_EXCEPTION;

   if (mClips.empty())
   {
      // Special case if there is no clip yet
      auto clip = make_movable<WaveClip>(mDirManager, mFormat, mRate, this->GetWaveColorIndex());
      clip->InsertSilence(0, len);
      // use NOFAIL-GUARANTEE
      mClips.push_back( std::move( clip ) );
      return;
   }
   else {
      // Assume at most one clip contains t
      const auto end = mClips.end();
      const auto it = std::find_if( mClips.begin(), end,
         [&](const WaveClipHolder &clip) { return clip->WithinClip(t); } );

      // use STRONG-GUARANTEE
      if (it != end)
         it->get()->InsertSilence(t, len);

      // use NOFAIL-GUARANTEE
      for (const auto &clip : mClips)
      {
         if (clip->BeforeClip(t))
            clip->Offset(len);
      }
   }
}

void WaveTrack::Append(samplePtr buffer, sampleFormat format,
                       size_t len, unsigned int stride /* = 1 */)
// PARTIAL-GUARANTEE in case of exceptions:
// Some prefix (maybe none) of the buffer is appended, and no content already
// flushed to disk is lost.
{
   RightmostOrNewClip()->Append(buffer, format, len, stride);
}

void WaveTrack::AppendAlias(const wxString &fName, sampleCount start,
                            size_t len, int channel,bool useOD)
// STRONG-GUARANTEE
{
   RightmostOrNewClip()->AppendAlias(fName, start, len, channel, useOD);
}

void WaveTrack::AppendCoded(const wxString &fName, sampleCount start,
                            size_t len, int channel, int decodeType)
// STRONG-GUARANTEE
{
   RightmostOrNewClip()->AppendCoded(fName, start, len, channel, decodeType);
}

///gets an int with OD flags so that we can determine which ODTasks should be run on this track after save/open, etc.
unsigned int WaveTrack::GetODFlags() const
{
   unsigned int ret = 0;
   for (const auto &clip : mClips)
   {
      ret = ret | clip->GetSequence()->GetODFlags();
   }
   return ret;
}


sampleCount WaveTrack::GetBlockStart(sampleCount s) const
{
   for (const auto &clip : mClips)
   {
      const auto startSample = (sampleCount)floor(0.5 + clip->GetStartTime()*mRate);
      const auto endSample = startSample + clip->GetNumSamples();
      if (s >= startSample && s < endSample)
         return startSample + clip->GetSequence()->GetBlockStart(s - startSample);
   }

   return -1;
}

size_t WaveTrack::GetBestBlockSize(sampleCount s) const
{
   auto bestBlockSize = GetMaxBlockSize();

   for (const auto &clip : mClips)
   {
      auto startSample = (sampleCount)floor(clip->GetStartTime()*mRate + 0.5);
      auto endSample = startSample + clip->GetNumSamples();
      if (s >= startSample && s < endSample)
      {
         bestBlockSize = clip->GetSequence()->GetBestBlockSize(s - startSample);
         break;
      }
   }

   return bestBlockSize;
}

size_t WaveTrack::GetMaxBlockSize() const
{
   decltype(GetMaxBlockSize()) maxblocksize = 0;
   for (const auto &clip : mClips)
   {
      maxblocksize = std::max(maxblocksize, clip->GetSequence()->GetMaxBlockSize());
   }

   if (maxblocksize == 0)
   {
      // We really need the maximum block size, so create a
      // temporary sequence to get it.
      maxblocksize = Sequence{ mDirManager, mFormat }.GetMaxBlockSize();
   }

   wxASSERT(maxblocksize > 0);

   return maxblocksize;
}

size_t WaveTrack::GetIdealBlockSize()
{
   return NewestOrNewClip()->GetSequence()->GetIdealBlockSize();
}

void WaveTrack::Flush()
// NOFAIL-GUARANTEE that the rightmost clip will be in a flushed state.
// PARTIAL-GUARANTEE in case of exceptions:
// Some initial portion (maybe none) of the append buffer of the rightmost
// clip gets appended; no previously saved contents are lost.
{
   // After appending, presumably.  Do this to the clip that gets appended.
   RightmostOrNewClip()->Flush();
}

bool WaveTrack::Lock() const
{
   for (const auto &clip : mClips)
      clip->Lock();

   return true;
}

bool WaveTrack::CloseLock()
{
   for (const auto &clip : mClips)
      clip->CloseLock();

   return true;
}


bool WaveTrack::Unlock() const
{
   for (const auto &clip : mClips)
      clip->Unlock();

   return true;
}

AUDACITY_DLL_API sampleCount WaveTrack::TimeToLongSamples(double t0) const
{
   return sampleCount( floor(t0 * mRate + 0.5) );
}

double WaveTrack::LongSamplesToTime(sampleCount pos) const
{
   return pos.as_double() / mRate;
}

double WaveTrack::GetStartTime() const
{
   bool found = false;
   double best = 0.0;

   if (mClips.empty())
      return 0;

   for (const auto &clip : mClips)
      if (!found)
      {
         found = true;
         best = clip->GetStartTime();
      }
      else if (clip->GetStartTime() < best)
         best = clip->GetStartTime();

   return best;
}

double WaveTrack::GetEndTime() const
{
   bool found = false;
   double best = 0.0;

   if (mClips.empty())
      return 0;

   for (const auto &clip : mClips)
      if (!found)
      {
         found = true;
         best = clip->GetEndTime();
      }
      else if (clip->GetEndTime() > best)
         best = clip->GetEndTime();

   return best;
}

//
// Getting/setting samples.  The sample counts here are
// expressed relative to t=0.0 at the track's sample rate.
//

std::pair<float, float> WaveTrack::GetMinMax(
   double t0, double t1, bool mayThrow) const
{
   std::pair<float, float> results {
      // we need these at extremes to make sure we find true min and max
      FLT_MAX, -FLT_MAX
   };
   bool clipFound = false;

   if (t0 > t1) {
      if (mayThrow)
         THROW_INCONSISTENCY_EXCEPTION;
      return results;
   }

   if (t0 == t1)
      return results;

   for (const auto &clip: mClips)
   {
      if (t1 >= clip->GetStartTime() && t0 <= clip->GetEndTime())
      {
         clipFound = true;
         auto clipResults = clip->GetMinMax(t0, t1, mayThrow);
         if (clipResults.first < results.first)
            results.first = clipResults.first;
         if (clipResults.second > results.second)
            results.second = clipResults.second;
      }
   }

   if(!clipFound)
   {
      results = { 0.f, 0.f }; // sensible defaults if no clips found
   }

   return results;
}

float WaveTrack::GetRMS(double t0, double t1, bool mayThrow) const
{
   if (t0 > t1) {
      if (mayThrow)
         THROW_INCONSISTENCY_EXCEPTION;
      return 0.f;
   }

   if (t0 == t1)
      return 0.f;

   double sumsq = 0.0;
   sampleCount length = 0;

   for (const auto &clip: mClips)
   {
      // If t1 == clip->GetStartTime() or t0 == clip->GetEndTime(), then the clip
      // is not inside the selection, so we don't want it.
      // if (t1 >= clip->GetStartTime() && t0 <= clip->GetEndTime())
      if (t1 >= clip->GetStartTime() && t0 <= clip->GetEndTime())
      {
         sampleCount clipStart, clipEnd;

         float cliprms = clip->GetRMS(t0, t1, mayThrow);

         clip->TimeToSamplesClip(wxMax(t0, clip->GetStartTime()), &clipStart);
         clip->TimeToSamplesClip(wxMin(t1, clip->GetEndTime()), &clipEnd);
         sumsq += cliprms * cliprms * (clipEnd - clipStart).as_float();
         length += (clipEnd - clipStart);
      }
   }
   return length > 0 ? sqrt(sumsq / length.as_double()) : 0.0;
}

bool WaveTrack::Get(samplePtr buffer, sampleFormat format,
                    sampleCount start, size_t len, fillFormat fill,
                    bool mayThrow) const
{
   // Simple optimization: When this buffer is completely contained within one clip,
   // don't clear anything (because we won't have to). Otherwise, just clear
   // everything to be on the safe side.
   bool doClear = true;
   bool result = true;
   for (const auto &clip: mClips)
   {
      if (start >= clip->GetStartSample() && start+len <= clip->GetEndSample())
      {
         doClear = false;
         break;
      }
   }
   if (doClear)
   {
      // Usually we fill in empty space with zero
      if( fill == fillZero )
         ClearSamples(buffer, format, 0, len);
      // but we don't have to.
      else if( fill==fillTwo )
      {
         wxASSERT( format==floatSample );
         float * pBuffer = (float*)buffer;
         for(size_t i=0;i<len;i++)
            pBuffer[i]=2.0f;
      }
      else
      {
         wxFAIL_MSG(wxT("Invalid fill format"));
      }
   }

   for (const auto &clip: mClips)
   {
      auto clipStart = clip->GetStartSample();
      auto clipEnd = clip->GetEndSample();

      if (clipEnd > start && clipStart < start+len)
      {
         // Clip sample region and Get/Put sample region overlap
         auto samplesToCopy =
            std::min( start+len - clipStart, clip->GetNumSamples() );
         auto startDelta = clipStart - start;
         decltype(startDelta) inclipDelta = 0;
         if (startDelta < 0)
         {
            inclipDelta = -startDelta; // make positive value
            samplesToCopy -= inclipDelta;
            // samplesToCopy is now either len or
            //    (clipEnd - clipStart) - (start - clipStart)
            //    == clipEnd - start > 0
            // samplesToCopy is not more than len
            //
            startDelta = 0;
            // startDelta is zero
         }
         else {
            // startDelta is nonnegative and less than than len
            // samplesToCopy is positive and not more than len
         }

         if (!clip->GetSamples(
               (samplePtr)(((char*)buffer) +
                           startDelta.as_size_t() *
                           SAMPLE_SIZE(format)),
               format, inclipDelta, samplesToCopy.as_size_t(), mayThrow ))
            result = false;
      }
   }

   return result;
}

void WaveTrack::Set(samplePtr buffer, sampleFormat format,
                    sampleCount start, size_t len)
// WEAK-GUARANTEE
{
   for (const auto &clip: mClips)
   {
      auto clipStart = clip->GetStartSample();
      auto clipEnd = clip->GetEndSample();

      if (clipEnd > start && clipStart < start+len)
      {
         // Clip sample region and Get/Put sample region overlap
         auto samplesToCopy =
            std::min( start+len - clipStart, clip->GetNumSamples() );
         auto startDelta = clipStart - start;
         decltype(startDelta) inclipDelta = 0;
         if (startDelta < 0)
         {
            inclipDelta = -startDelta; // make positive value
            samplesToCopy -= inclipDelta;
            // samplesToCopy is now either len or
            //    (clipEnd - clipStart) - (start - clipStart)
            //    == clipEnd - start > 0
            // samplesToCopy is not more than len
            //
            startDelta = 0;
            // startDelta is zero
         }
         else {
            // startDelta is nonnegative and less than than len
            // samplesToCopy is positive and not more than len
         }

         clip->SetSamples(
               (samplePtr)(((char*)buffer) +
                           startDelta.as_size_t() *
                           SAMPLE_SIZE(format)),
                          format, inclipDelta, samplesToCopy.as_size_t() );
         clip->MarkChanged();
      }
   }
}

void WaveTrack::GetEnvelopeValues(double *buffer, size_t bufferLen,
                                  double t0) const
{
   // The output buffer corresponds to an unbroken span of time which the callers expect
   // to be fully valid.  As clips are processed below, the output buffer is updated with
   // envelope values from any portion of a clip, start, end, middle, or none at all.
   // Since this does not guarantee that the entire buffer is filled with values we need
   // to initialize the entire buffer to a default value.
   //
   // This does mean that, in the cases where a usable clip is located, the buffer value will
   // be set twice.  Unfortunately, there is no easy way around this since the clips are not
   // stored in increasing time order.  If they were, we could just track the time as the
   // buffer is filled.
   for (decltype(bufferLen) i = 0; i < bufferLen; i++)
   {
      buffer[i] = 1.0;
   }

   double startTime = t0;
   auto tstep = 1.0 / mRate;
   double endTime = t0 + tstep * bufferLen;
   for (const auto &clip: mClips)
   {
      // IF clip intersects startTime..endTime THEN...
      auto dClipStartTime = clip->GetStartTime();
      auto dClipEndTime = clip->GetEndTime();
      if ((dClipStartTime < endTime) && (dClipEndTime > startTime))
      {
         auto rbuf = buffer;
         auto rlen = bufferLen;
         auto rt0 = t0;

         if (rt0 < dClipStartTime)
         {
            // This is not more than the number of samples in
            // (endTime - startTime) which is bufferLen:
            auto nDiff = (sampleCount)floor((dClipStartTime - rt0) * mRate + 0.5);
            auto snDiff = nDiff.as_size_t();
            rbuf += snDiff;
            wxASSERT(snDiff <= rlen);
            rlen -= snDiff;
            rt0 = dClipStartTime;
         }

         if (rt0 + rlen*tstep > dClipEndTime)
         {
            auto nClipLen = clip->GetEndSample() - clip->GetStartSample();

            if (nClipLen <= 0) // Testing for bug 641, this problem is consistently '== 0', but doesn't hurt to check <.
               return;

            // This check prevents problem cited in http://bugzilla.audacityteam.org/show_bug.cgi?id=528#c11,
            // Gale's cross_fade_out project, which was already corrupted by bug 528.
            // This conditional prevents the previous write past the buffer end, in clip->GetEnvelope() call.
            // Never increase rlen here.
            // PRL bug 827:  rewrote it again
            rlen = limitSampleBufferSize( rlen, nClipLen );
            rlen = std::min(rlen, size_t(floor(0.5 + (dClipEndTime - rt0) / tstep)));
         }
         // Samples are obtained for the purpose of rendering a wave track,
         // so quantize time
         clip->GetEnvelope()->GetValues(rbuf, rlen, rt0, tstep);
      }
   }
}

WaveClip* WaveTrack::GetClipAtX(int xcoord)
{
   for (const auto &clip: mClips)
   {
      wxRect r;
      clip->GetDisplayRect(&r);
      if (xcoord >= r.x && xcoord < r.x+r.width)
         return clip.get();
   }

   return NULL;
}

WaveClip* WaveTrack::GetClipAtSample(sampleCount sample)
{
   for (const auto &clip: mClips)
   {
      auto start = clip->GetStartSample();
      auto len   = clip->GetNumSamples();

      if (sample >= start && sample < start + len)
         return clip.get();
   }

   return NULL;
}

// When the time is both the end of a clip and the start of the next clip, the
// latter clip is returned.
WaveClip* WaveTrack::GetClipAtTime(double time)
{
   
   const auto clips = SortedClipArray();
   auto p = std::find_if(clips.rbegin(), clips.rend(), [&] (WaveClip* const& clip) {
      return time >= clip->GetStartTime() && time <= clip->GetEndTime(); });

   // When two clips are immediately next to each other, the GetEndTime() of the first clip
   // and the GetStartTime() of the second clip may not be exactly equal due to rounding errors.
   // If "time" is the end time of the first of two such clips, and the end time is slightly
   // less than the start time of the second clip, then the first rather than the
   // second clip is found by the above code. So correct this.
   if (p != clips.rend() && p != clips.rbegin() &&
      time == (*p)->GetEndTime() &&
      (*p)->SharesBoundaryWithNextClip(*(p-1))) {
      p--;
   }

   return p != clips.rend() ? *p : nullptr;
}

Envelope* WaveTrack::GetEnvelopeAtX(int xcoord)
{
   WaveClip* clip = GetClipAtX(xcoord);
   if (clip)
      return clip->GetEnvelope();
   else
      return NULL;
}

Sequence* WaveTrack::GetSequenceAtX(int xcoord)
{
   WaveClip* clip = GetClipAtX(xcoord);
   if (clip)
      return clip->GetSequence();
   else
      return NULL;
}

WaveClip* WaveTrack::CreateClip()
{
   mClips.push_back(make_movable<WaveClip>(mDirManager, mFormat, mRate, GetWaveColorIndex()));
   return mClips.back().get();
}

WaveClip* WaveTrack::NewestOrNewClip()
{
   if (mClips.empty()) {
      WaveClip *clip = CreateClip();
      clip->SetOffset(mOffset);
      return clip;
   }
   else
      return mClips.back().get();
}

WaveClip* WaveTrack::RightmostOrNewClip()
// NOFAIL-GUARANTEE
{
   if (mClips.empty()) {
      WaveClip *clip = CreateClip();
      clip->SetOffset(mOffset);
      return clip;
   }
   else
   {
      auto it = mClips.begin();
      WaveClip *rightmost = (*it++).get();
      double maxOffset = rightmost->GetOffset();
      for (auto end = mClips.end(); it != end; ++it)
      {
         WaveClip *clip = it->get();
         double offset = clip->GetOffset();
         if (maxOffset < offset)
            maxOffset = offset, rightmost = clip;
      }
      return rightmost;
   }
}

int WaveTrack::GetClipIndex(const WaveClip* clip) const
{
   int result;
   FindClip(mClips, clip, &result);
   return result;
}

WaveClip* WaveTrack::GetClipByIndex(int index)
{
   if(index < (int)mClips.size())
      return mClips[index].get();
   else
      return nullptr;
}

const WaveClip* WaveTrack::GetClipByIndex(int index) const
{
   return const_cast<WaveTrack&>(*this).GetClipByIndex(index);
}

int WaveTrack::GetNumClips() const
{
   return mClips.size();
}

bool WaveTrack::CanOffsetClip(WaveClip* clip, double amount,
                              double *allowedAmount /* = NULL */)
{
   if (allowedAmount)
      *allowedAmount = amount;

   for (const auto &c: mClips)
   {
      if (c.get() != clip && c->GetStartTime() < clip->GetEndTime()+amount &&
                       c->GetEndTime() > clip->GetStartTime()+amount)
      {
         if (!allowedAmount)
            return false; // clips overlap

         if (amount > 0)
         {
            if (c->GetStartTime()-clip->GetEndTime() < *allowedAmount)
               *allowedAmount = c->GetStartTime()-clip->GetEndTime();
            if (*allowedAmount < 0)
               *allowedAmount = 0;
         } else
         {
            if (c->GetEndTime()-clip->GetStartTime() > *allowedAmount)
               *allowedAmount = c->GetEndTime()-clip->GetStartTime();
            if (*allowedAmount > 0)
               *allowedAmount = 0;
         }
      }
   }

   if (allowedAmount)
   {
      if (*allowedAmount == amount)
         return true;

      // Check if the NEW calculated amount would not violate
      // any other constraint
      if (!CanOffsetClip(clip, *allowedAmount, NULL)) {
         *allowedAmount = 0; // play safe and don't allow anything
         return false;
      }
      else
         return true;
   } else
      return true;
}

bool WaveTrack::CanInsertClip(WaveClip* clip,  double &slideBy, double &tolerance)
{
   for (const auto &c : mClips)
   {
      double d1 = c->GetStartTime() - (clip->GetEndTime()+slideBy);
      double d2 = (clip->GetStartTime()+slideBy) - c->GetEndTime();
      if ( (d1<0) &&  (d2<0) )
      {
         // clips overlap.
         // Try to rescue it.
         // The rescue logic is not perfect, and will typically
         // move the clip at most once.  
         // We divide by 1000 rather than set to 0, to allow for 
         // a second 'micro move' that is really about rounding error.
         if( -d1 < tolerance ){
            // right edge of clip overlaps slightly.
            // slide clip left a small amount.
            slideBy +=d1;
            tolerance /=1000;
         } else if( -d2 < tolerance ){
            // left edge of clip overlaps slightly.
            // slide clip right a small amount.
            slideBy -= d2;
            tolerance /=1000;
         }
         else
            return false; // clips overlap  No tolerance left.
      }
   }

   return true;
}

void WaveTrack::Split( double t0, double t1 )
// WEAK-GUARANTEE
{
   SplitAt( t0 );
   if( t0 != t1 )
      SplitAt( t1 );
}

void WaveTrack::SplitAt(double t)
// WEAK-GUARANTEE
{
   for (const auto &c : mClips)
   {
      if (c->WithinClip(t))
      {
         t = LongSamplesToTime(TimeToLongSamples(t)); // put t on a sample
         auto newClip = make_movable<WaveClip>( *c, mDirManager, true );
         c->Clear(t, c->GetEndTime());
         newClip->Clear(c->GetStartTime(), t);

         //offset the NEW clip by the splitpoint (noting that it is already offset to c->GetStartTime())
         sampleCount here = llrint(floor(((t - c->GetStartTime()) * mRate) + 0.5));
         newClip->Offset(here.as_double()/(double)mRate);
         // This could invalidate the iterators for the loop!  But we return
         // at once so it's okay
         mClips.push_back(std::move(newClip)); // transfer ownership
         return;
      }
   }
}

// Expand cut line (that is, re-insert audio, then DELETE audio saved in cut line)
void WaveTrack::ExpandCutLine(double cutLinePosition, double* cutlineStart,
                              double* cutlineEnd)
// STRONG-GUARANTEE
{
   bool editClipCanMove = true;
   gPrefs->Read(wxT("/GUI/EditClipCanMove"), &editClipCanMove);

   // Find clip which contains this cut line
   double start = 0, end = 0;
   auto pEnd = mClips.end();
   auto pClip = std::find_if( mClips.begin(), pEnd,
      [&](const WaveClipHolder &clip) {
         return clip->FindCutLine(cutLinePosition, &start, &end); } );
   if (pClip != pEnd)
   {
      auto &clip = *pClip;
      if (!editClipCanMove)
      {
         // We are not allowed to move the other clips, so see if there
         // is enough room to expand the cut line
         for (const auto &clip2: mClips)
         {
            if (clip2->GetStartTime() > clip->GetStartTime() &&
                clip->GetEndTime() + end - start > clip2->GetStartTime())
               // STRONG-GUARANTEE in case of this path
               throw SimpleMessageBoxException{
                  _("There is not enough room available to expand the cut line")
               };
          }
      }

      clip->ExpandCutLine(cutLinePosition);

      // STRONG-GUARANTEE provided that the following gives NOFAIL-GUARANTEE

      if (cutlineStart)
         *cutlineStart = start;
      if (cutlineEnd)
         *cutlineEnd = end;

      // Move clips which are to the right of the cut line
      if (editClipCanMove)
      {
         for (const auto &clip2 : mClips)
         {
            if (clip2->GetStartTime() > clip->GetStartTime())
               clip2->Offset(end - start);
         }
      }
   }
}

bool WaveTrack::RemoveCutLine(double cutLinePosition)
{
   for (const auto &clip : mClips)
      if (clip->RemoveCutLine(cutLinePosition))
         return true;

   return false;
}

void WaveTrack::MergeClips(int clipidx1, int clipidx2)
// STRONG-GUARANTEE
{
   WaveClip* clip1 = GetClipByIndex(clipidx1);
   WaveClip* clip2 = GetClipByIndex(clipidx2);

   if (!clip1 || !clip2) // Could happen if one track of a linked pair had a split and the other didn't.
      return; // Don't throw, just do nothing.

   // Append data from second clip to first clip
   // use STRONG-GUARANTEE
   clip1->Paste(clip1->GetEndTime(), clip2);
   
   // use NOFAIL-GUARANTEE for the rest
   // Delete second clip
   auto it = FindClip(mClips, clip2);
   mClips.erase(it);
}

void WaveTrack::Resample(int rate, ProgressDialog *progress)
// WEAK-GUARANTEE
// Partial completion may leave clips at differing sample rates!
{
   for (const auto &clip : mClips)
      clip->Resample(rate, progress);

   mRate = rate;
}

namespace {
   template < typename Cont1, typename Cont2 >
   Cont1 FillSortedClipArray(const Cont2& mClips)
   {
      Cont1 clips;
      for (const auto &clip : mClips)
         clips.push_back(clip.get());
      std::sort(clips.begin(), clips.end(),
         [](const WaveClip *a, const WaveClip *b)
      { return a->GetStartTime() < b->GetStartTime(); });
      return clips;
   }
}

WaveClipPointers WaveTrack::SortedClipArray()
{
   return FillSortedClipArray<WaveClipPointers>(mClips);
}

WaveClipConstPointers WaveTrack::SortedClipArray() const
{
   return FillSortedClipArray<WaveClipConstPointers>(mClips);
}

///Deletes all clips' wavecaches.  Careful, This may not be threadsafe.
void WaveTrack::ClearWaveCaches()
{
   for (const auto &clip : mClips)
      clip->ClearWaveCache();
}

///Adds an invalid region to the wavecache so it redraws that portion only.
void WaveTrack::AddInvalidRegion(sampleCount startSample, sampleCount endSample)
{
   for (const auto &clip : mClips)
      clip->AddInvalidRegion(startSample, endSample);
}

WaveTrackCache::~WaveTrackCache()
{
}

void WaveTrackCache::SetTrack(const std::shared_ptr<const WaveTrack> &pTrack)
{
   if (mPTrack != pTrack) {
      if (pTrack) {
         mBufferSize = pTrack->GetMaxBlockSize();
         if (!mPTrack ||
             mPTrack->GetMaxBlockSize() != mBufferSize) {
            Free();
            mBuffers[0].data = Floats{ mBufferSize };
            mBuffers[1].data = Floats{ mBufferSize };
         }
      }
      else
         Free();
      mPTrack = pTrack;
      mNValidBuffers = 0;
   }
}

constSamplePtr WaveTrackCache::Get(sampleFormat format,
   sampleCount start, size_t len, bool mayThrow)
{
   if (format == floatSample && len > 0) {
      const auto end = start + len;

      bool fillFirst = (mNValidBuffers < 1);
      bool fillSecond = (mNValidBuffers < 2);

      // Discard cached results that we no longer need
      if (mNValidBuffers > 0 &&
          (end <= mBuffers[0].start ||
           start >= mBuffers[mNValidBuffers - 1].end())) {
         // Complete miss
         fillFirst = true;
         fillSecond = true;
      }
      else if (mNValidBuffers == 2 &&
               start >= mBuffers[1].start &&
               end > mBuffers[1].end()) {
         // Request starts in the second buffer and extends past it.
         // Discard the first buffer.
         // (But don't deallocate the buffer space.)
         mBuffers[0] .swap ( mBuffers[1] );
         fillSecond = true;
         mNValidBuffers = 1;
      }
      else if (mNValidBuffers > 0 &&
         start < mBuffers[0].start &&
         0 <= mPTrack->GetBlockStart(start)) {
         // Request is not a total miss but starts before the cache,
         // and there is a clip to fetch from.
         // Not the access pattern for drawing spectrogram or playback,
         // but maybe scrubbing causes this.
         // Move the first buffer into second place, and later
         // refill the first.
         // (This case might be useful when marching backwards through
         // the track, as with scrubbing.)
         mBuffers[0] .swap ( mBuffers[1] );
         fillFirst = true;
         fillSecond = false;
         // Cache is not in a consistent state yet
         mNValidBuffers = 0;
      }

      // Refill buffers as needed
      if (fillFirst) {
         const auto start0 = mPTrack->GetBlockStart(start);
         if (start0 >= 0) {
            const auto len0 = mPTrack->GetBestBlockSize(start0);
            wxASSERT(len0 <= mBufferSize);
            if (!mPTrack->Get(
                  samplePtr(mBuffers[0].data.get()), floatSample, start0, len0,
                  fillZero, mayThrow))
               return 0;
            mBuffers[0].start = start0;
            mBuffers[0].len = len0;
            if (!fillSecond &&
                mBuffers[0].end() != mBuffers[1].start)
               fillSecond = true;
            // Keep the partially updated state consistent:
            mNValidBuffers = fillSecond ? 1 : 2;
         }
         else {
            // Request may fall between the clips of a track.
            // Invalidate all.  WaveTrack::Get() will return zeroes.
            mNValidBuffers = 0;
            fillSecond = false;
         }
      }
      wxASSERT(!fillSecond || mNValidBuffers > 0);
      if (fillSecond) {
         mNValidBuffers = 1;
         const auto end0 = mBuffers[0].end();
         if (end > end0) {
            const auto start1 = mPTrack->GetBlockStart(end0);
            if (start1 == end0) {
               const auto len1 = mPTrack->GetBestBlockSize(start1);
               wxASSERT(len1 <= mBufferSize);
               if (!mPTrack->Get(samplePtr(mBuffers[1].data.get()), floatSample, start1, len1, fillZero, mayThrow))
                  return 0;
               mBuffers[1].start = start1;
               mBuffers[1].len = len1;
               mNValidBuffers = 2;
            }
         }
      }
      wxASSERT(mNValidBuffers < 2 || mBuffers[0].end() == mBuffers[1].start);

      samplePtr buffer = 0;
      auto remaining = len;

      // Possibly get an initial portion that is uncached

      // This may be negative
      const auto initLen =
         mNValidBuffers < 1 ? sampleCount( len )
            : std::min(sampleCount( len ), mBuffers[0].start - start);

      if (initLen > 0) {
         // This might be fetching zeroes between clips
         mOverlapBuffer.Resize(len, format);
         // initLen is not more than len:
         auto sinitLen = initLen.as_size_t();
         if (!mPTrack->Get(mOverlapBuffer.ptr(), format, start, sinitLen,
                           fillZero, mayThrow))
            return 0;
         wxASSERT( sinitLen <= remaining );
         remaining -= sinitLen;
         start += initLen;
         buffer = mOverlapBuffer.ptr() + sinitLen * SAMPLE_SIZE(format);
      }

      // Now satisfy the request from the buffers
      for (int ii = 0; ii < mNValidBuffers && remaining > 0; ++ii) {
         const auto starti = start - mBuffers[ii].start;
         // Treatment of initLen above establishes this loop invariant,
         // and statements below preserve it:
         wxASSERT(starti >= 0);

         // This may be negative
         const auto leni =
            std::min( sampleCount( remaining ), mBuffers[ii].len - starti );
         if (initLen <= 0 && leni == len) {
            // All is contiguous already.  We can completely avoid copying
            // leni is nonnegative, therefore start falls within mBuffers[ii],
            // so starti is bounded between 0 and buffer length
            return samplePtr(mBuffers[ii].data.get() + starti.as_size_t() );
         }
         else if (leni > 0) {
            // leni is nonnegative, therefore start falls within mBuffers[ii]
            // But we can't satisfy all from one buffer, so copy
            if (buffer == 0) {
               mOverlapBuffer.Resize(len, format);
               buffer = mOverlapBuffer.ptr();
            }
            // leni is positive and not more than remaining
            const size_t size = sizeof(float) * leni.as_size_t();
            // starti is less than mBuffers[ii].len and nonnegative
            memcpy(buffer, mBuffers[ii].data.get() + starti.as_size_t(), size);
            wxASSERT( leni <= remaining );
            remaining -= leni.as_size_t();
            start += leni;
            buffer += size;
         }
      }

      if (remaining > 0) {
         // Very big request!
         // Fall back to direct fetch
         if (buffer == 0) {
            mOverlapBuffer.Resize(len, format);
            buffer = mOverlapBuffer.ptr();
         }
         if (!mPTrack->Get(buffer, format, start, remaining, fillZero, mayThrow))
            return 0;
      }

      return mOverlapBuffer.ptr();
   }

   // Cache works only for float format.
   mOverlapBuffer.Resize(len, format);
   if (mPTrack->Get(mOverlapBuffer.ptr(), format, start, len, fillZero, mayThrow))
      return mOverlapBuffer.ptr();
   else
      return 0;
}

void WaveTrackCache::Free()
{
   mBuffers[0].Free();
   mBuffers[1].Free();
   mOverlapBuffer.Free();
   mNValidBuffers = 0;
}
