/**********************************************************************

  Audacity: A Digital Audio Editor

  AudioIO.cpp

  Copyright 2000-2004:
  Dominic Mazzoni
  Joshua Haberman
  Markus Meyer
  Matt Brubeck

  This program is free software; you can redistribute it and/or modify it
  under the terms of the GNU General Public License as published by the Free
  Software Foundation; either version 2 of the License, or (at your option)
  any later version.

********************************************************************//**

\class AudioIO
\brief AudioIO uses the PortAudio library to play and record sound.

  Great care and attention to detail are necessary for understanding and
  modifying this system.  The code in this file is run from three
  different thread contexts: the UI thread, the disk thread (which
  this file creates and maintains; in the code, this is called the
  Audio Thread), and the PortAudio callback thread.
  To highlight this deliniation, the file is divided into three parts
  based on what thread context each function is intended to run in.

  \par MIDI With Audio
  When Audio and MIDI play simultaneously, MIDI synchronizes to Audio.
  This is necessary because the Audio sample clock is not the same
  hardware as the system time used to schedule MIDI messages. MIDI
  is synchronized to Audio because it is simple to pause or rush
  the dispatch of MIDI messages, but generally impossible to pause
  or rush synchronous audio samples (without distortion).

  \par
  MIDI output is driven by yet another thread. In principle, we could
  output timestamped MIDI data at the same time we fill audio buffers
  from disk, but audio buffers are filled far in advance of playback
  time, and there is a lower latency thread (PortAudio's callback) that
  actually sends samples to the output device. The relatively low
  latency to the output device allows Audacity to stop audio output
  quickly. We want the same behavior for MIDI, but there is not
  periodic callback from PortMidi (because MIDI is asynchronous), so
  this function is performed by the MidiThread class.

  \par
  When Audio is running, MIDI is synchronized to Audio. Globals are set
  in the Audio callback (audacityAudioCallback) for use by a time
  function that reports milliseconds to PortMidi. (Details below.)

  \par MIDI Without Audio
  When Audio is not running, PortMidi uses its own millisecond timer
  since there is no audio to synchronize to. (Details below.)

  \par Implementation Notes and Details for MIDI
  When opening devices, successAudio and successMidi indicate errors
  if false, so normally both are true. Use playbackChannels,
  captureChannels and mMidiPlaybackTracks.IsEmpty() to determine if
  Audio or MIDI is actually in use.

  \par Audio Time
  Normally, the current time during playback is given by the variable
  mTime. mTime normally advances by frames / samplerate each time an
  audio buffer is output by the audio callback. However, Audacity has
  a speed control that can perform continuously variable time stretching
  on audio. This is achieved in two places: the playback "mixer" that
  generates the samples for output processes the audio according to
  the speed control. In a separate algorithm, the audio callback updates
  mTime by (frames / samplerate) * factor, where factor reflects the
  speed at mTime. This effectively integrates speed to get position.
  Negative speeds are allowed too, for instance in scrubbing.

  \par The Big Picture
@verbatim

Sample
Time (in seconds, = total_sample_count / sample_rate)
  ^
  |                                             /         /
  |             y=x-mSystemTimeMinusAudioTime /         /
  |                                         /     #   /
  |                                       /         /
  |                                     /   # <- callbacks (#) showing
  |                                   /#        /   lots of timing jitter.
  |       top line is "full buffer" /         /     Some are later,
  |                     condition /         /       indicating buffer is
  |                             /         /         getting low. Plot
  |                           /     #   /           shows sample time
  |                         /    #    /             (based on how many
  |                       /    #    /               samples previously
  |                     /         /                 *written*) vs. real
  |                   / #       /                   time.
  |                 /<------->/ audio latency
  |               /#       v/
  |             /         / bottom line is "empty buffer"
  |           /   #     /      condition = DAC output time =
  |         /         /
  |       /      # <-- rapid callbacks as buffer is filled
  |     /         /
0 +...+---------#---------------------------------------------------->
  0 ^ |         |                                            real time
    | |         first callback time
    | mSystemMinusAudioTime
    |
    Probably the actual real times shown in this graph are very large
    in practice (> 350,000 sec.), so the X "origin" might be when
    the computer was booted or 1970 or something.


@endverbatim

  To estimate the true DAC time (needed to synchronize MIDI), we need
  a mapping from track time to DAC time. The estimate is the theoretical
  time of the full buffer (top diagonal line) + audio latency. To
  estimate the top diagonal line, we "draw" the line to be at least
  as high as any sample time corresponding to a callback (#), and we
  slowly lower the line in case the sample clock is slow or the system
  clock is fast, preventing the estimated line from drifting too far
  from the actual callback observations. The line is occasionally
  "bumped" up by new callback observations, but continuously
  "lowered" at a very low rate.  All adjustment is accomplished
  by changing mSystemMinusAudioTime, shown here as the X-intercept.\n
    theoreticalFullBufferTime = realTime - mSystemMinusAudioTime\n
  To estimate audio latency, notice that the first callback happens on
  an empty buffer, but the buffer soon fills up. This will cause a rapid
  re-estimation of mSystemMinusAudioTime. (The first estimate of
  mSystemMinusAudioTime will simply be the real time of the first
  callback time.) By watching these changes, which happen within ms of
  starting, we can estimate the buffer size and thus audio latency.
  So, to map from track time to real time, we compute:\n
    DACoutputTime = trackTime + mSystemMinusAudioTime\n
  There are some additional details to avoid counting samples while
  paused or while waiting for initialization, MIDI latency, etc.
  Also, in the code, track time is measured with respect to the track
  origin, so there's an extra term to add (mT0) if you start somewhere
  in the middle of the track.
  Finally, when a callback occurs, you might expect there is room in
  the output buffer for the requested frames, so maybe the "full buffer"
  sample time should be based not on the first sample of the callback, but
  the last sample time + 1 sample. I suspect, at least on Linux, that the
  callback occurs as soon as the last callback completes, so the buffer is
  really full, and the callback thread is going to block waiting for space
  in the output buffer.

  \par Midi Time
  MIDI is not warped according to the speed control. This might be
  something that should be changed. (Editorial note: Wouldn't it
  make more sense to display audio at the correct time and allow
  users to stretch audio the way they can stretch MIDI?) For now,
  MIDI plays at 1 second per second, so it requires an unwarped clock.
  In fact, MIDI time synchronization requires a millisecond clock that
  does not pause. Note that mTime will stop progress when the Pause
  button is pressed, even though audio samples (zeros) continue to
  be output.

  \par
  Therefore, we define the following interface for MIDI timing:
  \li \c AudioTime() is the time based on all samples written so far, including zeros output during pauses. AudioTime() is based on the start location mT0, not zero.
  \li \c PauseTime() is the amount of time spent paused, based on a count of zero-padding samples output.
  \li \c MidiTime() is an estimate in milliseconds of the current audio output time + 1s. In other words, what audacity track time corresponds to the audio (plus pause insertions) at the DAC output?

  \par AudioTime() and PauseTime() computation
  AudioTime() is simply mT0 + mNumFrames / mRate.
  mNumFrames is incremented in each audio callback. Similarly, PauseTime()
  is mNumPauseFrames / mRate. mNumPauseFrames is also incremented in
  each audio callback when a pause is in effect or audio output is ready to start.

  \par MidiTime() computation
  MidiTime() is computed based on information from PortAudio's callback,
  which estimates the system time at which the current audio buffer will
  be output. Consider the (unimplemented) function RealToTrack() that
  maps real audio write time to track time. If writeTime is the system
  time for the first sample of the current output buffer, and
  if we are in the callback, so AudioTime() also refers to the first sample
  of the buffer, then \n
  RealToTrack(writeTime) = AudioTime() - PauseTime()\n
  We want to know RealToTrack of the current time (when we are not in the
  callback, so we use this approximation for small d: \n
  RealToTrack(t + d) = RealToTrack(t) + d, or \n
  Letting t = writeTime and d = (systemTime - writeTime), we can
  substitute to get:\n
  RealToTrack(systemTime)
     = RealToTrack(writeTime) + systemTime - writeTime\n
     = AudioTime() - PauseTime() + (systemTime - writeTime) \n
  MidiTime() should include pause time, so that it increases smoothly,
  and audioLatency so that MidiTime() corresponds to the time of audio
  output rather than audio write times.  Also MidiTime() is offset by 1
  second to avoid negative time at startup, so add 1: \n
  MidiTime(systemTime) in seconds\n
     = RealToTrack(systemTime) + PauseTime() - audioLatency + 1 \n
     = AudioTime() + (systemTime - writeTime) - audioLatency + 1 \n
  (Note that audioLatency is called mAudioOutLatency in the code.)
  When we schedule a MIDI event with track time TT, we need
  to map TT to a PortMidi timestamp. The PortMidi timestamp is exactly
  MidiTime(systemTime) in ms units, and \n
     MidiTime(x) = RealToTrack(x) + PauseTime() + 1, so \n
     timestamp = TT + PauseTime() + 1 - midiLatency \n
  Note 1: The timestamp is incremented by the PortMidi stream latency
  (midiLatency) so we subtract midiLatency here for the timestamp
  passed to PortMidi. \n
  Note 2: Here, we're setting x to the time at which RealToTrack(x) = TT,
  so then MidiTime(x) is the desired timestamp. To be completely
  correct, we should assume that MidiTime(x + d) = MidiTime(x) + d,
  and consider that we compute MidiTime(systemTime) based on the
  *current* system time, but we really want the MidiTime(x) for some
  future time corresponding when MidiTime(x) = TT.)

  \par
  Also, we should assume PortMidi was opened with mMidiLatency, and that
  MIDI messages become sound with a delay of mSynthLatency. Therefore,
  the final timestamp calculation is: \n
     timestamp = TT + PauseTime() + 1 - (mMidiLatency + mSynthLatency) \n
  (All units here are seconds; some conversion is needed in the code.)

  \par
  The difference AudioTime() - PauseTime() is the time "cursor" for
  MIDI. When the speed control is used, MIDI and Audio will become
  unsynchronized. In particular, MIDI will not be synchronized with
  the visual cursor, which moves with scaled time reported in mTime.

  \par Timing in Linux
  It seems we cannot get much info from Linux. We can read the time
  when we get a callback, and we get a variable frame count (it changes
  from one callback to the next). Returning to the RealToTrack()
  equations above: \n
  RealToTrack(outputTime) = AudioTime() - PauseTime() - bufferDuration \n
  where outputTime should be PortAudio's estimate for the most recent output
  buffer, but at least on my Dell Latitude E7450, PortAudio is getting zero
  from ALSA, so we need to find a proxy for this.

  \par Estimating outputTime (Plan A, assuming double-buffered, fixed-size buffers, please skip to Plan B)
  One can expect the audio callback to happen as soon as there is room in
  the output for another block of samples, so we could just measure system
  time at the top of the callback. Then we could add the maximum delay
  buffered in the system. E.g. if there is simple double buffering and the
  callback is computing one of the buffers, the callback happens just as
  one of the buffers empties, meaning the other buffer is full, so we have
  exactly one buffer delay before the next computed sample is output.

  If computation falls behind a bit, the callback will be later, so the
  delay to play the next computed sample will be less. I think a reasonable
  way to estimate the actual output time is to assume that the computer is
  mostly keeping up and that *most* callbacks will occur immediately when
  there is space. Note that the most likely reason for the high-priority
  audio thread to fall behind is the callback itself, but the start of the
  callback should be pretty consistently keeping up.

  Also, we do not have to have a perfect estimate of the time. Suppose we
  estimate a linear mapping from sample count to system time by saying
  that the sample count maps to the system time at the most recent callback,
  and set the slope to 1% slower than real time (as if the sample clock is
  slow). Now, at each callback, if the callback seems to occur earlier than
  expected, we can adjust the mapping to be earlier. The earlier the
  callback, the more accurate it must be. On the other hand, if the callback
  is later than predicted, it must be a delayed callback (or else the
  sample clock is more than 1% slow, which is really a hardware problem.)
  How bad can this be? Assuming callbacks every 30ms (this seems to be what
  I'm observing in a default setup), you'll be a maximum of 1ms off even if
  2 out of 3 callbacks are late. This is pretty reasonable given that
  PortMIDI clock precision is 1ms. If buffers are larger and callback timing
  is more erratic, errors will be larger, but even a few ms error is
  probably OK.

  \par Estimating outputTime (Plan B, variable framesPerBuffer in callback, please skip to Plan C)
  ALSA is complicated because we get varying values of
  framesPerBuffer from callback to callback. Assume you get more frames
  when the callback is later (because there is more accumulated input to
  deliver and more more accumulated room in the output buffers). So take
  the current time and subtract the duration of the frame count in the
  current callback. This should be a time position that is relatively
  jitter free (because we estimated the lateness by frame count and
  subtracted that out). This time position intuitively represents the
  current ADC time, or if no input, the time of the tail of the output
  buffer. If we wanted DAC time, we'd have to add the total output
  buffer duration, which should be reported by PortAudio. (If PortAudio
  is wrong, we'll be systematically shifted in time by the error.)

  Since there is still bound to be jitter, we can smooth these estimates.
  First, we will assume a linear mapping from system time to audio time
  with slope = 1, so really it's just the offset we need, which is going
  to be a double that we can read/write atomically without locks or
  anything fancy. (Maybe it should be "volatile".)

  To improve the estimate, we get a new offset every callback, so we can
  create a "smooth" offset by using a simple regression model (also
  this could be seen as a first order filter). The following formula
  updates smooth_offset with a new offset estimate in the callback:
      smooth_offset = smooth_offset * 0.9 + new_offset_estimate * 0.1
  Since this is smooth, we'll have to be careful to give it a good initial
  value to avoid a long convergence.

  \par Estimating outputTime (Plan C)
  ALSA is complicated because we get varying values of
  framesPerBuffer from callback to callback. It seems there is a lot
  of variation in callback times and buffer space. One solution would
  be to go to fixed size double buffer, but Audacity seems to work
  better as is, so Plan C is to rely on one invariant which is that
  the output buffer cannot overflow, so there's a limit to how far
  ahead of the DAC time we can be writing samples into the
  buffer. Therefore, we'll assume that the audio clock runs slow by
  about 0.2% and we'll assume we're computing at that rate. If the
  actual output position is ever ahead of the computed position, we'll
  increase the computed position to the actual position. Thus whenever
  the buffer is less than near full, we'll stay ahead of DAC time,
  falling back at a rate of about 0.2% until eventually there's
  another near-full buffer callback that will push the time back ahead.

  \par Interaction between MIDI, Audio, and Pause
  When Pause is used, PauseTime() will increase at the same rate as
  AudioTime(), and no more events will be output. Because of the
  time advance of mAudioOutputLatency + MIDI_SLEEP + latency and the
  fact that
  AudioTime() advances stepwise by mAudioBufferDuration, some extra MIDI
  might be output, but the same is true of audio: something like
  mAudioOutputLatency audio samples will be in the output buffer
  (with up to mAudioBufferDuration additional samples, depending on
  when the Pause takes effect). When playback is resumed, there will
  be a slight delay corresponding to the extra data previously sent.
  Again, the same is true of audio. Audio and MIDI will not pause and
  resume at exactly the same times, but their pause and resume times
  will be within the low tens of milliseconds, and the streams will
  be synchronized in any case. I.e. if audio pauses 10ms earlier than
  MIDI, it will resume 10ms earlier as well.

  \par PortMidi Latency Parameter
  PortMidi has a "latency" parameter that is added to all timestamps.
  This value must be greater than zero to enable timestamp-based timing,
  but serves no other function, so we will set it to 1. All timestamps
  must then be adjusted down by 1 before messages are sent. This
  adjustment is on top of all the calculations described above. It just
  seem too complicated to describe everything in complete detail in one
  place.

  \par Midi with a time track
  When a variable-speed time track is present, MIDI events are output
  with the times used by the time track (rather than the raw times).
  This ensures MIDI is synchronized with audio.

  \par Midi While Recording Only or Without Audio Playback
  To reduce duplicate code and to ensure recording is synchronised with
  MIDI, a portaudio stream will always be used, even when there is no
  actual audio output.  For recording, this ensures that the recorded
  audio will by synchronized with the MIDI (otherwise, it gets out-of-
  sync if played back with correct timing).

  \par NoteTrack PlayLooped
  When mPlayLooped is true, output is supposed to loop from mT0 to mT1.
  For NoteTracks, we interpret this to mean that any note-on or control
  change in the range mT0 <= t < mT1 is sent (notes that start before
  mT0 are not played even if they extend beyond mT0). Then, all notes
  are turned off. Events in the range mT0 <= t < mT1 are then repeated,
  offset by (mT1 - mT0), etc.  We do NOT go back to the beginning and
  play all control changes (update events) up to mT0, nor do we "undo"
  any state changes between mT0 and mT1.

  \par NoteTrack PlayLooped Implementation
  The mIterator object (an Alg_iterator) returns NULL when there are
  no more events scheduled before mT1. At mT1, we want to output
  all notes off messages, but the FillMidiBuffers() loop will exit
  if mNextEvent is NULL, so we create a "fake" mNextEvent for this
  special "event" of sending all notes off. After that, we destroy
  the iterator and use PrepareMidiIterator() to set up a NEW one.
  At each iteration, time must advance by (mT1 - mT0), so the
  accumulated complete loop time (in "unwarped," track time) is computed
  by MidiLoopOffset().

  \todo run through all functions called from audio and portaudio threads
  to verify they are thread-safe. Note that synchronization of the style:
  "A sets flag to signal B, B clears flag to acknowledge completion"
  is not thread safe in a general multiple-CPU context. For example,
  B can write to a buffer and set a completion flag. The flag write can
  occur before the buffer write due to out-of-order execution. Then A
  can see the flag and read the buffer before buffer writes complete.

*//****************************************************************//**

\class AudioThread
\brief Defined different on Mac and other platforms (on Mac it does not
use wxWidgets wxThread), this class sits in a thread loop reading and
writing audio.

*//****************************************************************//**

\class AudioIOListener
\brief Monitors record play start/stop and new blockfiles.  Has 
callbacks for these events.

*//****************************************************************//**

\class AudioIOStartStreamOptions
\brief struct holding stream options, including a pointer to the 
AudioIOListener and whether the playback is looped.

*//*******************************************************************/

