/**********************************************************************

  Audacity: A Digital Audio Editor

  AudioIO.h

  Dominic Mazzoni

  Use the PortAudio library to play and record sound

**********************************************************************/

#ifndef __AUDACITY_AUDIO_IO__
#define __AUDACITY_AUDIO_IO__

#include "portaudio.h"
#include "Audacity.h"
#include "Experimental.h"

#include "MemoryX.h"
#include <utility>
#include <vector>
#include <wx/atomic.h>
#include <wx/weakref.h>

#include <wx/event.h>
#include <wx/string.h>
#include <wx/thread.h>

#include "SampleFormat.h"

class AudioIO;
class RingBuffer;
class Mixer;
class Resample;
class AudioThread;
class MeterPanel;
class SelectedRegion;

class AudacityProject;

class WaveTrack;
using WaveTrackArray = std::vector < std::shared_ptr < WaveTrack > >;
using WaveTrackConstArray = std::vector < std::shared_ptr < const WaveTrack > >;

extern AUDACITY_DLL_API AudioIO *gAudioIO;

void InitAudioIO();
void DeinitAudioIO();
wxString DeviceName(const PaDeviceInfo* info);
wxString HostName(const PaDeviceInfo* info);
bool ValidateDeviceNames();

class AudioIOListener;

// #include <cfloat> if you need this constant
#define BAD_STREAM_TIME (-DBL_MAX)

#define MAX_MIDI_BUFFER_SIZE 5000
#define DEFAULT_SYNTH_LATENCY 5

#define DEFAULT_LATENCY_DURATION 100.0
#define DEFAULT_LATENCY_CORRECTION -130.0

wxDECLARE_EXPORTED_EVENT(AUDACITY_DLL_API,
                         EVT_AUDIOIO_PLAYBACK, wxCommandEvent);
wxDECLARE_EXPORTED_EVENT(AUDACITY_DLL_API,
                         EVT_AUDIOIO_CAPTURE, wxCommandEvent);
wxDECLARE_EXPORTED_EVENT(AUDACITY_DLL_API,
                         EVT_AUDIOIO_MONITOR, wxCommandEvent);

// To avoid growing the argument list of StartStream, add fields here
struct AudioIOStartStreamOptions
{
   explicit
   AudioIOStartStreamOptions(double rate_)
      : listener(NULL)
      , rate(rate_)
      , playLooped(false)
      , cutPreviewGapStart(0.0)
      , cutPreviewGapLen(0.0)
      , pStartTime(NULL)
   {}

   AudioIOListener* listener;
   double rate;
   bool playLooped;
   double cutPreviewGapStart;
   double cutPreviewGapLen;
   double * pStartTime;
};

// This workaround makes pause and stop work when output is to GarageBand,
// which seems not to implement the notes-off message correctly.
#define AUDIO_IO_GB_MIDI_WORKAROUND

class AUDACITY_DLL_API AudioIO final {

 public:
   AudioIO();
   ~AudioIO();

   AudioIOListener* GetListener() { return mListener; }
   void SetListener(AudioIOListener* listener);

   /** \brief Start up Portaudio for capture and recording as needed for
    * input monitoring and software playthrough only
    *
    * This uses the Default project sample format, current sample rate, and
    * selected number of input channels to open the recording device and start
    * reading input data. If software playthrough is enabled, it also opens
    * the output device in stereo to play the data through */
   void StartMonitoring(double sampleRate);

   /** \brief Start recording or playing back audio
    *
    * Allocates buffers for recording and playback, gets the Audio thread to
    * fill them, and sets the stream rolling.
    * If successful, returns a token identifying this particular stream
    * instance.  For use with IsStreamActive() below */

   int StartStream(const WaveTrackConstArray &playbackTracks, const WaveTrackArray &captureTracks,
                   double t0, double t1,
                   const AudioIOStartStreamOptions &options);