#include "Audacity.h"
#include "Experimental.h"
#include "AudioIO.h"
#include "float_cast.h"

#include <cfloat>
#include <math.h>
#include <stdlib.h>
#include <algorithm>

#ifdef __WXMSW__
#include <malloc.h>
#endif

#ifdef HAVE_ALLOCA_H
#include <alloca.h>
#endif

#if USE_PORTMIXER
#include "portmixer.h"
#endif

#include <wx/log.h>
#include <wx/textctrl.h>
#include <wx/timer.h>
#include <wx/intl.h>
#include <wx/debug.h>
#include <wx/sstream.h>
#include <wx/txtstrm.h>

#include "AudacityApp.h"
#include "AudacityException.h"
#include "Mix.h"
#include "MixerBoard.h"
#include "Resample.h"
#include "RingBuffer.h"
#include "prefs/GUISettings.h"
#include "Prefs.h"
#include "Project.h"
#include "WaveTrack.h"
#include "AutoRecovery.h"

#include "toolbars/ControlToolBar.h"
#include "widgets/Meter.h"
#include "widgets/ErrorDialog.h"
#include "widgets/Warning.h"

using std::max;
using std::min;

std::unique_ptr<AudioIO> ugAudioIO;
AudioIO *gAudioIO{};

wxDEFINE_EVENT(EVT_AUDIOIO_PLAYBACK, wxCommandEvent);
wxDEFINE_EVENT(EVT_AUDIOIO_CAPTURE, wxCommandEvent);
wxDEFINE_EVENT(EVT_AUDIOIO_MONITOR, wxCommandEvent);

// static
int AudioIO::mNextStreamToken = 0;
int AudioIO::mCachedPlaybackIndex = -1;
std::vector<long> AudioIO::mCachedPlaybackRates;
int AudioIO::mCachedCaptureIndex = -1;
std::vector<long> AudioIO::mCachedCaptureRates;
std::vector<long> AudioIO::mCachedSampleRates;
double AudioIO::mCachedBestRateIn = 0.0;
double AudioIO::mCachedBestRateOut;

enum {
   // This is the least positive latency we can
   // specify to Pm_OpenOutput, 1 ms, which prevents immediate
   // scheduling of events:
   MIDI_MINIMAL_LATENCY_MS = 1
};

const int AudioIO::StandardRates[] = {
   8000,
   11025,
   16000,
   22050,
   32000,
   44100,
   48000,
   88200,
   96000,
   176400,
   192000,
   352800,
   384000
};
const int AudioIO::NumStandardRates = sizeof(AudioIO::StandardRates) /
                                      sizeof(AudioIO::StandardRates[0]);
const int AudioIO::RatesToTry[] = {
   8000,
   9600,
   11025,
   12000,
   15000,
   16000,
   22050,
   24000,
   32000,
   44100,
   48000,
   88200,
   96000,
   176400,
   192000,
   352800,
   384000
};
const int AudioIO::NumRatesToTry = sizeof(AudioIO::RatesToTry) /
                                      sizeof(AudioIO::RatesToTry[0]);

int audacityAudioCallback(const void *inputBuffer, void *outputBuffer,
                          unsigned long framesPerBuffer,
                          const PaStreamCallbackTimeInfo *timeInfo,
                          PaStreamCallbackFlags statusFlags, void *userData );

//////////////////////////////////////////////////////////////////////
//
//     class AudioThread - declaration and glue code
//
//////////////////////////////////////////////////////////////////////

#ifdef __WXMAC__

// On Mac OS X, it's better not to use the wxThread class.
// We use our own implementation based on pthreads instead.

#include <pthread.h>
#include <time.h>

class AudioThread {
 public:
   typedef int ExitCode;
   AudioThread() { mDestroy = false; mThread = NULL; }
   virtual ExitCode Entry();
   void Create() {}
   void Delete() {
      mDestroy = true;
      pthread_join(mThread, NULL);
   }
   bool TestDestroy() { return mDestroy; }
   void Sleep(int ms) {
      struct timespec spec;
      spec.tv_sec = 0;
      spec.tv_nsec = ms * 1000 * 1000;
      nanosleep(&spec, NULL);
   }
   static void *callback(void *p) {
      AudioThread *th = (AudioThread *)p;
      return (void *)th->Entry();
   }
   void Run() {
      pthread_create(&mThread, NULL, callback, this);
   }
 private:
   bool mDestroy;
   pthread_t mThread;

};

#else

// The normal wxThread-derived AudioThread class for all other
// platforms:
class AudioThread /* not final */ : public wxThread {
 public:
   AudioThread():wxThread(wxTHREAD_JOINABLE) {}
   ExitCode Entry() override;
};

#endif


//////////////////////////////////////////////////////////////////////
//
//     UI Thread Context
//
//////////////////////////////////////////////////////////////////////

void InitAudioIO()
{
   ugAudioIO.reset(safenew AudioIO());
   gAudioIO = ugAudioIO.get();
   gAudioIO->mThread->Run();

   // Make sure device prefs are initialized
   if (gPrefs->Read(wxT("AudioIO/RecordingDevice"), wxT("")) == wxT("")) {
      int i = AudioIO::getRecordDevIndex();
      const PaDeviceInfo *info = Pa_GetDeviceInfo(i);
      if (info) {
         gPrefs->Write(wxT("/AudioIO/RecordingDevice"), DeviceName(info));
         gPrefs->Write(wxT("/AudioIO/Host"), HostName(info));
      }
   }

   if (gPrefs->Read(wxT("AudioIO/PlaybackDevice"), wxT("")) == wxT("")) {
      int i = AudioIO::getPlayDevIndex();
      const PaDeviceInfo *info = Pa_GetDeviceInfo(i);
      if (info) {
         gPrefs->Write(wxT("/AudioIO/PlaybackDevice"), DeviceName(info));
         gPrefs->Write(wxT("/AudioIO/Host"), HostName(info));
      }
   }

   gPrefs->Flush();
}

void DeinitAudioIO()
{
   ugAudioIO.reset();
}

wxString DeviceName(const PaDeviceInfo* info)
{
   wxString infoName = wxSafeConvertMB2WX(info->name);

   return infoName;
}

wxString HostName(const PaDeviceInfo* info)
{
   wxString hostapiName = wxSafeConvertMB2WX(Pa_GetHostApiInfo(info->hostApi)->name);

   return hostapiName;
}

bool AudioIO::ValidateDeviceNames(const wxString &play, const wxString &rec)
{
   const PaDeviceInfo *pInfo = Pa_GetDeviceInfo(AudioIO::getPlayDevIndex(play));
   const PaDeviceInfo *rInfo = Pa_GetDeviceInfo(AudioIO::getRecordDevIndex(rec));

   if (!pInfo || !rInfo || pInfo->hostApi != rInfo->hostApi) {
      return false;
   }

   return true;
}

AudioIO::AudioIO()
{
   mAudioThreadShouldCallFillBuffersOnce = false;
   mAudioThreadFillBuffersLoopRunning = false;
   mAudioThreadFillBuffersLoopActive = false;
   mPortStreamV19 = NULL;

   mStreamToken = 0;

   mLastPaError = paNoError;

   mLastRecordingOffset = 0.0;
   mNumCaptureChannels = 0;
   mPaused = false;
   mPlayMode = PLAY_STRAIGHT;

   mListener = NULL;
   mUpdateMeters = false;
   mUpdatingMeters = false;

   mOwningProject = NULL;
   mOutputMeter = NULL;

   PaError err = Pa_Initialize();

   if (err != paNoError) {
      wxString errStr = _("Could not find any audio devices.\n");
      errStr += _("You will not be able to play or record audio.\n\n");
      wxString paErrStr = LAT1CTOWX(Pa_GetErrorText(err));
      if (!paErrStr.IsEmpty())
         errStr += _("Error: ")+paErrStr;
      // XXX: we are in libaudacity, popping up dialogs not allowed!  A
      // long-term solution will probably involve exceptions
      AudacityMessageBox(errStr, _("Error Initializing Audio"), wxICON_ERROR|wxOK);

      // Since PortAudio is not initialized, all calls to PortAudio
      // functions will fail.  This will give reasonable behavior, since
      // the user will be able to do things not relating to audio i/o,
      // but any attempt to play or record will simply fail.
   }

   // Start thread
   mThread = std::make_unique<AudioThread>();
   mThread->Create();

#if defined(USE_PORTMIXER)
   mPortMixer = NULL;
   mPreviousHWPlaythrough = -1.0;
   HandleDeviceChange();
#else
   mEmulateMixerOutputVol = true;
   mMixerOutputVol = 1.0;
   mInputMixerWorks = false;
#endif

   mLastPlaybackTimeMillis = 0;
}

AudioIO::~AudioIO()
{
#if defined(USE_PORTMIXER)
   if (mPortMixer) {
      #if __WXMAC__
      if (Px_SupportsPlaythrough(mPortMixer) && mPreviousHWPlaythrough >= 0.0)
         Px_SetPlaythrough(mPortMixer, mPreviousHWPlaythrough);
         mPreviousHWPlaythrough = -1.0;
      #endif
      Px_CloseMixer(mPortMixer);
      mPortMixer = NULL;
   }
#endif

   // FIXME: ? TRAP_ERR.  Pa_Terminate probably OK if err without reporting.
   Pa_Terminate();

   /* Delete is a "graceful" way to stop the thread.
      (Kill is the not-graceful way.) */

   // This causes reentrancy issues during application shutdown
   // wxTheApp->Yield();

   mThread->Delete();
   mThread.reset();

   gAudioIO = nullptr;
}

void AudioIO::SetMixer(int inputSource)
{
#if defined(USE_PORTMIXER)
   int oldRecordSource = Px_GetCurrentInputSource(mPortMixer);
   if ( inputSource != oldRecordSource )
         Px_SetCurrentInputSource(mPortMixer, inputSource);
#endif
}
void AudioIO::SetMixer(int inputSource, float recordVolume,
                       float playbackVolume)
{
   mMixerOutputVol = playbackVolume;

#if defined(USE_PORTMIXER)
   PxMixer *mixer = mPortMixer;

   if( mixer )
   {
      float oldRecordVolume = Px_GetInputVolume(mixer);
      float oldPlaybackVolume = Px_GetPCMOutputVolume(mixer);

      SetMixer(inputSource);
      if( oldRecordVolume != recordVolume )
         Px_SetInputVolume(mixer, recordVolume);
      if( oldPlaybackVolume != playbackVolume )
         Px_SetPCMOutputVolume(mixer, playbackVolume);

      return;
   }
#endif
}

void AudioIO::GetMixer(int *recordDevice, float *recordVolume,
                       float *playbackVolume)
{
#if defined(USE_PORTMIXER)

   PxMixer *mixer = mPortMixer;

   if( mixer )
   {
      *recordDevice = Px_GetCurrentInputSource(mixer);

      if (mInputMixerWorks)
         *recordVolume = Px_GetInputVolume(mixer);
      else
         *recordVolume = 1.0f;

      if (mEmulateMixerOutputVol)
         *playbackVolume = mMixerOutputVol;
      else
         *playbackVolume = Px_GetPCMOutputVolume(mixer);

      return;
   }

#endif

   *recordDevice = 0;
   *recordVolume = 1.0f;
   *playbackVolume = mMixerOutputVol;
}

bool AudioIO::InputMixerWorks()
{
   return mInputMixerWorks;
}

bool AudioIO::OutputMixerEmulated()
{
   return mEmulateMixerOutputVol;
}

wxArrayString AudioIO::GetInputSourceNames()
{
#if defined(USE_PORTMIXER)

   wxArrayString deviceNames;

   if( mPortMixer )
   {
      int numSources = Px_GetNumInputSources(mPortMixer);
      for( int source = 0; source < numSources; source++ )
         deviceNames.Add(wxString(wxSafeConvertMB2WX(Px_GetInputSourceName(mPortMixer, source))));
   }
   else
   {
      wxLogDebug(wxT("AudioIO::GetInputSourceNames(): PortMixer not initialised!"));
   }

   return deviceNames;

#else

   wxArrayString blank;

   return blank;

#endif
}