   /** \brief Stop recording, playback or input monitoring.
    *
    * Does quite a bit of housekeeping, including switching off monitoring,
    * flushing recording buffers out to wave tracks, and applies latency
    * correction to recorded tracks if necessary */
   void StopStream();
   /** \brief Move the playback / recording position of the current stream
    * by the specified amount from where it is now */
   void SeekStream(double seconds) { mSeek = seconds; }

   /** \brief  Returns true if audio i/o is busy starting, stopping, playing,
    * or recording.
    *
    * When this is false, it's safe to start playing or recording */
   bool IsBusy();

   /** \brief Returns true if the audio i/o is running at all, but not during
    * cleanup
    *
    * Doesn't return true if the device has been closed but some disk i/o or
    * cleanup is still going on. If you want to know if it's safe to start a
    * NEW stream, use IsBusy() */
   bool IsStreamActive();
   bool IsStreamActive(int token);

   wxLongLong GetLastPlaybackTime() const { return mLastPlaybackTimeMillis; }
   AudacityProject *GetOwningProject() const { return mOwningProject; }

   /** \brief Returns true if the stream is active, or even if audio I/O is
    * busy cleaning up its data or writing to disk.
    *
    * This is used by TrackPanel to determine when a track has been completely
    * recorded, and it's safe to flush to disk. */
   bool IsAudioTokenActive(int token);

   /** \brief Returns true if we're monitoring input (but not recording or
    * playing actual audio) */
   bool IsMonitoring();

   /** \brief Pause and un-pause playback and recording */
   void SetPaused(bool state);
   /** \brief Find out if playback / recording is currently paused */
   bool IsPaused();

   /** @brief Find out if the input hardware level control is available
    *
    * Checks the mInputMixerWorks variable, which is set up in
    * AudioIO::HandleDeviceChange(). External people care, because we want to
    * disable the UI if it doesn't work.
    */
   bool InputMixerWorks();

   /** @brief Find out if the output level control is being emulated via software attenuation
    *
    * Checks the mEmulateMixerOutputVol variable, which is set up in
    * AudioIO::HandleDeviceChange(). External classes care, because we want to
    * modify the UI if it doesn't work.
    */
   bool OutputMixerEmulated();

   /** \brief Get the list of inputs to the current mixer device
    *
    * Returns an array of strings giving the names of the inputs to the
    * soundcard mixer (driven by PortMixer) */
   wxArrayString GetInputSourceNames();

   /** \brief update state after changing what audio devices are selected
    *
    * Called when the devices stored in the preferences are changed to update
    * the audio mixer capabilities
    *
    * \todo: Make this do a sample rate query and store the result in the
    * AudioIO object to avoid doing it later? Would simplify the
    * GetSupported*Rate functions considerably */
   void HandleDeviceChange();

   /** \brief Get a list of sample rates the output (playback) device
    * supports.
    *
    * If no information about available sample rates can be fetched,
    * an empty list is returned.
    *
    * You can explicitely give the index of the device.  If you don't
    * give it, the currently selected device from the preferences will be used.
    *
    * You may also specify a rate for which to check in addition to the
    * standard rates.
    */
   static std::vector<long> GetSupportedPlaybackRates(int DevIndex = -1,
                                                double rate = 0.0);

   /** \brief Get a list of sample rates the input (recording) device
    * supports.
    *
    * If no information about available sample rates can be fetched,
    * an empty list is returned.
    *
    * You can explicitely give the index of the device.  If you don't
    * give it, the currently selected device from the preferences will be used.
    *
    * You may also specify a rate for which to check in addition to the
    * standard rates.
    */
   static std::vector<long> GetSupportedCaptureRates(int devIndex = -1,
                                               double rate = 0.0);

   /** \brief Get a list of sample rates the current input/output device
    * combination supports.
    *
    * Since there is no concept (yet) for different input/output
    * sample rates, this currently returns only sample rates that are
    * supported on both the output and input device. If no information
    * about available sample rates can be fetched, it returns a default
    * list.
    * You can explicitely give the indexes of the playDevice/recDevice.
    * If you don't give them, the selected devices from the preferences
    * will be used.
    * You may also specify a rate for which to check in addition to the
    * standard rates.
    */
   static std::vector<long> GetSupportedSampleRates(int playDevice = -1,
                                              int recDevice = -1,
                                       double rate = 0.0);

   /** \brief Get a supported sample rate which can be used a an optimal
    * default.
    *
    * Currently, this uses the first supported rate in the list
    * [44100, 48000, highest sample rate]. Used in Project as a default value
    * for project rates if one cannot be retrieved from the preferences.
    * So all in all not that useful or important really
    */
   static int GetOptimalSupportedSampleRate();

   /** \brief During playback, the (unwarped) track time most recently played
    *
    * When playing looped, this will start from t0 again,
    * too. So the returned time should be always between
    * t0 and t1
    */
   double GetStreamTime();

   sampleFormat GetCaptureFormat() { return mCaptureFormat; }
   unsigned GetNumPlaybackChannels() const { return mNumPlaybackChannels; }
   unsigned GetNumCaptureChannels() const { return mNumCaptureChannels; }

   /** \brief Array of common audio sample rates
    *
    * These are the rates we will always support, regardless of hardware support
    * for them (by resampling in audacity if needed) */
   static const int StandardRates[];
   /** \brief How many standard sample rates there are */
   static const int NumStandardRates;

   /** \brief Get diagnostic information on all the available audio I/O devices
    *
    */
   wxString GetDeviceInfo();

   /** \brief Ensure selected device names are valid
    *
    */
   static bool ValidateDeviceNames(const wxString &play, const wxString &rec);

   bool IsAvailable(AudacityProject *projecT);
   void SetCaptureMeter(AudacityProject *project, MeterPanel *meter);
   void SetPlaybackMeter(AudacityProject *project, MeterPanel *meter);

private:
   /** \brief Set the current VU meters - this should be done once after
    * each call to StartStream currently */
   void SetMeters();

   /** \brief Return a valid sample rate that is supported by the current I/O
    * device(s).
    *
    * The return from this function is used to determine the sample rate that
    * audacity actually runs the audio I/O stream at. if there is no suitable
    * rate available from the hardware, it returns 0.
    * The sampleRate argument gives the desired sample rate (the rate of the
    * audio to be handeled, i.e. the currently Project Rate).
    * capturing is true if the stream is capturing one or more audio channels,
    * and playing is true if one or more channels are being played. */
   double GetBestRate(bool capturing, bool playing, double sampleRate);

   /** \brief Opens the portaudio stream(s) used to do playback or recording
    * (or both) through.
    *
    * The sampleRate passed is the Project Rate of the active project. It may
    * or may not be actually supported by playback or recording hardware
    * currently in use (for many reasons). The number of Capture and Playback
    * channels requested includes an allocation for doing software playthrough
    * if necessary. The captureFormat is used for recording only, the playback
    * being floating point always. Returns true if the stream opened sucessfully
    * and false if it did not. */
   bool StartPortAudioStream(double sampleRate,
                             unsigned int numPlaybackChannels,
                             unsigned int numCaptureChannels,
                             sampleFormat captureFormat);
   void FillBuffers();

   /** \brief Get the number of audio samples free in all of the playback
   * buffers.
   *
   * Returns the smallest of the buffer free space values in the event that
   * they are different. */
   size_t GetCommonlyAvailPlayback();

   /** \brief Get the number of audio samples ready in all of the recording
    * buffers.
    *
    * Returns the smallest of the number of samples available for storage in
    * the recording buffers (i.e. the number of samples that can be read from
    * all record buffers without underflow). */
   size_t GetCommonlyAvailCapture();

   /** \brief get the index of the supplied (named) recording device, or the
    * device selected in the preferences if none given.
    *
    * Pure utility function, but it comes round a number of times in the code
    * and would be neater done once. If the device isn't found, return the
    * default device index.
    */
   static int getRecordDevIndex(const wxString &devName = wxEmptyString);
   /** \brief get the index of the device selected in the preferences.
    *
    * If the device isn't found, returns -1
    */