void AudioIO::HandleDeviceChange()
{
   // This should not happen, but it would screw things up if it did.
   // Vaughan, 2010-10-08: But it *did* happen, due to a bug, and nobody
   // caught it because this method just returned. Added wxASSERT().
   wxASSERT(!IsStreamActive());
   if (IsStreamActive())
      return;

   // get the selected record and playback devices
   const int playDeviceNum = getPlayDevIndex();
   const int recDeviceNum = getRecordDevIndex();

   // If no change needed, return
   if (mCachedPlaybackIndex == playDeviceNum &&
       mCachedCaptureIndex == recDeviceNum)
       return;

   // cache playback/capture rates
   mCachedPlaybackRates = GetSupportedPlaybackRates(playDeviceNum);
   mCachedCaptureRates = GetSupportedCaptureRates(recDeviceNum);
   mCachedSampleRates = GetSupportedSampleRates(playDeviceNum, recDeviceNum);
   mCachedPlaybackIndex = playDeviceNum;
   mCachedCaptureIndex = recDeviceNum;
   mCachedBestRateIn = 0.0;

#if defined(USE_PORTMIXER)

   // if we have a PortMixer object, close it down
   if (mPortMixer) {
      #if __WXMAC__
      // on the Mac we must make sure that we restore the hardware playthrough
      // state of the sound device to what it was before, because there isn't
      // a UI for this (!)
      if (Px_SupportsPlaythrough(mPortMixer) && mPreviousHWPlaythrough >= 0.0)
         Px_SetPlaythrough(mPortMixer, mPreviousHWPlaythrough);
         mPreviousHWPlaythrough = -1.0;
      #endif
      Px_CloseMixer(mPortMixer);
      mPortMixer = NULL;
   }

   // that might have given us no rates whatsoever, so we have to guess an
   // answer to do the next bit
   int numrates = mCachedSampleRates.size();
   int highestSampleRate;
   if (numrates > 0)
   {
      highestSampleRate = mCachedSampleRates[numrates - 1];
   }
   else
   {  // we don't actually have any rates that work for Rec and Play. Guess one
      // to use for messing with the mixer, which doesn't actually do either
      highestSampleRate = 44100;
      // mCachedSampleRates is still empty, but it's not used again, so
      // can ignore
   }
   mInputMixerWorks = false;
   mEmulateMixerOutputVol = true;
   mMixerOutputVol = 1.0;

   int error;
   // This tries to open the device with the samplerate worked out above, which
   // will be the highest available for play and record on the device, or
   // 44.1kHz if the info cannot be fetched.

   PaStream *stream;

   PaStreamParameters playbackParameters;

   playbackParameters.device = playDeviceNum;
   playbackParameters.sampleFormat = paFloat32;
   playbackParameters.hostApiSpecificStreamInfo = NULL;
   playbackParameters.channelCount = 1;
   if (Pa_GetDeviceInfo(playDeviceNum))
      playbackParameters.suggestedLatency =
         Pa_GetDeviceInfo(playDeviceNum)->defaultLowOutputLatency;
   else
      playbackParameters.suggestedLatency = DEFAULT_LATENCY_CORRECTION/1000.0;

   PaStreamParameters captureParameters;

   captureParameters.device = recDeviceNum;
   captureParameters.sampleFormat = paFloat32;;
   captureParameters.hostApiSpecificStreamInfo = NULL;
   captureParameters.channelCount = 1;
   if (Pa_GetDeviceInfo(recDeviceNum))
      captureParameters.suggestedLatency =
         Pa_GetDeviceInfo(recDeviceNum)->defaultLowInputLatency;
   else
      captureParameters.suggestedLatency = DEFAULT_LATENCY_CORRECTION/1000.0;

   // try opening for record and playback
   error = Pa_OpenStream(&stream,
                         &captureParameters, &playbackParameters,
                         highestSampleRate, paFramesPerBufferUnspecified,
                         paClipOff | paDitherOff,
                         audacityAudioCallback, NULL);

   if (!error) {
      // Try portmixer for this stream
      mPortMixer = Px_OpenMixer(stream, 0);
      if (!mPortMixer) {
         Pa_CloseStream(stream);
         error = true;
      }
   }

   // if that failed, try just for record
   if( error ) {
      error = Pa_OpenStream(&stream,
                            &captureParameters, NULL,
                            highestSampleRate, paFramesPerBufferUnspecified,
                            paClipOff | paDitherOff,
                            audacityAudioCallback, NULL);

      if (!error) {
         mPortMixer = Px_OpenMixer(stream, 0);
         if (!mPortMixer) {
            Pa_CloseStream(stream);
            error = true;
         }
      }
   }

   // finally, try just for playback
   if ( error ) {
      error = Pa_OpenStream(&stream,
                            NULL, &playbackParameters,
                            highestSampleRate, paFramesPerBufferUnspecified,
                            paClipOff | paDitherOff,
                            audacityAudioCallback, NULL);

      if (!error) {
         mPortMixer = Px_OpenMixer(stream, 0);
         if (!mPortMixer) {
            Pa_CloseStream(stream);
            error = true;
         }
      }
   }

   // FIXME: TRAP_ERR errors in HandleDeviceChange not reported.
   // if it's still not working, give up
   if( error )
      return;

   // Set input source
#if USE_PORTMIXER
   int sourceIndex;
   if (gPrefs->Read(wxT("/AudioIO/RecordingSourceIndex"), &sourceIndex)) {
      if (sourceIndex >= 0) {
         //the current index of our source may be different because the stream
         //is a combination of two devices, so update it.
         sourceIndex = getRecordSourceIndex(mPortMixer);
         if (sourceIndex >= 0)
            SetMixer(sourceIndex);
      }
   }
#endif

   // Determine mixer capabilities - if it doesn't support control of output
   // signal level, we emulate it (by multiplying this value by all outgoing
   // samples)

   mMixerOutputVol = Px_GetPCMOutputVolume(mPortMixer);
   mEmulateMixerOutputVol = false;
   Px_SetPCMOutputVolume(mPortMixer, 0.0);
   if (Px_GetPCMOutputVolume(mPortMixer) > 0.1)
      mEmulateMixerOutputVol = true;
   Px_SetPCMOutputVolume(mPortMixer, 0.2f);
   if (Px_GetPCMOutputVolume(mPortMixer) < 0.1 ||
       Px_GetPCMOutputVolume(mPortMixer) > 0.3)
      mEmulateMixerOutputVol = true;
   Px_SetPCMOutputVolume(mPortMixer, mMixerOutputVol);

   float inputVol = Px_GetInputVolume(mPortMixer);
   mInputMixerWorks = true;   // assume it works unless proved wrong
   Px_SetInputVolume(mPortMixer, 0.0);
   if (Px_GetInputVolume(mPortMixer) > 0.1)
      mInputMixerWorks = false;  // can't set to zero
   Px_SetInputVolume(mPortMixer, 0.2f);
   if (Px_GetInputVolume(mPortMixer) < 0.1 ||
       Px_GetInputVolume(mPortMixer) > 0.3)
      mInputMixerWorks = false;  // can't set level accurately
   Px_SetInputVolume(mPortMixer, inputVol);

   Pa_CloseStream(stream);

   mMixerOutputVol = 1.0;

#endif   // USE_PORTMIXER
}

static PaSampleFormat AudacityToPortAudioSampleFormat(sampleFormat format)
{
   switch(format) {
   case int16Sample:
      return paInt16;
   case int24Sample:
      return paInt24;
   case floatSample:
   default:
      return paFloat32;
   }
}

bool AudioIO::StartPortAudioStream(double sampleRate,
                                   unsigned int numPlaybackChannels,
                                   unsigned int numCaptureChannels,
                                   sampleFormat captureFormat)
{
   mOwningProject = GetActiveProject();

   // PRL:  Protection from crash reported by David Bailes, involving starting
   // and stopping with frequent changes of active window, hard to reproduce
   if (!mOwningProject)
      return false;

   mInputMeter.Release();
   mOutputMeter = NULL;

   mLastPaError = paNoError;
   // pick a rate to do the audio I/O at, from those available. The project
   // rate is suggested, but we may get something else if it isn't supported
   mRate = GetBestRate(numCaptureChannels > 0, numPlaybackChannels > 0, sampleRate);

   // July 2016 (Carsten and Uwe)
   // BUG 193: Tell PortAudio sound card will handle 24 bit (under DirectSound) using 
   // userData.
   int captureFormat_saved = captureFormat;
   // Special case: Our 24-bit sample format is different from PortAudio's
   // 3-byte packed format. So just make PortAudio return float samples,
   // since we need float values anyway to apply the gain.
   // ANSWER-ME: So we *never* actually handle 24-bit?! This causes mCapture to 
   // be set to floatSample below.
   // JKC: YES that's right.  Internally Audacity uses float, and float has space for
   // 24 bits as well as exponent.  Actual 24 bit would require packing and
   // unpacking unaligned bytes and would be inefficient.
   // ANSWER ME: is floatSample 64 bit on 64 bit machines?
   if (captureFormat == int24Sample)
      captureFormat = floatSample;

   mNumPlaybackChannels = numPlaybackChannels;
   mNumCaptureChannels = numCaptureChannels;

   bool usePlayback = false, useCapture = false;
   PaStreamParameters playbackParameters{};
   PaStreamParameters captureParameters{};

   double latencyDuration = DEFAULT_LATENCY_DURATION;
   gPrefs->Read(wxT("/AudioIO/LatencyDuration"), &latencyDuration);

   if( numPlaybackChannels > 0)
   {
      usePlayback = true;

      // this sets the device index to whatever is "right" based on preferences,
      // then defaults
      playbackParameters.device = getPlayDevIndex();

      const PaDeviceInfo *playbackDeviceInfo;
      playbackDeviceInfo = Pa_GetDeviceInfo( playbackParameters.device );

      if( playbackDeviceInfo == NULL )
         return false;

      // regardless of source formats, we always mix to float
      playbackParameters.sampleFormat = paFloat32;
      playbackParameters.hostApiSpecificStreamInfo = NULL;
      playbackParameters.channelCount = mNumPlaybackChannels;

      if (mSoftwarePlaythrough)
         playbackParameters.suggestedLatency =
            playbackDeviceInfo->defaultLowOutputLatency;
      else
         playbackParameters.suggestedLatency = latencyDuration/1000.0;

      mOutputMeter = mOwningProject->GetPlaybackMeter();
   }

   if( numCaptureChannels > 0)
   {
      useCapture = true;
      mCaptureFormat = captureFormat;

      const PaDeviceInfo *captureDeviceInfo;
      // retrieve the index of the device set in the prefs, or a sensible
      // default if it isn't set/valid
      captureParameters.device = getRecordDevIndex();

      captureDeviceInfo = Pa_GetDeviceInfo( captureParameters.device );

      if( captureDeviceInfo == NULL )
         return false;

      captureParameters.sampleFormat =
         AudacityToPortAudioSampleFormat(mCaptureFormat);

      captureParameters.hostApiSpecificStreamInfo = NULL;
      captureParameters.channelCount = mNumCaptureChannels;

      if (mSoftwarePlaythrough)
         captureParameters.suggestedLatency =
            captureDeviceInfo->defaultHighInputLatency;
      else
         captureParameters.suggestedLatency = latencyDuration/1000.0;

      SetCaptureMeter( mOwningProject, mOwningProject->GetCaptureMeter() );
   }

   SetMeters();

#ifdef USE_PORTMIXER
#ifdef __WXMSW__
   //mchinen nov 30 2010.  For some reason Pa_OpenStream resets the input volume on windows.
   //so cache and restore after it.
   //The actual problem is likely in portaudio's pa_win_wmme.c OpenStream().
   float oldRecordVolume = Px_GetInputVolume(mPortMixer);
#endif
#endif

   // July 2016 (Carsten and Uwe)
   // BUG 193: Possibly tell portAudio to use 24 bit with DirectSound. 
   int  userData = 24;
   int* lpUserData = (captureFormat_saved == int24Sample) ? &userData : NULL;

   mLastPaError = Pa_OpenStream( &mPortStreamV19,
                                 useCapture ? &captureParameters : NULL,
                                 usePlayback ? &playbackParameters : NULL,
                                 mRate, paFramesPerBufferUnspecified,
                                 paNoFlag,
                                 audacityAudioCallback, lpUserData );


#if USE_PORTMIXER
#ifdef __WXMSW__
   Px_SetInputVolume(mPortMixer, oldRecordVolume);
#endif
   if (mPortStreamV19 != NULL && mLastPaError == paNoError) {

      #ifdef __WXMAC__
      if (mPortMixer) {
         if (Px_SupportsPlaythrough(mPortMixer)) {
            bool playthrough = false;

            mPreviousHWPlaythrough = Px_GetPlaythrough(mPortMixer);

            // Bug 388.  Feature not supported.
            //gPrefs->Read(wxT("/AudioIO/Playthrough"), &playthrough, false);
            if (playthrough)
               Px_SetPlaythrough(mPortMixer, 1.0);
            else
               Px_SetPlaythrough(mPortMixer, 0.0);
         }
      }
      #endif
   }
#endif

   return (mLastPaError == paNoError);
}

void AudioIO::StartMonitoring(double sampleRate)
{
   if ( mPortStreamV19 || mStreamToken )
      return;

   bool success;
   long captureChannels;
   sampleFormat captureFormat = (sampleFormat)
      gPrefs->Read(wxT("/SamplingRate/DefaultProjectSampleFormat"), floatSample);
   gPrefs->Read(wxT("/AudioIO/RecordChannels"), &captureChannels, 2L);
   gPrefs->Read(wxT("/AudioIO/SWPlaythrough"), &mSoftwarePlaythrough, false);
   int playbackChannels = 0;

   if (mSoftwarePlaythrough)
      playbackChannels = 2;

   // FIXME: TRAP_ERR StartPortAudioStream (a PaError may be present)
   // but StartPortAudioStream function only returns true or false.
   mUsingAlsa = false;
   success = StartPortAudioStream(sampleRate, (unsigned int)playbackChannels,
                                  (unsigned int)captureChannels,
                                  captureFormat);
   // TODO: Check return value of success.
   (void)success;

   wxCommandEvent e(EVT_AUDIOIO_MONITOR);
   e.SetEventObject(mOwningProject);
   e.SetInt(true);
   wxTheApp->ProcessEvent(e);

   // FIXME: TRAP_ERR PaErrorCode 'noted' but not reported in StartMonitoring.
   // Now start the PortAudio stream!
   // TODO: ? Factor out and reuse error reporting code from end of 
   // AudioIO::StartStream?
   mLastPaError = Pa_StartStream( mPortStreamV19 );

   // Update UI display only now, after all possibilities for error are past.
   if ((mLastPaError == paNoError) && mListener) {
      // advertise the chosen I/O sample rate to the UI
      mListener->OnAudioIORate((int)mRate);
   }
}