   /** \brief get the index of the supplied (named) playback device, or the
    * device selected in the preferences if none given.
    *
    * Pure utility function, but it comes round a number of times in the code
    * and would be neater done once. If the device isn't found, return the
    * default device index.
    */
   static int getPlayDevIndex(const wxString &devName = wxEmptyString);

   /** \brief Array of audio sample rates to try to use
    *
    * These are the rates we will check if a device supports, and is as long
    * as I can think of (to try and work out what the card can do) */
   static const int RatesToTry[];
   /** \brief How many sample rates to try */
   static const int NumRatesToTry;

   /** \brief True if the end time is before the start time */
   bool ReversedTime() const
   {
      return mT1 < mT0;
   }
   /** \brief Clamps the given time to be between mT0 and mT1
    *
    * Returns the bound if the value is out of bounds; does not wrap.
    * Returns a time in seconds.
    * @param absoluteTime A time in seconds, usually mTime
    */
   double LimitStreamTime(double absoluteTime) const;

   /** \brief Normalizes the given time, clamping it and handling gaps from cut preview.
    *
    * Clamps the time (unless scrubbing), and skips over the cut section.
    * Returns a time in seconds.
    * @param absoluteTime A time in seconds, usually mTime
    */
   double NormalizeStreamTime(double absoluteTime) const;

   /** \brief Clean up after StartStream if it fails.
     *
     * If bOnlyBuffers is specified, it only cleans up the buffers. */
   void StartStreamCleanup(bool bOnlyBuffers = false);

   std::unique_ptr<AudioThread> mThread;
   ArrayOf<std::unique_ptr<Resample>> mResample;
   ArrayOf<std::unique_ptr<RingBuffer>> mCaptureBuffers;
   WaveTrackArray      mCaptureTracks;
   ArrayOf<std::unique_ptr<RingBuffer>> mPlaybackBuffers;
   WaveTrackConstArray mPlaybackTracks;

   ArrayOf<std::unique_ptr<Mixer>> mPlaybackMixers;
   volatile int        mStreamToken;
   static int          mNextStreamToken;
   double              mFactor;
   /// Audio playback rate in samples per second
   double              mRate;
   /// Playback starts at offset of mT0, which is measured in seconds.
   double              mT0;
   /// Playback ends at offset of mT1, which is measured in seconds.  Note that mT1 may be less than mT0 during scrubbing.
   double              mT1;
   /// Current time position during playback, in seconds.  Between mT0 and mT1.
   double              mTime;

   /// Accumulated real time (not track position), starting at zero (unlike
   ///  mTime), and wrapping back to zero each time around looping play.
   /// Thus, it is the length in real seconds between mT0 and mTime.
   double              mWarpedTime;

   /// Real length to be played (if looping, for each pass) after warping via a
   /// time track, computed just once when starting the stream.
   /// Length in real seconds between mT0 and mT1.  Always positive.
   double              mWarpedLength;

   double              mSeek;
   double              mPlaybackRingBufferSecs;
   double              mCaptureRingBufferSecs;
   size_t              mPlaybackSamplesToCopy;
   double              mMinCaptureSecsToCopy;
   /// True if audio playback is paused
   bool                mPaused;
   PaStream           *mPortStreamV19;
   bool                mSoftwarePlaythrough;
   /// True if Sound Activated Recording is enabled
   bool                mPauseRec;
   float               mSilenceLevel;
   unsigned int        mNumCaptureChannels;
   unsigned int        mNumPlaybackChannels;
   sampleFormat        mCaptureFormat;
   unsigned long long  mLostSamples{ 0 };
   volatile bool       mAudioThreadShouldCallFillBuffersOnce;
   volatile bool       mAudioThreadFillBuffersLoopRunning;
   volatile bool       mAudioThreadFillBuffersLoopActive;