int AudioIO::StartStream(const WaveTrackConstArray &playbackTracks,
                         const WaveTrackArray &captureTracks,
                         double t0, double t1,
                         const AudioIOStartStreamOptions &options)
{
   mLostSamples = 0;
   mLostCaptureIntervals.clear();
   mDetectDropouts =
      gPrefs->Read( WarningDialogKey(wxT("DropoutDetected")), true ) != 0;
   auto cleanup = finally ( [this] { ClearRecordingException(); } );

   if( IsBusy() )
      return 0;

   const auto &sampleRate = options.rate;

   // We just want to set mStreamToken to -1 - this way avoids
   // an extremely rare but possible race condition, if two functions
   // somehow called StartStream at the same time...
   mStreamToken--;
   if (mStreamToken != -1)
      return 0;

   // TODO: we don't really need to close and reopen stream if the
   // format matches; however it's kind of tricky to keep it open...
   //
   //   if (sampleRate == mRate &&
   //       playbackChannels == mNumPlaybackChannels &&
   //       captureChannels == mNumCaptureChannels &&
   //       captureFormat == mCaptureFormat) {

   if (mPortStreamV19) {
      StopStream();
      while(mPortStreamV19)
         wxMilliSleep( 50 );
   }

#ifdef __WXGTK__
   // Detect whether ALSA is the chosen host, and do the various involved MIDI
   // timing compensations only then.
   mUsingAlsa = (gPrefs->Read(wxT("/AudioIO/Host"), wxT("")) == "ALSA");
#endif

   gPrefs->Read(wxT("/AudioIO/SWPlaythrough"), &mSoftwarePlaythrough, false);
   gPrefs->Read(wxT("/AudioIO/SoundActivatedRecord"), &mPauseRec, false);
   int silenceLevelDB;
   gPrefs->Read(wxT("/AudioIO/SilenceLevel"), &silenceLevelDB, -50);
   int dBRange;
   dBRange = gPrefs->Read(ENV_DB_KEY, ENV_DB_RANGE);
   if(silenceLevelDB < -dBRange)
   {
      silenceLevelDB = -dBRange + 3;   // meter range was made smaller than SilenceLevel
      gPrefs->Write(ENV_DB_KEY, dBRange); // so set SilenceLevel reasonable
      gPrefs->Flush();
   }
   mSilenceLevel = (silenceLevelDB + dBRange)/(double)dBRange;  // meter goes -dBRange dB -> 0dB

   mListener = options.listener;
   mRate    = sampleRate;
   mT0      = t0;
   mT1      = t1;
   mTime    = t0;
   mSeek    = 0;
   mLastRecordingOffset = 0;
   mCaptureTracks = captureTracks;
   mPlaybackTracks = playbackTracks;

   bool commit = false;
   auto cleanupTracks = finally([&]{
      if (!commit) {
         // Don't keep unnecessary shared pointers to tracks
         mPlaybackTracks.clear();
         mCaptureTracks.clear();
      }
   });

   mPlayMode = options.playLooped ? PLAY_LOOPED : PLAY_STRAIGHT;
   mCutPreviewGapStart = options.cutPreviewGapStart;
   mCutPreviewGapLen = options.cutPreviewGapLen;

   mPlaybackBuffers.reset();
   mPlaybackMixers.reset();
   mCaptureBuffers.reset();
   mResample.reset();

   double playbackTime = 4.0;

   // mWarpedTime and mWarpedLength are irrelevant when scrubbing,
   // else they are used in updating mTime,
   // and when not scrubbing or playing looped, mTime is also used
   // in the test for termination of playback.

   // with ComputeWarpedLength, it is now possible the calculate the warped length with 100% accuracy
   // (ignoring accumulated rounding errors during playback) which fixes the 'missing sound at the end' bug
   mWarpedTime = 0.0;

	mWarpedLength = mT1 - mT0;
	// PRL allow backwards play
	mWarpedLength = fabs(mWarpedLength);

   //
   // The RingBuffer sizes, and the max amount of the buffer to
   // fill at a time, both grow linearly with the number of
   // tracks.  This allows us to scale up to many tracks without
   // killing performance.
   //

   // (warped) playback time to produce with each filling of the buffers
   // by the Audio thread (except at the end of playback):
   // usually, make fillings fewer and longer for less CPU usage.
   // But for useful scrubbing, we can't run too far ahead without checking
   // mouse input, so make fillings more and shorter.
   // What Audio thread produces for playback is then consumed by the PortAudio
   // thread, in many smaller pieces.
   wxASSERT( playbackTime >= 0 );
   mPlaybackSamplesToCopy = playbackTime * mRate;

   // Capacity of the playback buffer.
   mPlaybackRingBufferSecs = 10.0;

   mCaptureRingBufferSecs = 4.5 + 0.5 * std::min(size_t(16), mCaptureTracks.size());
   mMinCaptureSecsToCopy = 0.2 + 0.2 * std::min(size_t(16), mCaptureTracks.size());

   unsigned int playbackChannels = 0;
   unsigned int captureChannels = 0;
   sampleFormat captureFormat = floatSample;

   if (playbackTracks.size() > 0)
      playbackChannels = 2;

   if (mSoftwarePlaythrough)
      playbackChannels = 2;

   if( captureTracks.size() > 0 )
   {
      // For capture, every input channel gets its own track
      captureChannels = mCaptureTracks.size();
      // I don't deal with the possibility of the capture tracks
      // having different sample formats, since it will never happen
      // with the current code.  This code wouldn't *break* if this
      // assumption was false, but it would be sub-optimal.  For example,
      // if the first track was 16-bit and the second track was 24-bit,
      // we would set the sound card to capture in 16 bits and the second
      // track wouldn't get the benefit of all 24 bits the card is capable
      // of.
      captureFormat = mCaptureTracks[0]->GetSampleFormat();

      // Tell project that we are about to start recording
      if (mListener)
         mListener->OnAudioIOStartRecording();
   }

   bool successAudio;

   successAudio = StartPortAudioStream(sampleRate, playbackChannels,
                                       captureChannels, captureFormat);

   if (!successAudio) {
      if (mListener && captureChannels > 0)
         mListener->OnAudioIOStopRecording();
      mStreamToken = 0;

      // Don't cause a busy wait in the audio thread after stopping scrubbing
      mPlayMode = PLAY_STRAIGHT;

      return 0;
   }

   //
   // The (audio) stream has been opened successfully (assuming we tried
   // to open it). We now proceed to
   // allocate the memory structures the stream will need.
   //

   bool bDone;
   do
   {
      bDone = true; // assume success
      try
      {
         if( mNumPlaybackChannels > 0 ) {
            // Allocate output buffers.  For every output track we allocate
            // a ring buffer of five seconds
            auto playbackBufferSize =
               (size_t)lrint(mRate * mPlaybackRingBufferSecs);
            auto playbackMixBufferSize =
               mPlaybackSamplesToCopy;

            mPlaybackBuffers.reinit(mPlaybackTracks.size());
            mPlaybackMixers.reinit(mPlaybackTracks.size());

            for (unsigned int i = 0; i < mPlaybackTracks.size(); i++)
            {
               mPlaybackBuffers[i] = std::make_unique<RingBuffer>(floatSample, playbackBufferSize);

               // MB: use normal time for the end time, not warped time!
               WaveTrackConstArray tracks;
               tracks.push_back(mPlaybackTracks[i]);
               mPlaybackMixers[i] = std::make_unique<Mixer>
                  (tracks,
                  // Don't throw for read errors, just play silence:
                  false,
                  mT0, mT1, 1,
                  playbackMixBufferSize, false,
                  mRate, floatSample, false);
               mPlaybackMixers[i]->ApplyTrackGains(false);
            }
         }

         if( mNumCaptureChannels > 0 )
         {
            // Allocate input buffers.  For every input track we allocate
            // a ring buffer of five seconds
            auto captureBufferSize = (size_t)(mRate * mCaptureRingBufferSecs + 0.5);

            // In the extraordinarily rare case that we can't even afford 100 samples, just give up.
            if(captureBufferSize < 100)
            {
               StartStreamCleanup();
               AudacityMessageBox(_("Out of memory!"));
               return 0;
            }

            mCaptureBuffers.reinit(mCaptureTracks.size());
            mResample.reinit(mCaptureTracks.size());
            mFactor = sampleRate / mRate;

            for( unsigned int i = 0; i < mCaptureTracks.size(); i++ )
            {
               mCaptureBuffers[i] = std::make_unique<RingBuffer>
                  ( mCaptureTracks[i]->GetSampleFormat(),
                                                    captureBufferSize );
               mResample[i] = std::make_unique<Resample>(true, mFactor, mFactor); // constant rate resampling
            }
         }
      }
      catch(std::bad_alloc&)
      {
         // Oops!  Ran out of memory.  This is pretty rare, so we'll just
         // try deleting everything, halving our buffer size, and try again.
         StartStreamCleanup(true);
         mPlaybackRingBufferSecs *= 0.5;
         mPlaybackSamplesToCopy /= 2;
         mCaptureRingBufferSecs *= 0.5;
         mMinCaptureSecsToCopy *= 0.5;
         bDone = false;

         // In the extraordinarily rare case that we can't even afford 100 samples, just give up.
         auto playbackBufferSize = (size_t)lrint(mRate * mPlaybackRingBufferSecs);
         auto playbackMixBufferSize = mPlaybackSamplesToCopy;
         if(playbackBufferSize < 100 || playbackMixBufferSize < 100)
         {
            StartStreamCleanup();
            AudacityMessageBox(_("Out of memory!"));
            return 0;
         }
      }
   } while(!bDone);

   if (mNumPlaybackChannels > 0)
   {
      EffectManager & em = EffectManager::Get();
      // Setup for realtime playback at the rate of the realtime
      // stream, not the rate of the track.
      em.RealtimeInitialize(mRate);

      // The following adds a NEW effect processor for each logical track and the
      // group determination should mimic what is done in audacityAudioCallback()
      // when calling RealtimeProcess().
      int group = 0;
      for (size_t i = 0, cnt = mPlaybackTracks.size(); i < cnt; i++)
      {
         const WaveTrack *vt = gAudioIO->mPlaybackTracks[i].get();

         unsigned chanCnt = 1;
         if (vt->GetLinked())
         {
            i++;
            chanCnt++;
         }

         // Setup for realtime playback at the rate of the realtime
         // stream, not the rate of the track.
         em.RealtimeAddProcessor(group++, chanCnt, mRate);
      }
   }

   if (options.pStartTime)
   {
      // Calculate the NEW time position
      mTime = std::max(mT0, std::min(mT1, *options.pStartTime));
      // Reset mixer positions for all playback tracks
      unsigned numMixers = mPlaybackTracks.size();
      for (unsigned ii = 0; ii < numMixers; ++ii)
         mPlaybackMixers[ii]->Reposition(mTime);
      mWarpedTime = mTime - mT0;
   }

   // We signal the audio thread to call FillBuffers, to prime the RingBuffers
   // so that they will have data in them when the stream starts.  Having the
   // audio thread call FillBuffers here makes the code more predictable, since
   // FillBuffers will ALWAYS get called from the Audio thread.
   mAudioThreadShouldCallFillBuffersOnce = true;

   while( mAudioThreadShouldCallFillBuffersOnce == true ) {
      wxMilliSleep( 50 );
   }

   if(mNumPlaybackChannels > 0 || mNumCaptureChannels > 0) {

#ifdef REALTIME_ALSA_THREAD
      // PRL: Do this in hope of less thread scheduling jitter in calls to
      // audacityAudioCallback.
      // Not needed to make audio playback work smoothly.
      // But needed in case we also play MIDI, so that the variable "offset"
      // in AudioIO::MidiTime() is a better approximation of the duration
      // between the call of audacityAudioCallback and the actual output of
      // the first audio sample.
      // (Which we should be able to determine from fields of
      // PaStreamCallbackTimeInfo, but that seems not to work as documented with
      // ALSA.)
      if (mUsingAlsa)
         // Perhaps we should do this only if also playing MIDI ?
         PaAlsa_EnableRealtimeScheduling( mPortStreamV19, 1 );
#endif

      //
      // Generate a unique value each time, to be returned to
      // clients accessing the AudioIO API, so they can query if they
      // are the ones who have reserved AudioIO or not.
      //
      // It is important to set this before setting the portaudio stream in
      // motion -- otherwise it may play an unspecified number of leading
      // zeroes.
      mStreamToken = (++mNextStreamToken);

      // This affects the AudioThread (not the portaudio callback).
      // Probably not needed so urgently before portaudio thread start for usual
      // playback, since our ring buffers have been primed already with 4 sec
      // of audio, but then we might be scrubbing, so do it.
      mAudioThreadFillBuffersLoopRunning = true;

      // Now start the PortAudio stream!
      PaError err;
      err = Pa_StartStream( mPortStreamV19 );

      if( err != paNoError )
      {
         mStreamToken = 0;
         mAudioThreadFillBuffersLoopRunning = false;
         if (mListener && mNumCaptureChannels > 0)
            mListener->OnAudioIOStopRecording();
         StartStreamCleanup();
         AudacityMessageBox(LAT1CTOWX(Pa_GetErrorText(err)));
         return 0;
      }
   }

   // Update UI display only now, after all possibilities for error are past.
   if (mListener) {
      // advertise the chosen I/O sample rate to the UI
      mListener->OnAudioIORate((int)mRate);
   }

   if (mNumPlaybackChannels > 0)
   {
      wxCommandEvent e(EVT_AUDIOIO_PLAYBACK);
      e.SetEventObject(mOwningProject);
      e.SetInt(true);
      wxTheApp->ProcessEvent(e);
   }

   if (mNumCaptureChannels > 0)
   {
      wxCommandEvent e(EVT_AUDIOIO_CAPTURE);
      e.SetEventObject(mOwningProject);
      e.SetInt(true);
      wxTheApp->ProcessEvent(e);
   }

   // Enable warning popups for unfound aliased blockfiles.
   wxGetApp().SetMissingAliasedFileWarningShouldShow(true);

   commit = true;
   return mStreamToken;
}

void AudioIO::StartStreamCleanup(bool bOnlyBuffers)
{
   if (mNumPlaybackChannels > 0)
   {
      EffectManager::Get().RealtimeFinalize();
   }

   mPlaybackBuffers.reset();
   mPlaybackMixers.reset();
   mCaptureBuffers.reset();
   mResample.reset();

   if(!bOnlyBuffers)
   {
      Pa_AbortStream( mPortStreamV19 );
      Pa_CloseStream( mPortStreamV19 );
      mPortStreamV19 = NULL;
      mStreamToken = 0;
   }

   // Don't cause a busy wait in the audio thread after stopping scrubbing
   mPlayMode = PLAY_STRAIGHT;
}

bool AudioIO::IsAvailable(AudacityProject *project)
{
   return mOwningProject == NULL || mOwningProject == project;
}

void AudioIO::SetCaptureMeter(AudacityProject *project, MeterPanel *meter)
{
   if (!mOwningProject || mOwningProject == project)
   {
      if (meter)
      {
         mInputMeter = meter;
         mInputMeter->Reset(mRate, true);
      }
      else
         mInputMeter.Release();
   }
}

void AudioIO::SetPlaybackMeter(AudacityProject *project, MeterPanel *meter)
{
   if (!mOwningProject || mOwningProject == project)
   {
      mOutputMeter = meter;
      if (mOutputMeter)
      {
         mOutputMeter->Reset(mRate, true);
      }
   }
}

void AudioIO::SetMeters()
{
   if (mInputMeter)
      mInputMeter->Reset(mRate, true);
   if (mOutputMeter)
      mOutputMeter->Reset(mRate, true);

   AudacityProject* pProj = GetActiveProject();
   MixerBoard* pMixerBoard = pProj->GetMixerBoard();
   if (pMixerBoard)
      pMixerBoard->ResetMeters(true);

   mUpdateMeters = true;
}

void AudioIO::StopStream()
{
   auto cleanup = finally ( [this] { ClearRecordingException(); } );

   if( mPortStreamV19 == NULL)
      return;

   if( Pa_IsStreamStopped( mPortStreamV19 ))
      return;

   wxMutexLocker locker(mSuspendAudioThread);

   // No longer need effects processing
   if (mNumPlaybackChannels > 0)
   {
      EffectManager::Get().RealtimeFinalize();
   }

   //
   // We got here in one of two ways:
   //
   // 1. The user clicked the stop button and we therefore want to stop
   //    as quickly as possible.  So we use AbortStream().  If this is
   //    the case the portaudio stream is still in the Running state
   //    (see PortAudio state machine docs).
   //
   // 2. The callback told PortAudio to stop the stream since it had
   //    reached the end of the selection.  The UI thread discovered
   //    this by noticing that AudioIO::IsActive() returned false.
   //    IsActive() (which calls Pa_GetStreamActive()) will not return
   //    false until all buffers have finished playing, so we can call
   //    AbortStream without losing any samples.  If this is the case
   //    we are in the "callback finished state" (see PortAudio state
   //    machine docs).
   //
   // The moral of the story: We can call AbortStream safely, without
   // losing samples.
   //
   // DMM: This doesn't seem to be true; it seems to be necessary to
   // call StopStream if the callback brought us here, and AbortStream
   // if the user brought us here.
   //

   mAudioThreadFillBuffersLoopRunning = false;

   // Audacity can deadlock if it tries to update meters while
   // we're stopping PortAudio (because the meter updating code
   // tries to grab a UI mutex while PortAudio tries to join a
   // pthread).  So we tell the callback to stop updating meters,
   // and wait until the callback has left this part of the code
   // if it was already there.
   mUpdateMeters = false;
   while(mUpdatingMeters) {
      ::wxSafeYield();
      wxMilliSleep( 50 );
   }

   // Turn off HW playthrough if PortMixer is being used

  #if defined(USE_PORTMIXER)
   if( mPortMixer ) {
      #if __WXMAC__
      if (Px_SupportsPlaythrough(mPortMixer) && mPreviousHWPlaythrough >= 0.0)
         Px_SetPlaythrough(mPortMixer, mPreviousHWPlaythrough);
         mPreviousHWPlaythrough = -1.0;
      #endif
   }
  #endif

   if (mPortStreamV19) {
      Pa_AbortStream( mPortStreamV19 );
      Pa_CloseStream( mPortStreamV19 );
      mPortStreamV19 = NULL;
   }

   if (mNumPlaybackChannels > 0)
   {
      wxCommandEvent e(EVT_AUDIOIO_PLAYBACK);
      e.SetEventObject(mOwningProject);
      e.SetInt(false);
      wxTheApp->ProcessEvent(e);
   }

   if (mNumCaptureChannels > 0)
   {
      wxCommandEvent e(mStreamToken == 0 ? EVT_AUDIOIO_MONITOR : EVT_AUDIOIO_CAPTURE);
      e.SetEventObject(mOwningProject);
      e.SetInt(false);
      wxTheApp->ProcessEvent(e);
   }

   // If there's no token, we were just monitoring, so we can
   // skip this next part...
   if (mStreamToken > 0) {
      // In either of the above cases, we want to make sure that any
      // capture data that made it into the PortAudio callback makes it
      // to the target WaveTrack.  To do this, we ask the audio thread to
      // call FillBuffers one last time (it normally would not do so since
      // Pa_GetStreamActive() would now return false
      mAudioThreadShouldCallFillBuffersOnce = true;

      while( mAudioThreadShouldCallFillBuffersOnce == true )
      {
         // LLL:  Experienced recursive yield here...once.
         wxGetApp().Yield(true); // Pass true for onlyIfNeeded to avoid recursive call error.
         wxMilliSleep( 50 );
      }

      //
      // Everything is taken care of.  Now, just free all the resources
      // we allocated in StartStream()
      //

      if (mPlaybackTracks.size() > 0)
      {
         mPlaybackBuffers.reset();
         mPlaybackMixers.reset();
      }

      //
      // Offset all recorded tracks to account for latency
      //
      if (mCaptureTracks.size() > 0)
      {
         mCaptureBuffers.reset();
         mResample.reset();

         //
         // We only apply latency correction when we actually played back
         // tracks during the recording. If we did not play back tracks,
         // there's nothing we could be out of sync with. This also covers the
         // case that we do not apply latency correction when recording the
         // first track in a project.
         //
         double latencyCorrection = DEFAULT_LATENCY_CORRECTION;
         gPrefs->Read(wxT("/AudioIO/LatencyCorrection"), &latencyCorrection);

         double recordingOffset =
            mLastRecordingOffset + latencyCorrection / 1000.0;

         for (unsigned int i = 0; i < mCaptureTracks.size(); i++) {
            // The calls to Flush, and (less likely) Clear and InsertSilence,
            // may cause exceptions because of exhaustion of disk space.
            // Stop those exceptions here, or else they propagate through too
            // many parts of Audacity that are not effects or editing
            // operations.  GuardedCall ensures that the user sees a warning.

            // Also be sure to Flush each track, at the top of the guarded call,
            // relying on the guarantee that the track will be left in a flushed
            // state, though the append buffer may be lost.

            // If the other track operations fail their strong guarantees, then
            // the shift for latency correction may be skipped.
            GuardedCall( [&] {
               WaveTrack* track = mCaptureTracks[i].get();

               // use NOFAIL-GUARANTEE that track is flushed,
               // PARTIAL-GUARANTEE that some initial length of the recording
               // is saved.
               // See comments in FillBuffers().
               track->Flush();

               if (mPlaybackTracks.size() > 0)
               {  // only do latency correction if some tracks are being played back
                  WaveTrackArray playbackTracks;
                  AudacityProject *p = GetActiveProject();
                  // we need to get this as mPlaybackTracks does not contain tracks being recorded into
                  playbackTracks = p->GetTracks()->GetWaveTrackArray(false);
                  bool appendRecord = false;
                  for (unsigned int j = 0; j < playbackTracks.size(); j++)
                  {  // find if we are recording into an existing track (append-record)
                     WaveTrack* trackP = playbackTracks[j].get();
                     if( track == trackP )
                     {
                        if( track->GetStartTime() != mT0 )  // in a NEW track if these are equal
                        {
                           appendRecord = true;
                           break;
                        }
                     }
                  }
                  if( appendRecord )
                  {  // append-recording
                     if (recordingOffset < 0)
                        // use STRONG-GUARANTEE
                        track->Clear(mT0, mT0 - recordingOffset); // cut the latency out
                     else
                        // use STRONG-GUARANTEE
                        track->InsertSilence(mT0, recordingOffset); // put silence in
                  }
                  else
                  {  // recording into a NEW track
                     // gives NOFAIL-GUARANTEE though we only need STRONG
                     track->SetOffset(track->GetStartTime() + recordingOffset);
                     if(track->GetEndTime() < 0.)
                     {
                        // Bug 96: Only warn for the first track.
                        if( i==0 )
                        {
                           AudacityMessageDialog m(NULL, _(
"Latency Correction setting has caused the recorded audio to be hidden before zero.\nAudacity has brought it back to start at zero.\nYou may have to use the Time Shift Tool (<---> or F5) to drag the track to the right place."),
                           _("Latency problem"), wxOK);
                           m.ShowModal();
                        }
                        // gives NOFAIL-GUARANTEE though we only need STRONG
                        track->SetOffset(0.);
                     }
                  }
               }
            } );
         }

         for (auto &interval : mLostCaptureIntervals) {
            auto &start = interval.first;
            if (mPlaybackTracks.size() > 0)
               // only do latency correction if some tracks are being played back
               start += recordingOffset;
            auto duration = interval.second;
            for (auto &track : mCaptureTracks) {
               GuardedCall([&] {
                  track->SyncLockAdjust(start, start + duration);
               });
            }
         }

         AudacityProject *p = GetActiveProject();
         ControlToolBar *bar = p->GetControlToolBar();
         bar->CommitRecording();
      }
   }

   if (mInputMeter)
      mInputMeter->Reset(mRate, false);

   if (mOutputMeter)
      mOutputMeter->Reset(mRate, false);

   MixerBoard* pMixerBoard = mOwningProject->GetMixerBoard();
   if (pMixerBoard)
      pMixerBoard->ResetMeters(false);

   mInputMeter.Release();
   mOutputMeter = NULL;
   mOwningProject = NULL;

   if (mListener && mNumCaptureChannels > 0)
      mListener->OnAudioIOStopRecording();

   //
   // Only set token to 0 after we're totally finished with everything
   //
   mStreamToken = 0;

   mNumCaptureChannels = 0;
   mNumPlaybackChannels = 0;

   mPlaybackTracks.clear();
   mCaptureTracks.clear();

   if (mListener) {
      // Tell UI to hide sample rate
      mListener->OnAudioIORate(0);
   }

   // Don't cause a busy wait in the audio thread after stopping scrubbing
   mPlayMode = PLAY_STRAIGHT;
}

void AudioIO::SetPaused(bool state)
{
   if (state != mPaused)
   {
      if (state)
      {
         EffectManager::Get().RealtimeSuspend();
      }
      else
      {
         EffectManager::Get().RealtimeResume();
      }
   }

   mPaused = state;
}

bool AudioIO::IsPaused()
{
   return mPaused;
}

bool AudioIO::IsBusy()
{
   if (mStreamToken != 0)
      return true;

   return false;
}

bool AudioIO::IsStreamActive()
{
   bool isActive = false;
   // JKC: Not reporting any Pa error, but that looks OK.
   if( mPortStreamV19 )
      isActive = (Pa_IsStreamActive( mPortStreamV19 ) > 0);
   return isActive;
}

bool AudioIO::IsStreamActive(int token)
{
   return (this->IsStreamActive() && this->IsAudioTokenActive(token));
}

bool AudioIO::IsAudioTokenActive(int token)
{
   return ( token > 0 && token == mStreamToken );
}

bool AudioIO::IsMonitoring()
{
   return ( mPortStreamV19 && mStreamToken==0 );
}

double AudioIO::LimitStreamTime(double absoluteTime) const
{
   // Allows for forward or backward play
   if (ReversedTime())
      return std::max(mT1, std::min(mT0, absoluteTime));
   else
      return std::max(mT0, std::min(mT1, absoluteTime));
}

double AudioIO::NormalizeStreamTime(double absoluteTime) const
{
   // dmazzoni: This function is needed for two reasons:
   // One is for looped-play mode - this function makes sure that the
   // position indicator keeps wrapping around.  The other reason is
   // more subtle - it's because PortAudio can query the hardware for
   // the current stream time, and this query is not always accurate.
   // Sometimes it's a little behind or ahead, and so this function
   // makes sure that at least we clip it to the selection.
   //
   // msmeyer: There is also the possibility that we are using "cut preview"
   //          mode. In this case, we should jump over a defined "gap" in the
   //          audio.
   absoluteTime = LimitStreamTime(absoluteTime);

   if (mCutPreviewGapLen > 0)
   {
      // msmeyer: We're in cut preview mode, so if we are on the right
      // side of the gap, we jump over it.
      if (absoluteTime > mCutPreviewGapStart)
         absoluteTime += mCutPreviewGapLen;
   }

   return absoluteTime;
}

double AudioIO::GetStreamTime()
{
   if( !IsStreamActive() )
      return BAD_STREAM_TIME;

   return NormalizeStreamTime(mTime);
}


std::vector<long> AudioIO::GetSupportedPlaybackRates(int devIndex, double rate)
{
   if (devIndex == -1)
   {  // weren't given a device index, get the prefs / default one
      devIndex = getPlayDevIndex();
   }

   // Check if we can use the cached rates
   if (mCachedPlaybackIndex != -1 && devIndex == mCachedPlaybackIndex
         && (rate == 0.0 || make_iterator_range(mCachedPlaybackRates).contains(rate)))
   {
      return mCachedPlaybackRates;
   }

   std::vector<long> supported;
   int irate = (int)rate;
   const PaDeviceInfo* devInfo = NULL;
   int i;

   devInfo = Pa_GetDeviceInfo(devIndex);

   if (!devInfo)
   {
      wxLogDebug(wxT("GetSupportedPlaybackRates() Could not get device info!"));
      return supported;
   }

   // LLL: Remove when a proper method of determining actual supported
   //      DirectSound rate is devised.
   const PaHostApiInfo* hostInfo = Pa_GetHostApiInfo(devInfo->hostApi);
   bool isDirectSound = (hostInfo && hostInfo->type == paDirectSound);

   PaStreamParameters pars;

   pars.device = devIndex;
   pars.channelCount = 1;
   pars.sampleFormat = paFloat32;
   pars.suggestedLatency = devInfo->defaultHighOutputLatency;
   pars.hostApiSpecificStreamInfo = NULL;

   // JKC: PortAudio Errors handled OK here.  No need to report them
   for (i = 0; i < NumRatesToTry; i++)
   {
      // LLL: Remove when a proper method of determining actual supported
      //      DirectSound rate is devised.
      if (!(isDirectSound && RatesToTry[i] > 200000))
      if (Pa_IsFormatSupported(NULL, &pars, RatesToTry[i]) == 0)
         supported.push_back(RatesToTry[i]);
   }

   if (irate != 0 && !make_iterator_range(supported).contains(irate))
   {
      // LLL: Remove when a proper method of determining actual supported
      //      DirectSound rate is devised.
      if (!(isDirectSound && RatesToTry[i] > 200000))
      if (Pa_IsFormatSupported(NULL, &pars, irate) == 0)
         supported.push_back(irate);
   }

   return supported;
}

std::vector<long> AudioIO::GetSupportedCaptureRates(int devIndex, double rate)
{
   if (devIndex == -1)
   {  // not given a device, look up in prefs / default
      devIndex = getRecordDevIndex();
   }

   // Check if we can use the cached rates
   if (mCachedCaptureIndex != -1 && devIndex == mCachedCaptureIndex
         && (rate == 0.0 || make_iterator_range(mCachedCaptureRates).contains(rate)))
   {
      return mCachedCaptureRates;
   }

   std::vector<long> supported;
   int irate = (int)rate;
   const PaDeviceInfo* devInfo = NULL;
   int i;

   devInfo = Pa_GetDeviceInfo(devIndex);

   if (!devInfo)
   {
      wxLogDebug(wxT("GetSupportedCaptureRates() Could not get device info!"));
      return supported;
   }

   double latencyDuration = DEFAULT_LATENCY_DURATION;
   long recordChannels = 1;
   gPrefs->Read(wxT("/AudioIO/LatencyDuration"), &latencyDuration);
   gPrefs->Read(wxT("/AudioIO/RecordChannels"), &recordChannels);

   // LLL: Remove when a proper method of determining actual supported
   //      DirectSound rate is devised.
   const PaHostApiInfo* hostInfo = Pa_GetHostApiInfo(devInfo->hostApi);
   bool isDirectSound = (hostInfo && hostInfo->type == paDirectSound);

   PaStreamParameters pars;

   pars.device = devIndex;
   pars.channelCount = recordChannels;
   pars.sampleFormat = paFloat32;
   pars.suggestedLatency = latencyDuration / 1000.0;
   pars.hostApiSpecificStreamInfo = NULL;

   for (i = 0; i < NumRatesToTry; i++)
   {
      // LLL: Remove when a proper method of determining actual supported
      //      DirectSound rate is devised.
      if (!(isDirectSound && RatesToTry[i] > 200000))
      if (Pa_IsFormatSupported(&pars, NULL, RatesToTry[i]) == 0)
         supported.push_back(RatesToTry[i]);
   }

   if (irate != 0 && !make_iterator_range(supported).contains(irate))
   {
      // LLL: Remove when a proper method of determining actual supported
      //      DirectSound rate is devised.
      if (!(isDirectSound && RatesToTry[i] > 200000))
      if (Pa_IsFormatSupported(&pars, NULL, irate) == 0)
         supported.push_back(irate);
   }

   return supported;
}

std::vector<long> AudioIO::GetSupportedSampleRates(int playDevice, int recDevice, double rate)
{
   // Not given device indices, look up prefs
   if (playDevice == -1) {
      playDevice = getPlayDevIndex();
   }
   if (recDevice == -1) {
      recDevice = getRecordDevIndex();
   }

   // Check if we can use the cached rates
   if (mCachedPlaybackIndex != -1 && mCachedCaptureIndex != -1 &&
         playDevice == mCachedPlaybackIndex &&
         recDevice == mCachedCaptureIndex &&
         (rate == 0.0 || make_iterator_range(mCachedSampleRates).contains(rate)))
   {
      return mCachedSampleRates;
   }

   auto playback = GetSupportedPlaybackRates(playDevice, rate);
   auto capture = GetSupportedCaptureRates(recDevice, rate);
   int i;

   // Return only sample rates which are in both arrays
   std::vector<long> result;

   for (i = 0; i < (int)playback.size(); i++)
      if (make_iterator_range(capture).contains(playback[i]))
         result.push_back(playback[i]);

   // If this yields no results, use the default sample rates nevertheless
/*   if (result.IsEmpty())
   {
      for (i = 0; i < NumStandardRates; i++)
         result.Add(StandardRates[i]);
   }*/

   return result;
}

/** \todo: should this take into account PortAudio's value for
 * PaDeviceInfo::defaultSampleRate? In principal this should let us work out
 * which rates are "real" and which resampled in the drivers, and so prefer
 * the real rates. */
int AudioIO::GetOptimalSupportedSampleRate()
{
   auto rates = GetSupportedSampleRates();

   if (make_iterator_range(rates).contains(44100))
      return 44100;

   if (make_iterator_range(rates).contains(48000))
      return 48000;

   // if there are no supported rates, the next bit crashes. So check first,
   // and give them a "sensible" value if there are no valid values. They
   // will still get an error later, but with any luck may have changed
   // something by then. It's no worse than having an invalid default rate
   // stored in the preferences, which we don't check for
   if (rates.empty()) return 44100;

   return rates.back();
}

double AudioIO::GetBestRate(bool capturing, bool playing, double sampleRate)
{
   // Check if we can use the cached value
   if (mCachedBestRateIn != 0.0 && mCachedBestRateIn == sampleRate) {
      return mCachedBestRateOut;
   }

   // In order to cache the value, all early returns should instead set retval
   // and jump to finished
   double retval;

   std::vector<long> rates;
   if (capturing) wxLogDebug(wxT("AudioIO::GetBestRate() for capture"));
   if (playing) wxLogDebug(wxT("AudioIO::GetBestRate() for playback"));
   wxLogDebug(wxT("GetBestRate() suggested rate %.0lf Hz"), sampleRate);

   if (capturing && !playing) {
      rates = GetSupportedCaptureRates(-1, sampleRate);
   }
   else if (playing && !capturing) {
      rates = GetSupportedPlaybackRates(-1, sampleRate);
   }
   else {   // we assume capturing and playing - the alternative would be a
            // bit odd
      rates = GetSupportedSampleRates(-1, -1, sampleRate);
   }
   /* rem rates is the array of hardware-supported sample rates (in the current
    * configuration), sampleRate is the Project Rate (desired sample rate) */
   long rate = (long)sampleRate;

   if (make_iterator_range(rates).contains(rate)) {
      wxLogDebug(wxT("GetBestRate() Returning %.0ld Hz"), rate);
      retval = rate;
      goto finished;
      /* the easy case - the suggested rate (project rate) is in the list, and
       * we can just accept that and send back to the caller. This should be
       * the case for most users most of the time (all of the time on
       * Win MME as the OS does resampling) */
   }

   /* if we get here, there is a problem - the project rate isn't supported
    * on our hardware, so we can't us it. Need to come up with an alternative
    * rate to use. The process goes like this:
    * * If there are no rates to pick from, we're stuck and return 0 (error)
    * * If there are some rates, we pick the next one higher than the requested
    *   rate to use.
    * * If there aren't any higher, we use the highest available rate */

   if (rates.empty()) {
      /* we're stuck - there are no supported rates with this hardware. Error */
      wxLogDebug(wxT("GetBestRate() Error - no supported sample rates"));
      retval = 0.0;
      goto finished;
   }
   int i;
   for (i = 0; i < (int)rates.size(); i++)  // for each supported rate
         {
         if (rates[i] > rate) {
            // supported rate is greater than requested rate
            wxLogDebug(wxT("GetBestRate() Returning next higher rate - %.0ld Hz"), rates[i]);
            retval = rates[i];
            goto finished;
         }
         }

   wxLogDebug(wxT("GetBestRate() Returning highest rate - %.0ld Hz"), rates.back());
   retval = rates.back(); // the highest available rate
   goto finished;

finished:
   mCachedBestRateIn = sampleRate;
   mCachedBestRateOut = retval;
   return retval;
}