   wxLongLong          mLastPlaybackTimeMillis;

   volatile double     mLastRecordingOffset;
   PaError             mLastPaError;

   AudacityProject    *mOwningProject;
   wxWeakRef<MeterPanel> mInputMeter{};
   MeterPanel         *mOutputMeter;
   bool                mUpdateMeters;
   volatile bool       mUpdatingMeters;

   bool                mEmulateMixerOutputVol;
   /** @brief Can we control the hardware input level?
    *
    * This flag is set to true if using portmixer to control the
    * input volume seems to be working (and so we offer the user the control),
    * and to false (locking the control out) otherwise. This avoids stupid
    * scaled clipping problems when trying to do software emulated input volume
    * control */
   bool                mInputMixerWorks;
   float               mMixerOutputVol;

   volatile enum {
      PLAY_STRAIGHT,
      PLAY_LOOPED,
   }                   mPlayMode;
   double              mCutPreviewGapStart;
   double              mCutPreviewGapLen;

   GrowableSampleBuffer mSilentBuf;

   AudioIOListener*    mListener;

   friend class AudioThread;

   friend void InitAudioIO();

   bool mUsingAlsa { false };

   // For cacheing supported sample rates
   static int mCachedPlaybackIndex;
   static std::vector<long> mCachedPlaybackRates;
   static int mCachedCaptureIndex;
   static std::vector<long> mCachedCaptureRates;
   static std::vector<long> mCachedSampleRates;
   static double mCachedBestRateIn;
   static double mCachedBestRateOut;

   /** brief The function which is called from PortAudio's callback thread
    * context to collect and deliver audio for / from the sound device.
    *
    * This covers recording, playback, and doing both simultaneously. It is
    * also invoked to do monitoring and software playthrough. Note that dealing
    * with the two buffers needs some care to ensure that the right things
    * happen for all possible cases.
    * @param inputBuffer Buffer of length framesPerBuffer containing samples
    * from the sound card, or null if not capturing audio. Note that the data
    * type will depend on the format of audio data that was chosen when the
    * stream was created (so could be floats or various integers)
    * @param outputBuffer Uninitialised buffer of length framesPerBuffer which
    * will be sent to the sound card after the callback, or null if not playing
    * audio back.
    * @param framesPerBuffer The length of the playback and recording buffers
    * @param PaStreamCallbackTimeInfo Pointer to PortAudio time information
    * structure, which tells us how long we have been playing / recording
    * @param statusFlags PortAudio stream status flags
    * @param userData pointer to user-defined data structure. Provided for
    * flexibility by PortAudio, but not used by Audacity - the data is stored in
    * the AudioIO class instead.
    */
   friend int audacityAudioCallback(
                const void *inputBuffer, void *outputBuffer,
                unsigned long framesPerBuffer,
                const PaStreamCallbackTimeInfo *timeInfo,
                PaStreamCallbackFlags statusFlags, void *userData );

   // Serialize main thread and PortAudio thread's attempts to pause and change
   // the state used by the third, Audio thread.
   wxMutex mSuspendAudioThread;

   // A flag tested and set in one thread, cleared in another.  Perhaps
   // this guarantee of atomicity is more cautious than necessary.
   wxAtomicInt mRecordingException {};
   void SetRecordingException()
      { wxAtomicInc( mRecordingException ); }
   void ClearRecordingException()
      { if (mRecordingException) wxAtomicDec( mRecordingException ); }

   std::vector< std::pair<double, double> > mLostCaptureIntervals;
   bool mDetectDropouts{ true };

public:
   // Pairs of starting time and duration
   const std::vector< std::pair<double, double> > &LostCaptureIntervals()
   { return mLostCaptureIntervals; }

   // Used only for testing purposes in alpha builds
   bool mSimulateRecordingErrors{ false };

   // Whether to check the error code passed to audacityAudioCallback to
   // detect more dropouts
   bool mDetectUpstreamDropouts{ true };
};

#endif