//////////////////////////////////////////////////////////////////////
//
//     Audio Thread Context
//
//////////////////////////////////////////////////////////////////////

AudioThread::ExitCode AudioThread::Entry()
{
   while( !TestDestroy() )
   {
      // Set LoopActive outside the tests to avoid race condition
      gAudioIO->mAudioThreadFillBuffersLoopActive = true;
      if( gAudioIO->mAudioThreadShouldCallFillBuffersOnce )
      {
         gAudioIO->FillBuffers();
         gAudioIO->mAudioThreadShouldCallFillBuffersOnce = false;
      }
      else if( gAudioIO->mAudioThreadFillBuffersLoopRunning )
      {
         gAudioIO->FillBuffers();
      }
      gAudioIO->mAudioThreadFillBuffersLoopActive = false;

      Sleep(10);
   }

   return 0;
}

size_t AudioIO::GetCommonlyAvailPlayback()
{
   auto commonlyAvail = mPlaybackBuffers[0]->AvailForPut();
   for (unsigned i = 1; i < mPlaybackTracks.size(); ++i)
      commonlyAvail = std::min(commonlyAvail,
         mPlaybackBuffers[i]->AvailForPut());
   return commonlyAvail;
}

size_t AudioIO::GetCommonlyAvailCapture()
{
   auto commonlyAvail = mCaptureBuffers[0]->AvailForGet();
   for (unsigned i = 1; i < mCaptureTracks.size(); ++i)
      commonlyAvail = std::min(commonlyAvail,
         mCaptureBuffers[i]->AvailForGet());
   return commonlyAvail;
}

#if USE_PORTMIXER
int AudioIO::getRecordSourceIndex(PxMixer *portMixer)
{
   int i;
   wxString sourceName = gPrefs->Read(wxT("/AudioIO/RecordingSource"), wxT(""));
   int numSources = Px_GetNumInputSources(portMixer);
   for (i = 0; i < numSources; i++) {
      if (sourceName == wxString(wxSafeConvertMB2WX(Px_GetInputSourceName(portMixer, i))))
         return i;
   }
   return -1;
}
#endif

int AudioIO::getPlayDevIndex(const wxString &devNameArg)
{
   wxString devName(devNameArg);
   // if we don't get given a device, look up the preferences
   if (devName.IsEmpty())
   {
      devName = gPrefs->Read(wxT("/AudioIO/PlaybackDevice"), wxT(""));
   }

   wxString hostName = gPrefs->Read(wxT("/AudioIO/Host"), wxT(""));
   PaHostApiIndex hostCnt = Pa_GetHostApiCount();
   PaHostApiIndex hostNum;
   for (hostNum = 0; hostNum < hostCnt; hostNum++)
   {
      const PaHostApiInfo *hinfo = Pa_GetHostApiInfo(hostNum);
      if (hinfo && wxString(wxSafeConvertMB2WX(hinfo->name)) == hostName)
      {
         for (PaDeviceIndex hostDevice = 0; hostDevice < hinfo->deviceCount; hostDevice++)
         {
            PaDeviceIndex deviceNum = Pa_HostApiDeviceIndexToDeviceIndex(hostNum, hostDevice);

            const PaDeviceInfo *dinfo = Pa_GetDeviceInfo(deviceNum);
            if (dinfo && DeviceName(dinfo) == devName && dinfo->maxOutputChannels > 0 )
            {
               // this device name matches the stored one, and works.
               // So we say this is the answer and return it
               return deviceNum;
            }
         }

         // The device wasn't found so use the default for this host.
         // LL:  At this point, preferences and active no longer match.
         return hinfo->defaultOutputDevice;
      }
   }

   // The host wasn't found, so use the default output device.
   // FIXME: TRAP_ERR PaErrorCode not handled well (this code is similar to input code
   // and the input side has more comments.)

   PaDeviceIndex deviceNum = Pa_GetDefaultOutputDevice();

   // Sometimes PortAudio returns -1 if it cannot find a suitable default
   // device, so we just use the first one available
   //
   // LL:  At this point, preferences and active no longer match
   //
   //      And I can't imagine how far we'll get specifying an "invalid" index later
   //      on...are we certain "0" even exists?
   if (deviceNum < 0) {
      wxASSERT(false);
      deviceNum = 0;
   }

   return deviceNum;
}

int AudioIO::getRecordDevIndex(const wxString &devNameArg)
{
   wxString devName(devNameArg);
   // if we don't get given a device, look up the preferences
   if (devName.IsEmpty())
   {
      devName = gPrefs->Read(wxT("/AudioIO/RecordingDevice"), wxT(""));
   }

   wxString hostName = gPrefs->Read(wxT("/AudioIO/Host"), wxT(""));
   PaHostApiIndex hostCnt = Pa_GetHostApiCount();
   PaHostApiIndex hostNum;
   for (hostNum = 0; hostNum < hostCnt; hostNum++)
   {
      const PaHostApiInfo *hinfo = Pa_GetHostApiInfo(hostNum);
      if (hinfo && wxString(wxSafeConvertMB2WX(hinfo->name)) == hostName)
      {
         for (PaDeviceIndex hostDevice = 0; hostDevice < hinfo->deviceCount; hostDevice++)
         {
            PaDeviceIndex deviceNum = Pa_HostApiDeviceIndexToDeviceIndex(hostNum, hostDevice);

            const PaDeviceInfo *dinfo = Pa_GetDeviceInfo(deviceNum);
            if (dinfo && DeviceName(dinfo) == devName && dinfo->maxInputChannels > 0 )
            {
               // this device name matches the stored one, and works.
               // So we say this is the answer and return it
               return deviceNum;
            }
         }

         // The device wasn't found so use the default for this host.
         // LL:  At this point, preferences and active no longer match.
         return hinfo->defaultInputDevice;
      }
   }

   // The host wasn't found, so use the default input device.
   // FIXME: TRAP_ERR PaErrorCode not handled well in getRecordDevIndex()
   PaDeviceIndex deviceNum = Pa_GetDefaultInputDevice();

   // Sometimes PortAudio returns -1 if it cannot find a suitable default
   // device, so we just use the first one available
   // PortAudio has an error reporting function.  We should log/report the error?
   //
   // LL:  At this point, preferences and active no longer match
   //
   //      And I can't imagine how far we'll get specifying an "invalid" index later
   //      on...are we certain "0" even exists?
   if (deviceNum < 0) {
      // JKC: This ASSERT will happen if you run with no config file
      // This happens once.  Config file will exist on the next run.
      // TODO: Look into this a bit more.  Could be relevant to blank Device Toolbar.
      wxASSERT(false);
      deviceNum = 0;
   }

   return deviceNum;
}

wxString AudioIO::GetDeviceInfo()
{
   wxStringOutputStream o;
   wxTextOutputStream s(o, wxEOL_UNIX);
   wxString e(wxT("\n"));

   if (IsStreamActive()) {
      return wxT("Stream is active ... unable to gather information.");
   }


   // FIXME: TRAP_ERR PaErrorCode not handled.  3 instances in GetDeviceInfo().
   int recDeviceNum = Pa_GetDefaultInputDevice();
   int playDeviceNum = Pa_GetDefaultOutputDevice();
   int cnt = Pa_GetDeviceCount();

   wxLogDebug(wxT("Portaudio reports %d audio devices"),cnt);

   s << wxT("==============================") << e;
   s << wxT("Default recording device number: ") << recDeviceNum << e;
   s << wxT("Default playback device number: ") << playDeviceNum << e;

   wxString recDevice = gPrefs->Read(wxT("/AudioIO/RecordingDevice"), wxT(""));
   wxString playDevice = gPrefs->Read(wxT("/AudioIO/PlaybackDevice"), wxT(""));
   int j;

   // This gets info on all available audio devices (input and output)
   if (cnt <= 0) {
      s << wxT("No devices found\n");
      return o.GetString();
   }

   const PaDeviceInfo* info;

   for (j = 0; j < cnt; j++) {
      s << wxT("==============================") << e;

      info = Pa_GetDeviceInfo(j);
      if (!info) {
         s << wxT("Device info unavailable for: ") << j << wxT("\n");
         continue;
      }

      wxString name = DeviceName(info);
      s << wxT("Device ID: ") << j << e;
      s << wxT("Device name: ") << name << e;
      s << wxT("Host name: ") << HostName(info) << e;
      s << wxT("Recording channels: ") << info->maxInputChannels << e;
      s << wxT("Playback channels: ") << info->maxOutputChannels << e;
      s << wxT("Low Recording Latency: ") << info->defaultLowInputLatency << e;
      s << wxT("Low Playback Latency: ") << info->defaultLowOutputLatency << e;
      s << wxT("High Recording Latency: ") << info->defaultHighInputLatency << e;
      s << wxT("High Playback Latency: ") << info->defaultHighOutputLatency << e;

      auto rates = GetSupportedPlaybackRates(j, 0.0);

      s << wxT("Supported Rates:") << e;
      for (int k = 0; k < (int) rates.size(); k++) {
         s << wxT("    ") << (int)rates[k] << e;
      }

      if (name == playDevice && info->maxOutputChannels > 0)
         playDeviceNum = j;

      if (name == recDevice && info->maxInputChannels > 0)
         recDeviceNum = j;

      // Sometimes PortAudio returns -1 if it cannot find a suitable default
      // device, so we just use the first one available
      if (recDeviceNum < 0 && info->maxInputChannels > 0){
         recDeviceNum = j;
      }
      if (playDeviceNum < 0 && info->maxOutputChannels > 0){
         playDeviceNum = j;
      }
   }

   bool haveRecDevice = (recDeviceNum >= 0);
   bool havePlayDevice = (playDeviceNum >= 0);

   s << wxT("==============================") << e;
   if(haveRecDevice){
      s << wxT("Selected recording device: ") << recDeviceNum << wxT(" - ") << recDevice << e;
   }else{
      s << wxT("No recording device found for '") << recDevice << wxT("'.") << e;
   }
   if(havePlayDevice){
      s << wxT("Selected playback device: ") << playDeviceNum << wxT(" - ") << playDevice << e;
   }else{
      s << wxT("No playback device found for '") << playDevice << wxT("'.") << e;
   }

   std::vector<long> supportedSampleRates;

   if(havePlayDevice && haveRecDevice){
      supportedSampleRates = GetSupportedSampleRates(playDeviceNum, recDeviceNum);

      s << wxT("Supported Rates:") << e;
      for (int k = 0; k < (int) supportedSampleRates.size(); k++) {
         s << wxT("    ") << (int)supportedSampleRates[k] << e;
      }
   }else{
      s << wxT("Cannot check mutual sample rates without both devices.") << e;
      return o.GetString();
   }

#if defined(USE_PORTMIXER)
   if (supportedSampleRates.size() > 0)
      {
      int highestSampleRate = supportedSampleRates.back();
      bool EmulateMixerInputVol = true;
      bool EmulateMixerOutputVol = true;
      float MixerInputVol = 1.0;
      float MixerOutputVol = 1.0;

      int error;

      PaStream *stream;

      PaStreamParameters playbackParameters;

      playbackParameters.device = playDeviceNum;
      playbackParameters.sampleFormat = paFloat32;
      playbackParameters.hostApiSpecificStreamInfo = NULL;
      playbackParameters.channelCount = 1;
      if (Pa_GetDeviceInfo(playDeviceNum)){
         playbackParameters.suggestedLatency =
            Pa_GetDeviceInfo(playDeviceNum)->defaultLowOutputLatency;
      }
      else{
         playbackParameters.suggestedLatency = DEFAULT_LATENCY_CORRECTION/1000.0;
      }

      PaStreamParameters captureParameters;

      captureParameters.device = recDeviceNum;
      captureParameters.sampleFormat = paFloat32;;
      captureParameters.hostApiSpecificStreamInfo = NULL;
      captureParameters.channelCount = 1;
      if (Pa_GetDeviceInfo(recDeviceNum)){
         captureParameters.suggestedLatency =
            Pa_GetDeviceInfo(recDeviceNum)->defaultLowInputLatency;
      }else{
         captureParameters.suggestedLatency = DEFAULT_LATENCY_CORRECTION/1000.0;
      }

      error = Pa_OpenStream(&stream,
                         &captureParameters, &playbackParameters,
                         highestSampleRate, paFramesPerBufferUnspecified,
                         paClipOff | paDitherOff,
                         audacityAudioCallback, NULL);

      if (error) {
         error = Pa_OpenStream(&stream,
                            &captureParameters, NULL,
                            highestSampleRate, paFramesPerBufferUnspecified,
                            paClipOff | paDitherOff,
                            audacityAudioCallback, NULL);
      }

      if (error) {
         s << wxT("Received ") << error << wxT(" while opening devices") << e;
         return o.GetString();
      }

      PxMixer *PortMixer = Px_OpenMixer(stream, 0);

      if (!PortMixer) {
         s << wxT("Unable to open Portmixer") << e;
         Pa_CloseStream(stream);
         return o.GetString();
      }

      s << wxT("==============================") << e;
      s << wxT("Available mixers:") << e;

      // FIXME: ? PortMixer errors on query not reported in GetDeviceInfo
      cnt = Px_GetNumMixers(stream);
      for (int i = 0; i < cnt; i++) {
         wxString name = wxSafeConvertMB2WX(Px_GetMixerName(stream, i));
         s << i << wxT(" - ") << name << e;
      }

      s << wxT("==============================") << e;
      s << wxT("Available recording sources:") << e;
      cnt = Px_GetNumInputSources(PortMixer);
      for (int i = 0; i < cnt; i++) {
         wxString name = wxSafeConvertMB2WX(Px_GetInputSourceName(PortMixer, i));
         s << i << wxT(" - ") << name << e;
      }

      s << wxT("==============================") << e;
      s << wxT("Available playback volumes:") << e;
      cnt = Px_GetNumOutputVolumes(PortMixer);
      for (int i = 0; i < cnt; i++) {
         wxString name = wxSafeConvertMB2WX(Px_GetOutputVolumeName(PortMixer, i));
         s << i << wxT(" - ") << name << e;
      }

      // Determine mixer capabilities - it it doesn't support either
      // input or output, we emulate them (by multiplying this value
      // by all incoming/outgoing samples)

      MixerOutputVol = Px_GetPCMOutputVolume(PortMixer);
      EmulateMixerOutputVol = false;
      Px_SetPCMOutputVolume(PortMixer, 0.0);
      if (Px_GetPCMOutputVolume(PortMixer) > 0.1)
         EmulateMixerOutputVol = true;
      Px_SetPCMOutputVolume(PortMixer, 0.2f);
      if (Px_GetPCMOutputVolume(PortMixer) < 0.1 ||
          Px_GetPCMOutputVolume(PortMixer) > 0.3)
         EmulateMixerOutputVol = true;
      Px_SetPCMOutputVolume(PortMixer, MixerOutputVol);

      MixerInputVol = Px_GetInputVolume(PortMixer);
      EmulateMixerInputVol = false;
      Px_SetInputVolume(PortMixer, 0.0);
      if (Px_GetInputVolume(PortMixer) > 0.1)
         EmulateMixerInputVol = true;
      Px_SetInputVolume(PortMixer, 0.2f);
      if (Px_GetInputVolume(PortMixer) < 0.1 ||
          Px_GetInputVolume(PortMixer) > 0.3)
         EmulateMixerInputVol = true;
      Px_SetInputVolume(PortMixer, MixerInputVol);

      Pa_CloseStream(stream);

      s << wxT("==============================") << e;
      s << wxT("Recording volume is ") << (EmulateMixerInputVol? wxT("emulated"): wxT("native")) << e;
      s << wxT("Playback volume is ") << (EmulateMixerOutputVol? wxT("emulated"): wxT("native")) << e;

      Px_CloseMixer(PortMixer);

      }  //end of massive if statement if a valid sample rate has been found
#endif
   return o.GetString();
}

// This method is the data gateway between the audio thread (which
// communicates with the disk) and the PortAudio callback thread
// (which communicates with the audio device).
void AudioIO::FillBuffers()
{
   unsigned int i;

   auto delayedHandler = [this] ( AudacityException * pException ) {
      // In the main thread, stop recording
      // This is one place where the application handles disk
      // exhaustion exceptions from wave track operations, without rolling
      // back to the last pushed undo state.  Instead, partial recording
      // results are pushed as a NEW undo state.  For this reason, as
      // commented elsewhere, we want an exception safety guarantee for
      // the output wave tracks, after the failed append operation, that
      // the tracks remain as they were after the previous successful
      // (block-level) appends.

      // Note that the Flush in StopStream() may throw another exception,
      // but StopStream() contains that exception, and the logic in
      // AudacityException::DelayedHandlerAction prevents redundant message
      // boxes.
      StopStream();
      DefaultDelayedHandlerAction{}( pException );
   };

   if (mPlaybackTracks.size() > 0)
   {
      // Though extremely unlikely, it is possible that some buffers
      // will have more samples available than others.  This could happen
      // if we hit this code during the PortAudio callback.  To keep
      // things simple, we only write as much data as is vacant in
      // ALL buffers, and advance the global time by that much.
      // MB: subtract a few samples because the code below has rounding errors
      auto nAvailable = (int)GetCommonlyAvailPlayback() - 10;

      //
      // Don't fill the buffers at all unless we can do the
      // full mMaxPlaybackSecsToCopy.  This improves performance
      // by not always trying to process tiny chunks, eating the
      // CPU unnecessarily.
      //
      // The exception is if we're at the end of the selected
      // region - then we should just fill the buffer.
      //
      if (nAvailable >= (int)mPlaybackSamplesToCopy ||
          (mPlayMode == PLAY_STRAIGHT &&
           nAvailable > 0 &&
           mWarpedTime+(nAvailable/mRate) >= mWarpedLength))
      {
         // Limit maximum buffer size (increases performance)
         auto available =
            std::min<size_t>( nAvailable, mPlaybackSamplesToCopy );

         // msmeyer: When playing a very short selection in looped
         // mode, the selection must be copied to the buffer multiple
         // times, to ensure, that the buffer has a reasonable size
         // This is the purpose of this loop.
         // PRL: or, when scrubbing, we may get work repeatedly from the
         // scrub queue.
         bool done = false;
         Maybe<wxMutexLocker> cleanup;
         do {
            // How many samples to produce for each channel.
            auto frames = available;
            bool progress = true;
#ifdef EXPERIMENTAL_SCRUBBING_SUPPORT
            if (mPlayMode == PLAY_SCRUB)
               // scrubbing does not use warped time and length
               frames = limitSampleBufferSize(frames, mScrubDuration);
            else
#endif
            {
               double deltat = frames / mRate;
               if (mWarpedTime + deltat > mWarpedLength)
               {
                  frames = (mWarpedLength - mWarpedTime) * mRate;
                  // Don't fall into an infinite loop, if loop-playing a selection
                  // that is so short, it has no samples: detect that case
                  progress =
                     !(mPlayMode == PLAY_LOOPED &&
                       mWarpedTime == 0.0 && frames == 0);
                  mWarpedTime = mWarpedLength;
               }
               else
                  mWarpedTime += deltat;
            }

            if (!progress)
               frames = available;

            for (i = 0; i < mPlaybackTracks.size(); i++)
            {
               // The mixer here isn't actually mixing: it's just doing
               // resampling, format conversion, and possibly time track
               // warping
               decltype(mPlaybackMixers[i]->Process(frames))
                  processed = 0;
               samplePtr warpedSamples;
               //don't do anything if we have no length.  In particular, Process() will fail an wxAssert
               //that causes a crash since this is not the GUI thread and wxASSERT is a GUI call.
               const bool silent = false;

               if (progress && !silent && frames > 0)
               {
                  processed = mPlaybackMixers[i]->Process(frames);
                  wxASSERT(processed <= frames);
                  warpedSamples = mPlaybackMixers[i]->GetBuffer();
                  const auto put = mPlaybackBuffers[i]->Put
                     (warpedSamples, floatSample, processed);
                  // wxASSERT(put == processed);
                  // but we can't assert in this thread
                  wxUnusedVar(put);
               }
               
               //if looping and processed is less than the full chunk/block/buffer that gets pulled from
               //other longer tracks, then we still need to advance the ring buffers or
               //we'll trip up on ourselves when we start them back up again.
               //if not looping we never start them up again, so its okay to not do anything
               // If scrubbing, we may be producing some silence.  Otherwise this should not happen,
               // but makes sure anyway that we produce equal
               // numbers of samples for all channels for this pass of the do-loop.
               if(processed < frames && mPlayMode != PLAY_STRAIGHT)
               {
                  mSilentBuf.Resize(frames, floatSample);
                  ClearSamples(mSilentBuf.ptr(), floatSample, 0, frames);
                  const auto put = mPlaybackBuffers[i]->Put
                     (mSilentBuf.ptr(), floatSample, frames - processed);
                  // wxASSERT(put == frames - processed);
                  // but we can't assert in this thread
                  wxUnusedVar(put);
               }
            }

            available -= frames;
            wxASSERT(available >= 0);

            switch (mPlayMode)
            {
				case PLAY_LOOPED:
				   done = !progress || (available == 0);
				   // msmeyer: If playing looped, check if we are at the end of the buffer
				   // and if yes, restart from the beginning.
				   if (mWarpedTime >= mWarpedLength)
				   {
					  for (i = 0; i < mPlaybackTracks.size(); i++)
						 mPlaybackMixers[i]->Restart();
					  mWarpedTime = 0.0;
				   }
				   break;
				default:
				   done = true;
				   break;
            }
         } while (!done);
      }
   }  // end of playback buffering

   if (!mRecordingException &&
       mCaptureTracks.size() > 0)
      GuardedCall( [&] {
         // start record buffering
         auto commonlyAvail = GetCommonlyAvailCapture();

         //
         // Determine how much this will add to captured tracks
         //
         double deltat = commonlyAvail / mRate;

         if (mAudioThreadShouldCallFillBuffersOnce ||
             deltat >= mMinCaptureSecsToCopy)
         {
            // Append captured samples to the end of the WaveTracks.
            // The WaveTracks have their own buffering for efficiency.
            AutoSaveFile blockFileLog;
            auto numChannels = mCaptureTracks.size();

            for( i = 0; i < numChannels; i++ )
            {
               auto avail = commonlyAvail;
               sampleFormat trackFormat = mCaptureTracks[i]->GetSampleFormat();

               AutoSaveFile appendLog;

               if( mFactor == 1.0 )
               {
                  SampleBuffer temp(avail, trackFormat);
                  const auto got =
                  mCaptureBuffers[i]->Get(temp.ptr(), trackFormat, avail);
                  // wxASSERT(got == avail);
                  // but we can't assert in this thread
                  wxUnusedVar(got);
                  // see comment in second handler about guarantee
                  mCaptureTracks[i]-> Append(temp.ptr(), trackFormat, avail, 1,
                                             &appendLog);
               }
               else
               {
                  size_t size = lrint(avail * mFactor);
                  SampleBuffer temp1(avail, floatSample);
                  SampleBuffer temp2(size, floatSample);
                  const auto got =
                  mCaptureBuffers[i]->Get(temp1.ptr(), floatSample, avail);
                  // wxASSERT(got == avail);
                  // but we can't assert in this thread
                  wxUnusedVar(got);
                  /* we are re-sampling on the fly. The last resampling call
                   * must flush any samples left in the rate conversion buffer
                   * so that they get recorded
                   */
                  const auto results =
                  mResample[i]->Process(mFactor, (float *)temp1.ptr(), avail,
                                        !IsStreamActive(), (float *)temp2.ptr(), size);
                  size = results.second;
                  // see comment in second handler about guarantee
                  mCaptureTracks[i]-> Append(temp2.ptr(), floatSample, size, 1,
                                             &appendLog);
               }

               if (!appendLog.IsEmpty())
               {
                  blockFileLog.StartTag(wxT("recordingrecovery"));
                  blockFileLog.WriteAttr(wxT("id"), mCaptureTracks[i]->GetAutoSaveIdent());
                  blockFileLog.WriteAttr(wxT("channel"), (int)i);
                  blockFileLog.WriteAttr(wxT("numchannels"), numChannels);
                  blockFileLog.WriteSubTree(appendLog);
                  blockFileLog.EndTag(wxT("recordingrecovery"));
               }
            }
         }
         // end of record buffering
      },
      // handler
      [this] ( AudacityException *pException ) {
         if ( pException ) {
            // So that we don't attempt to fill the recording buffer again
            // before the main thread stops recording
            SetRecordingException();
            return ;
         }
         else
            // Don't want to intercept other exceptions (?)
            throw;
      },
      delayedHandler
   );
}

void AudioIO::SetListener(AudioIOListener* listener)
{
   if (IsBusy())
      return;

   mListener = listener;
}

//////////////////////////////////////////////////////////////////////
//
//    PortAudio callback thread context
//
//////////////////////////////////////////////////////////////////////

#define MAX(a,b) ((a) > (b) ? (a) : (b))

static void DoSoftwarePlaythrough(const void *inputBuffer,
                                  sampleFormat inputFormat,
                                  unsigned inputChannels,
                                  float *outputBuffer,
                                  int len)
{
   for (unsigned int i=0; i < inputChannels; i++) {
      samplePtr inputPtr = ((samplePtr)inputBuffer) + (i * SAMPLE_SIZE(inputFormat));
      samplePtr outputPtr = ((samplePtr)outputBuffer) + (i * SAMPLE_SIZE(floatSample));

      CopySamples(inputPtr, inputFormat,
                  (samplePtr)outputPtr, floatSample,
                  len, true, inputChannels, 2);
   }

   // One mono input channel goes to both output channels...
   if (inputChannels == 1)
      for (int i=0; i < len; i++)
         outputBuffer[2*i + 1] = outputBuffer[2*i];
}

int audacityAudioCallback(const void *inputBuffer, void *outputBuffer,
                          unsigned long framesPerBuffer,
                          const PaStreamCallbackTimeInfo * WXUNUSED(timeInfo),
                          const PaStreamCallbackFlags statusFlags, void * WXUNUSED(userData) )
{
   auto numPlaybackChannels = gAudioIO->mNumPlaybackChannels;
   auto numPlaybackTracks = gAudioIO->mPlaybackTracks.size();
   auto numCaptureChannels = gAudioIO->mNumCaptureChannels;
   int callbackReturn = paContinue;
   void *tempBuffer = alloca(framesPerBuffer*sizeof(float)*
                             MAX(numCaptureChannels,numPlaybackChannels));
   float *tempFloats = (float*)tempBuffer;

   // output meter may need samples untouched by volume emulation
   float *outputMeterFloats;
   outputMeterFloats =
      (outputBuffer && gAudioIO->mEmulateMixerOutputVol &&
                       gAudioIO->mMixerOutputVol != 1.0) ?
         (float *)alloca(framesPerBuffer*numPlaybackChannels * sizeof(float)) :
         (float *)outputBuffer;

   unsigned int i;

   /* Send data to recording VU meter if applicable */

   if (gAudioIO->mInputMeter &&
         !gAudioIO->mInputMeter->IsMeterDisabled() &&
         inputBuffer) {
      // get here if meters are actually live , and being updated
      /* It's critical that we don't update the meters while StopStream is
       * trying to stop PortAudio, otherwise it can lead to a freeze.  We use
       * two variables to synchronize:
       *   mUpdatingMeters tells StopStream when the callback is about to enter
       *     the code where it might update the meters, and
       *   mUpdateMeters is how the rest of the code tells the callback when it
       *     is allowed to actually do the updating.
       * Note that mUpdatingMeters must be set first to avoid a race condition.
       */
      gAudioIO->mUpdatingMeters = true;
      if (gAudioIO->mUpdateMeters) {
         if (gAudioIO->mCaptureFormat == floatSample)
            gAudioIO->mInputMeter->UpdateDisplay(numCaptureChannels,
                                                 framesPerBuffer,
                                                 (float *)inputBuffer);
         else {
            CopySamples((samplePtr)inputBuffer, gAudioIO->mCaptureFormat,
                        (samplePtr)tempFloats, floatSample,
                        framesPerBuffer * numCaptureChannels);
            gAudioIO->mInputMeter->UpdateDisplay(numCaptureChannels,
                                                 framesPerBuffer,
                                                 tempFloats);
         }
      }
      gAudioIO->mUpdatingMeters = false;
   }  // end recording VU meter update

   // Stop recording if 'silence' is detected
   //
   // LL:  We'd gotten a little "dangerous" with the control toolbar calls
   //      here because we are not running in the main GUI thread.  Eventually
   //      the toolbar attempts to update the active project's status bar.
   //      But, since we're not in the main thread, we can get all manner of
   //      really weird failures.  Or none at all which is even worse, since
   //      we don't know a problem exists.
   //
   //      By using CallAfter(), we can schedule the call to the toolbar
   //      to run in the main GUI thread after the next event loop iteration.
   if(gAudioIO->mPauseRec && inputBuffer && gAudioIO->mInputMeter) {
      if(gAudioIO->mInputMeter->GetMaxPeak() < gAudioIO->mSilenceLevel ) {
         if(!gAudioIO->IsPaused()) {
            AudacityProject *p = GetActiveProject();
            ControlToolBar *bar = p->GetControlToolBar();
            bar->CallAfter(&ControlToolBar::Pause);
         }
      }
      else {
         if(gAudioIO->IsPaused()) {
            AudacityProject *p = GetActiveProject();
            ControlToolBar *bar = p->GetControlToolBar();
            bar->CallAfter(&ControlToolBar::Pause);
         }
      }
   }
   if( gAudioIO->mPaused )
   {
      if (outputBuffer && numPlaybackChannels > 0)
      {
         ClearSamples((samplePtr)outputBuffer, floatSample,
                      0, framesPerBuffer * numPlaybackChannels);

         if (inputBuffer && gAudioIO->mSoftwarePlaythrough) {
            DoSoftwarePlaythrough(inputBuffer, gAudioIO->mCaptureFormat,
                                  numCaptureChannels,
                                  (float *)outputBuffer, (int)framesPerBuffer);
         }
      }

      return paContinue;
   }

   if (gAudioIO->mStreamToken > 0)
   {
      //
      // Mix and copy to PortAudio's output buffer
      //

      if( outputBuffer && (numPlaybackChannels > 0) )
      {
         bool cut = false;
         bool linkFlag = false;

         float *outputFloats = (float *)outputBuffer;
         for( i = 0; i < framesPerBuffer*numPlaybackChannels; i++)
            outputFloats[i] = 0.0;

         if (inputBuffer && gAudioIO->mSoftwarePlaythrough) {
            DoSoftwarePlaythrough(inputBuffer, gAudioIO->mCaptureFormat,
                                  numCaptureChannels,
                                  (float *)outputBuffer, (int)framesPerBuffer);
         }

         // Copy the results to outputMeterFloats if necessary
         if (outputMeterFloats != outputFloats) {
            for (i = 0; i < framesPerBuffer*numPlaybackChannels; ++i) {
               outputMeterFloats[i] = outputFloats[i];
            }
         }

         if (gAudioIO->mSeek)
         {
            int token = gAudioIO->mStreamToken;
            wxMutexLocker locker(gAudioIO->mSuspendAudioThread);
            if (token != gAudioIO->mStreamToken)
               // This stream got destroyed while we waited for it
               return paAbort;

            // Pause audio thread and wait for it to finish
            gAudioIO->mAudioThreadFillBuffersLoopRunning = false;
            while( gAudioIO->mAudioThreadFillBuffersLoopActive == true )
            {
               wxMilliSleep( 50 );
            }

            // Calculate the NEW time position
            gAudioIO->mTime += gAudioIO->mSeek;
            gAudioIO->mTime = gAudioIO->LimitStreamTime(gAudioIO->mTime);
            gAudioIO->mSeek = 0.0;

            // Reset mixer positions and flush buffers for all tracks
            gAudioIO->mWarpedTime = gAudioIO->mTime - gAudioIO->mT0;
            gAudioIO->mWarpedTime = std::abs(gAudioIO->mWarpedTime);

            // Reset mixer positions and flush buffers for all tracks
            for (i = 0; i < numPlaybackTracks; i++)
            {
               gAudioIO->mPlaybackMixers[i]->Reposition(gAudioIO->mTime);
               const auto toDiscard =
                  gAudioIO->mPlaybackBuffers[i]->AvailForGet();
               const auto discarded =
                  gAudioIO->mPlaybackBuffers[i]->Discard( toDiscard );
               // wxASSERT( discarded == toDiscard );
               // but we can't assert in this thread
               wxUnusedVar(discarded);
            }

            // Reload the ring buffers
            gAudioIO->mAudioThreadShouldCallFillBuffersOnce = true;
            while( gAudioIO->mAudioThreadShouldCallFillBuffersOnce == true )
            {
               wxMilliSleep( 50 );
            }

            // Reenable the audio thread
            gAudioIO->mAudioThreadFillBuffersLoopRunning = true;

            return paContinue;
         }

         unsigned numSolo = 0;
         for(unsigned t = 0; t < numPlaybackTracks; t++ )
            if( gAudioIO->mPlaybackTracks[t]->GetSolo() )
               numSolo++;

         const WaveTrack **chans = (const WaveTrack **) alloca(numPlaybackChannels * sizeof(WaveTrack *));
         float **tempBufs = (float **) alloca(numPlaybackChannels * sizeof(float *));
         for (unsigned int c = 0; c < numPlaybackChannels; c++)
         {
            tempBufs[c] = (float *) alloca(framesPerBuffer * sizeof(float));
         }

         EffectManager & em = EffectManager::Get();
         em.RealtimeProcessStart();

         bool selected = false;
         int group = 0;
         int chanCnt = 0;
         decltype(framesPerBuffer) maxLen = 0;
         for (unsigned t = 0; t < numPlaybackTracks; t++)
         {
            const WaveTrack *vt = gAudioIO->mPlaybackTracks[t].get();

            chans[chanCnt] = vt;

            if (linkFlag)
               linkFlag = false;
            else {
               cut = false;

               // Cut if somebody else is soloing
               if (numSolo>0 && !vt->GetSolo())
                  cut = true;

               // Cut if we're muted (unless we're soloing)
               if (vt->GetMute() && !vt->GetSolo())
                  cut = true;

               linkFlag = vt->GetLinked();
               selected = vt->GetSelected();

               // If we have a mono track, clear the right channel
               if (!linkFlag)
               {
                  memset(tempBufs[1], 0, framesPerBuffer * sizeof(float));
               }
            }

#define ORIGINAL_DO_NOT_PLAY_ALL_MUTED_TRACKS_TO_END
#ifdef ORIGINAL_DO_NOT_PLAY_ALL_MUTED_TRACKS_TO_END
            decltype(framesPerBuffer) len = 0;
            // this is original code prior to r10680 -RBD
            if (cut)
            {
               len = gAudioIO->mPlaybackBuffers[t]->Discard(framesPerBuffer);
               // keep going here.  
               // we may still need to issue a paComplete.
            }
            else
            {
               len = gAudioIO->mPlaybackBuffers[t]->Get((samplePtr)tempBufs[chanCnt],
                                                         floatSample,
                                                         framesPerBuffer);
               if (len < framesPerBuffer)
                  // Pad with zeroes to the end, in case of a short channel
                  memset((void*)&tempBufs[chanCnt][len], 0,
                     (framesPerBuffer - len) * sizeof(float));

               chanCnt++;
            }

            // PRL:  Bug1104:
            // There can be a difference of len in different loop passes if one channel
            // of a stereo track ends before the other!  Take a max!
            maxLen = std::max(maxLen, len);


            if (linkFlag)
            {
               continue;
            }
#else
            // This code was reorganized so that if all audio tracks
            // are muted, we still return paComplete when the end of
            // a selection is reached.
            // Vaughan, 2011-10-20: Further comments from Roger, by off-list email:
            //    ...something to do with what it means to mute all audio tracks. E.g. if you
            // mute all and play, does the playback terminate immediately or play
            // silence? If it terminates immediately, does that terminate any MIDI
            // playback that might also be going on? ...Maybe muted audio tracks + MIDI,
            // the playback would NEVER terminate. ...I think the #else part is probably preferable...
            size_t len;
            if (cut)
            {
               len =
                  gAudioIO->mPlaybackBuffers[t]->Discard(framesPerBuffer);
            } else
            {
               len =
                  gAudioIO->mPlaybackBuffers[t]->Get((samplePtr)tempFloats,
                                                     floatSample,
                                                     framesPerBuffer);
            }
#endif

            // Last channel seen now
            len = maxLen;

            if( !cut && selected )
            {
               len = em.RealtimeProcess(group, chanCnt, tempBufs, len);
            }
            group++;

            // If our buffer is empty and the time indicator is past
            // the end, then we've actually finished playing the entire
            // selection.
            // msmeyer: We never finish if we are playing looped
            // PRL: or scrubbing.
            if (len == 0 &&
                gAudioIO->mPlayMode == AudioIO::PLAY_STRAIGHT) {
               if ((gAudioIO->ReversedTime()
                  ? gAudioIO->mTime <= gAudioIO->mT1
                  : gAudioIO->mTime >= gAudioIO->mT1))
                  callbackReturn = paComplete;
            }

            if (cut) // no samples to process, they've been discarded
               continue;

            for (int c = 0; c < chanCnt; c++)
            {
               vt = chans[c];

               if (vt->GetChannel() == Track::LeftChannel ||
                   vt->GetChannel() == Track::MonoChannel)
               {
                  float gain = vt->GetChannelGain(0);

                  // Output volume emulation: possibly copy meter samples, then
                  // apply volume, then copy to the output buffer
                  if (outputMeterFloats != outputFloats)
                     for (decltype(len) i = 0; i < len; ++i)
                        outputMeterFloats[numPlaybackChannels*i] +=
                           gain*tempFloats[i];

                  if (gAudioIO->mEmulateMixerOutputVol)
                     gain *= gAudioIO->mMixerOutputVol;

                  for(decltype(len) i = 0; i < len; i++)
                     outputFloats[numPlaybackChannels*i] += gain*tempBufs[c][i];
               }

               if (vt->GetChannel() == Track::RightChannel ||
                   vt->GetChannel() == Track::MonoChannel)
               {
                  float gain = vt->GetChannelGain(1);

                  // Output volume emulation (as above)
                  if (outputMeterFloats != outputFloats)
                     for (decltype(len) i = 0; i < len; ++i)
                        outputMeterFloats[numPlaybackChannels*i+1] +=
                           gain*tempFloats[i];

                  if (gAudioIO->mEmulateMixerOutputVol)
                     gain *= gAudioIO->mMixerOutputVol;

                  for(decltype(len) i = 0; i < len; i++)
                     outputFloats[numPlaybackChannels*i+1] += gain*tempBufs[c][i];
               }
            }

            chanCnt = 0;
         }
         // Poke: If there are no playback tracks, then the earlier check
         // about the time indicator being passed the end won't happen;
         // do it here instead (but not if looping or scrubbing)
         if (numPlaybackTracks == 0 &&
            gAudioIO->mPlayMode == AudioIO::PLAY_STRAIGHT)
         {
            if ((gAudioIO->ReversedTime()
               ? gAudioIO->mTime <= gAudioIO->mT1
               : gAudioIO->mTime >= gAudioIO->mT1)) {
               callbackReturn = paComplete;
            }
         }

         em.RealtimeProcessEnd();

         gAudioIO->mLastPlaybackTimeMillis = ::wxGetLocalTimeMillis();

         //
         // Clip output to [-1.0,+1.0] range (msmeyer)
         //
         for( i = 0; i < framesPerBuffer*numPlaybackChannels; i++)
         {
            float f = outputFloats[i];
            if (f > 1.0)
               outputFloats[i] = 1.0;
            else if (f < -1.0)
               outputFloats[i] = -1.0;
         }

         // Same for meter output
         if (outputMeterFloats != outputFloats)
         {
            for (i = 0; i < framesPerBuffer*numPlaybackChannels; ++i)
            {
               float f = outputMeterFloats[i];
               if (f > 1.0)
                  outputMeterFloats[i] = 1.0;
               else if (f < -1.0)
                  outputMeterFloats[i] = -1.0;
            }
         }
      }

      //
      // Copy from PortAudio to our input buffers.
      //

      if( inputBuffer && (numCaptureChannels > 0) )
      {
         // If there are no playback tracks, and we are recording, then the
         // earlier checks for being passed the end won't happen, so do it here.
         if (gAudioIO->mTime >= gAudioIO->mT1) {
            callbackReturn = paComplete;
         }

         // The error likely from a too-busy CPU falling behind real-time data
         // is paInputOverflow
         bool inputError =
            (statusFlags & (paInputOverflow))
            && !(statusFlags & paPrimingOutput);

         // But it seems it's easy to get false positives, at least on Mac
         // So we have not decided to enable this extra detection yet in
         // production

         size_t len = framesPerBuffer;
         for(unsigned t = 0; t < numCaptureChannels; t++)
            len = std::min( len,
                           gAudioIO->mCaptureBuffers[t]->AvailForPut());

         if (gAudioIO->mSimulateRecordingErrors && 100LL * rand() < RAND_MAX)
            // Make spurious errors for purposes of testing the error
            // reporting
            len = 0;

         // A different symptom is that len < framesPerBuffer because
         // the other thread, executing FillBuffers, isn't consuming fast
         // enough from mCaptureBuffers; maybe it's CPU-bound, or maybe the
         // storage device it writes is too slow
         if (gAudioIO->mDetectDropouts &&
             ((gAudioIO->mDetectUpstreamDropouts && inputError) ||
              len < framesPerBuffer) ) {
            // Assume that any good partial buffer should be written leftmost
            // and zeroes will be padded after; label the zeroes.
            auto start = gAudioIO->mTime + len / gAudioIO->mRate;
            auto duration = (framesPerBuffer - len) / gAudioIO->mRate;
            auto interval = std::make_pair( start, duration );
            gAudioIO->mLostCaptureIntervals.push_back( interval );
         }

         if (len < framesPerBuffer)
         {
            gAudioIO->mLostSamples += (framesPerBuffer - len);
            wxPrintf(wxT("lost %d samples\n"), (int)(framesPerBuffer - len));
         }

         if (len > 0) {
            for(unsigned t = 0; t < numCaptureChannels; t++) {

               // dmazzoni:
               // Un-interleave.  Ugly special-case code required because the
               // capture channels could be in three different sample formats;
               // it'd be nice to be able to call CopySamples, but it can't
               // handle multiplying by the gain and then clipping.  Bummer.

               switch(gAudioIO->mCaptureFormat) {
               case floatSample: {
                  float *inputFloats = (float *)inputBuffer;
                  for( i = 0; i < len; i++)
                     tempFloats[i] =
                        inputFloats[numCaptureChannels*i+t];
               } break;
               case int24Sample:
                  // We should never get here. Audacity's int24Sample format
                  // is different from PortAudio's sample format and so we
                  // make PortAudio return float samples when recording in
                  // 24-bit samples.
                  wxASSERT(false);
                  break;
               case int16Sample: {
                  short *inputShorts = (short *)inputBuffer;
                  short *tempShorts = (short *)tempBuffer;
                  for( i = 0; i < len; i++) {
                     float tmp = inputShorts[numCaptureChannels*i+t];
                     if (tmp > 32767)
                        tmp = 32767;
                     if (tmp < -32768)
                        tmp = -32768;
                     tempShorts[i] = (short)(tmp);
                  }
               } break;
               } // switch

               const auto put =
                  gAudioIO->mCaptureBuffers[t]->Put(
                     (samplePtr)tempBuffer, gAudioIO->mCaptureFormat, len);
               // wxASSERT(put == len);
               // but we can't assert in this thread
               wxUnusedVar(put);
            }
         }
      }

	double delta = framesPerBuffer / gAudioIO->mRate;
	if (gAudioIO->ReversedTime())
		delta *= -1.0;

	gAudioIO->mTime += delta;

      // Wrap to start if looping
      if (gAudioIO->mPlayMode == AudioIO::PLAY_LOOPED)
      {
         while (gAudioIO->ReversedTime()
            ? gAudioIO->mTime <= gAudioIO->mT1
            : gAudioIO->mTime >= gAudioIO->mT1)
         {
            // LL:  This is not exactly right, but I'm at my wits end trying to
            //      figure it out.  Feel free to fix it.  :-)
            // MB: it's much easier than you think, mTime isn't warped at all!
            gAudioIO->mTime -= gAudioIO->mT1 - gAudioIO->mT0;
         }
      }

      // Record the reported latency from PortAudio.
      // TODO: Don't recalculate this with every callback?

   } // if mStreamToken > 0
   else {
      // No tracks to play, but we should clear the output, and
      // possibly do software playthrough...

      if( outputBuffer && (numPlaybackChannels > 0) ) {
         float *outputFloats = (float *)outputBuffer;
         for( i = 0; i < framesPerBuffer*numPlaybackChannels; i++)
            outputFloats[i] = 0.0;

         if (inputBuffer && gAudioIO->mSoftwarePlaythrough) {
            DoSoftwarePlaythrough(inputBuffer, gAudioIO->mCaptureFormat,
                                  numCaptureChannels,
                                  (float *)outputBuffer, (int)framesPerBuffer);
         }

         // Copy the results to outputMeterFloats if necessary
         if (outputMeterFloats != outputFloats) {
            for (i = 0; i < framesPerBuffer*numPlaybackChannels; ++i) {
               outputMeterFloats[i] = outputFloats[i];
            }
         }
      }

   }
   /* Send data to playback VU meter if applicable */
   if (gAudioIO->mOutputMeter &&
      !gAudioIO->mOutputMeter->IsMeterDisabled() &&
      outputMeterFloats) {
      // Get here if playback meter is live
      /* It's critical that we don't update the meters while StopStream is
       * trying to stop PortAudio, otherwise it can lead to a freeze.  We use
       * two variables to synchronize:
       *  mUpdatingMeters tells StopStream when the callback is about to enter
       *    the code where it might update the meters, and
       *  mUpdateMeters is how the rest of the code tells the callback when it
       *    is allowed to actually do the updating.
       * Note that mUpdatingMeters must be set first to avoid a race condition.
       */
      gAudioIO->mUpdatingMeters = true;
      if (gAudioIO->mUpdateMeters) {
         gAudioIO->mOutputMeter->UpdateDisplay(numPlaybackChannels,
                                               framesPerBuffer,
                                               outputMeterFloats);

         //v Vaughan, 2011-02-25: Moved this update back to TrackPanel::OnTimer()
         //    as it helps with playback issues reported by Bill and noted on Bug 258.
         //    The problem there occurs if Software Playthrough is on.
         //    Could conditionally do the update here if Software Playthrough is off,
         //    and in TrackPanel::OnTimer() if Software Playthrough is on, but not now.
         // PRL 12 Jul 2015: and what was in TrackPanel::OnTimer is now handled by means of event
         // type EVT_TRACK_PANEL_TIMER
         //AudacityProject* pProj = GetActiveProject();
         //MixerBoard* pMixerBoard = pProj->GetMixerBoard();
         //if (pMixerBoard)
         //   pMixerBoard->UpdateMeters(gAudioIO->GetStreamTime(),
         //                              (pProj->mLastPlayMode == loopedPlay));
      }
      gAudioIO->mUpdatingMeters = false;
   }  // end playback VU meter update

   return callbackReturn;
}
