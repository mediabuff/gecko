/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
/*
Each video element based on nsBuiltinDecoder has a state machine to manage
its play state and keep the current frame up to date. All state machines
share time in a single shared thread. Each decoder also has one thread
dedicated to decoding audio and video data. This thread is shutdown when
playback is paused. Each decoder also has a thread to push decoded audio
to the hardware. This thread is not created until playback starts, but
currently is not destroyed when paused, only when playback ends.

The decoder owns the resources for downloading the media file, and the
high level state. It holds an owning reference to the state machine that
owns all the resources related to decoding data, and manages the low level
decoding operations and A/V sync.

Each state machine runs on the shared state machine thread. Every time some
action is required for a state machine, it is scheduled to run on the shared
the state machine thread. The state machine runs one "cycle" on the state
machine thread, and then returns. If necessary, it will schedule itself to
run again in future. While running this cycle, it must not block the
thread, as other state machines' events may need to run. State shared
between a state machine's threads is synchronised via the monitor owned
by its nsBuiltinDecoder object.

The Main thread controls the decode state machine by setting the value
of a mPlayState variable and notifying on the monitor based on the
high level player actions required (Seek, Pause, Play, etc).

The player states are the states requested by the client through the
DOM API.  They represent the desired state of the player, while the
decoder's state represents the actual state of the decoder.

The high level state of the player is maintained via a PlayState value.
It can have the following states:

START
  The decoder has been initialized but has no resource loaded.
PAUSED
  A request via the API has been received to pause playback.
LOADING
  A request via the API has been received to load a resource.
PLAYING
  A request via the API has been received to start playback.
SEEKING
  A request via the API has been received to start seeking.
COMPLETED
  Playback has completed.
SHUTDOWN
  The decoder is about to be destroyed.

State transition occurs when the Media Element calls the Play, Seek,
etc methods on the nsBuiltinDecoder object. When the transition
occurs nsBuiltinDecoder then calls the methods on the decoder state
machine object to cause it to behave as required by the play state.
State transitions will likely schedule the state machine to run to
affect the change.

An implementation of the nsBuiltinDecoderStateMachine class is the event
that gets dispatched to the state machine thread. Each time the event is run,
the state machine must cycle the state machine once, and then return.

The state machine has the following states:

DECODING_METADATA
  The media headers are being loaded, and things like framerate, etc are
  being determined, and the first frame of audio/video data is being decoded.
DECODING
  The decode has started. If the PlayState is PLAYING, the decode thread
  should be alive and decoding video and audio frame, the audio thread
  should be playing audio, and the state machine should run periodically
  to update the video frames being displayed.
SEEKING
  A seek operation is in progress. The decode thread should be seeking.
BUFFERING
  Decoding is paused while data is buffered for smooth playback. If playback
  is paused (PlayState transitions to PAUSED) we'll destory the decode thread.
COMPLETED
  The resource has completed decoding, but possibly not finished playback.
  The decode thread will be destroyed. Once playback finished, the audio
  thread will also be destroyed.
SHUTDOWN
  The decoder object and its state machine are about to be destroyed.
  Once the last state machine has been destroyed, the shared state machine
  thread will also be destroyed. It will be recreated later if needed.

The following result in state transitions.

Shutdown()
  Clean up any resources the nsBuiltinDecoderStateMachine owns.
Play()
  Start decoding and playback of media data.
Buffer
  This is not user initiated. It occurs when the
  available data in the stream drops below a certain point.
Complete
  This is not user initiated. It occurs when the
  stream is completely decoded.
Seek(double)
  Seek to the time position given in the resource.

A state transition diagram:

DECODING_METADATA
  |      |
  v      | Shutdown()
  |      |
  v      -->-------------------->--------------------------|
  |---------------->----->------------------------|        v
DECODING             |          |  |              |        |
  ^                  v Seek(t)  |  |              |        |
  |         Play()   |          v  |              |        |
  ^-----------<----SEEKING      |  v Complete     v        v
  |                  |          |  |              |        |
  |                  |          |  COMPLETED    SHUTDOWN-<-|
  ^                  ^          |  |Shutdown()    |
  |                  |          |  >-------->-----^
  |          Play()  |Seek(t)   |Buffer()         |
  -----------<--------<-------BUFFERING           |
                                |                 ^
                                v Shutdown()      |
                                |                 |
                                ------------>-----|

The following represents the states that the nsBuiltinDecoder object
can be in, and the valid states the nsBuiltinDecoderStateMachine can be in at that
time:

player LOADING   decoder DECODING_METADATA
player PLAYING   decoder DECODING, BUFFERING, SEEKING, COMPLETED
player PAUSED    decoder DECODING, BUFFERING, SEEKING, COMPLETED
player SEEKING   decoder SEEKING
player COMPLETED decoder SHUTDOWN
player SHUTDOWN  decoder SHUTDOWN

The general sequence of events is:

1) The video element calls Load on nsMediaDecoder. This creates the
   state machine and starts the channel for downloading the
   file. It instantiates and schedules the nsBuiltinDecoderStateMachine. The
   high level LOADING state is entered, which results in the decode
   thread being created and starting to decode metadata. These are
   the headers that give the video size, framerate, etc. Load() returns
   immediately to the calling video element.

2) When the metadata has been loaded by the decode thread, the state machine
   will call a method on the video element object to inform it that this
   step is done, so it can do the things required by the video specification
   at this stage. The decode thread then continues to decode the first frame
   of data.

3) When the first frame of data has been successfully decoded the state
   machine calls a method on the video element object to inform it that
   this step has been done, once again so it can do the required things
   by the video specification at this stage.

   This results in the high level state changing to PLAYING or PAUSED
   depending on any user action that may have occurred.

   While the play state is PLAYING, the decode thread will decode
   data, and the audio thread will push audio data to the hardware to
   be played. The state machine will run periodically on the shared
   state machine thread to ensure video frames are played at the 
   correct time; i.e. the state machine manages A/V sync.

The Shutdown method on nsBuiltinDecoder closes the download channel, and
signals to the state machine that it should shutdown. The state machine
shuts down asynchronously, and will release the owning reference to the
state machine once its threads are shutdown.

The owning object of a nsBuiltinDecoder object *MUST* call Shutdown when
destroying the nsBuiltinDecoder object.

*/
#if !defined(nsBuiltinDecoder_h_)
#define nsBuiltinDecoder_h_

#include "nsMediaDecoder.h"

#include "nsISupports.h"
#include "nsCOMPtr.h"
#include "nsIThread.h"
#include "nsIChannel.h"
#include "nsIObserver.h"
#include "nsAutoPtr.h"
#include "nsSize.h"
#include "prlog.h"
#include "gfxContext.h"
#include "gfxRect.h"
#include "MediaResource.h"
#include "nsMediaDecoder.h"
#include "nsHTMLMediaElement.h"
#include "mozilla/ReentrantMonitor.h"

namespace mozilla {
namespace layers {
class Image;
} //namespace
} //namespace

typedef mozilla::layers::Image Image;

class nsAudioStream;
class nsBuiltinDecoderStateMachine;

static inline bool IsCurrentThread(nsIThread* aThread) {
  return NS_GetCurrentThread() == aThread;
}

class nsBuiltinDecoder : public nsMediaDecoder
{
public:
  typedef mozilla::MediaChannelStatistics MediaChannelStatistics;
  class DecodedStreamMainThreadListener;

  NS_DECL_ISUPPORTS
  NS_DECL_NSIOBSERVER

  // Enumeration for the valid play states (see mPlayState)
  enum PlayState {
    PLAY_STATE_START,
    PLAY_STATE_LOADING,
    PLAY_STATE_PAUSED,
    PLAY_STATE_PLAYING,
    PLAY_STATE_SEEKING,
    PLAY_STATE_ENDED,
    PLAY_STATE_SHUTDOWN
  };

  nsBuiltinDecoder();
  ~nsBuiltinDecoder();

  virtual bool Init(nsHTMLMediaElement* aElement);

  // This method must be called by the owning object before that
  // object disposes of this decoder object.
  virtual void Shutdown();

  virtual double GetCurrentTime();

  virtual nsresult Load(MediaResource* aResource,
                        nsIStreamListener** aListener,
                        nsMediaDecoder* aCloneDonor);

  // Called in |Load| to open the media resource.
  nsresult OpenResource(MediaResource* aResource,
                        nsIStreamListener** aStreamListener);

  virtual nsBuiltinDecoderStateMachine* CreateStateMachine() = 0;

  // Initialize state machine and schedule it.
  nsresult InitializeStateMachine(nsMediaDecoder* aCloneDonor);

  // Start playback of a video. 'Load' must have previously been
  // called.
  virtual nsresult Play();

  // Seek to the time position in (seconds) from the start of the video.
  virtual nsresult Seek(double aTime);

  virtual nsresult PlaybackRateChanged();

  virtual void Pause();
  virtual void SetVolume(double aVolume);
  virtual void SetAudioCaptured(bool aCaptured);

  // All MediaStream-related data is protected by mReentrantMonitor.
  // We have at most one DecodedStreamData per nsBuiltinDecoder. Its stream
  // is used as the input for each ProcessedMediaStream created by calls to
  // captureStream(UntilEnded). Seeking creates a new source stream, as does
  // replaying after the input as ended. In the latter case, the new source is
  // not connected to streams created by captureStreamUntilEnded.

  struct DecodedStreamData {
    DecodedStreamData(nsBuiltinDecoder* aDecoder,
                      int64_t aInitialTime, SourceMediaStream* aStream);
    ~DecodedStreamData();

    // The following group of fields are protected by the decoder's monitor
    // and can be read or written on any thread.
    int64_t mLastAudioPacketTime; // microseconds
    int64_t mLastAudioPacketEndTime; // microseconds
    // Count of audio frames written to the stream
    int64_t mAudioFramesWritten;
    // Saved value of aInitialTime. Timestamp of the first audio and/or
    // video packet written.
    int64_t mInitialTime; // microseconds
    // mNextVideoTime is the end timestamp for the last packet sent to the stream.
    // Therefore video packets starting at or after this time need to be copied
    // to the output stream.
    int64_t mNextVideoTime; // microseconds
    // The last video image sent to the stream. Useful if we need to replicate
    // the image.
    nsRefPtr<Image> mLastVideoImage;
    gfxIntSize mLastVideoImageDisplaySize;
    // This is set to true when the stream is initialized (audio and
    // video tracks added).
    bool mStreamInitialized;
    bool mHaveSentFinish;
    bool mHaveSentFinishAudio;
    bool mHaveSentFinishVideo;

    // The decoder is responsible for calling Destroy() on this stream.
    // Can be read from any thread.
    const nsRefPtr<SourceMediaStream> mStream;
    // A listener object that receives notifications when mStream's
    // main-thread-visible state changes. Used on the main thread only.
    const nsRefPtr<DecodedStreamMainThreadListener> mMainThreadListener;
    // True when we've explicitly blocked this stream because we're
    // not in PLAY_STATE_PLAYING. Used on the main thread only.
    bool mHaveBlockedForPlayState;
  };
  struct OutputStreamData {
    void Init(ProcessedMediaStream* aStream, bool aFinishWhenEnded)
    {
      mStream = aStream;
      mFinishWhenEnded = aFinishWhenEnded;
    }
    nsRefPtr<ProcessedMediaStream> mStream;
    // mPort connects mDecodedStream->mStream to our mStream.
    nsRefPtr<MediaInputPort> mPort;
    bool mFinishWhenEnded;
  };
  /**
   * Connects mDecodedStream->mStream to aStream->mStream.
   */
  void ConnectDecodedStreamToOutputStream(OutputStreamData* aStream);
  /**
   * Disconnects mDecodedStream->mStream from all outputs and clears
   * mDecodedStream.
   */
  void DestroyDecodedStream();
  /**
   * Recreates mDecodedStream. Call this to create mDecodedStream at first,
   * and when seeking, to ensure a new stream is set up with fresh buffers.
   * aStartTimeUSecs is relative to the state machine's mStartTime.
   */
  void RecreateDecodedStream(int64_t aStartTimeUSecs);
  /**
   * Called when the state of mDecodedStream as visible on the main thread
   * has changed. In particular we want to know when the stream has finished
   * so we can call PlaybackEnded.
   */
  void NotifyDecodedStreamMainThreadStateChanged();
  nsTArray<OutputStreamData>& OutputStreams()
  {
    GetReentrantMonitor().AssertCurrentThreadIn();
    return mOutputStreams;
  }
  DecodedStreamData* GetDecodedStream()
  {
    GetReentrantMonitor().AssertCurrentThreadIn();
    return mDecodedStream;
  }
  class DecodedStreamMainThreadListener : public MainThreadMediaStreamListener {
  public:
    DecodedStreamMainThreadListener(nsBuiltinDecoder* aDecoder)
      : mDecoder(aDecoder) {}
    virtual void NotifyMainThreadStateChanged()
    {
      mDecoder->NotifyDecodedStreamMainThreadStateChanged();
    }
    nsBuiltinDecoder* mDecoder;
  };

  virtual void AddOutputStream(ProcessedMediaStream* aStream, bool aFinishWhenEnded);

  virtual double GetDuration();

  virtual void SetInfinite(bool aInfinite);
  virtual bool IsInfinite();

  virtual MediaResource* GetResource() { return mResource; }
  virtual already_AddRefed<nsIPrincipal> GetCurrentPrincipal();

  virtual void NotifySuspendedStatusChanged();
  virtual void NotifyBytesDownloaded();
  virtual void NotifyDownloadEnded(nsresult aStatus);
  virtual void NotifyPrincipalChanged();
  // Called by the decode thread to keep track of the number of bytes read
  // from the resource.
  void NotifyBytesConsumed(int64_t aBytes);

  // Called when the video file has completed downloading.
  // Call on the main thread only.
  void ResourceLoaded();

  // Called if the media file encounters a network error.
  // Call on the main thread only.
  virtual void NetworkError();

  // Return true if we are currently seeking in the media resource.
  // Call on the main thread only.
  virtual bool IsSeeking() const;

  // Return true if the decoder has reached the end of playback.
  // Call on the main thread only.
  virtual bool IsEnded() const;

  // Set the duration of the media resource in units of seconds.
  // This is called via a channel listener if it can pick up the duration
  // from a content header. Must be called from the main thread only.
  virtual void SetDuration(double aDuration);

  // Set a flag indicating whether seeking is supported
  virtual void SetSeekable(bool aSeekable);

  // Return true if seeking is supported.
  virtual bool IsSeekable();

  virtual nsresult GetSeekable(nsTimeRanges* aSeekable);

  // Set the end time of the media resource. When playback reaches
  // this point the media pauses. aTime is in seconds.
  virtual void SetEndTime(double aTime);

  virtual Statistics GetStatistics();

  // Suspend any media downloads that are in progress. Called by the
  // media element when it is sent to the bfcache. Call on the main
  // thread only.
  virtual void Suspend();

  // Resume any media downloads that have been suspended. Called by the
  // media element when it is restored from the bfcache. Call on the
  // main thread only.
  virtual void Resume(bool aForceBuffering);

  // Tells our MediaResource to put all loads in the background.
  virtual void MoveLoadsToBackground();

  void AudioAvailable(float* aFrameBuffer, uint32_t aFrameBufferLength, float aTime);

  // Called by the state machine to notify the decoder that the duration
  // has changed.
  void DurationChanged();

  virtual bool OnStateMachineThread() const;

  virtual bool OnDecodeThread() const;

  // Returns the monitor for other threads to synchronise access to
  // state.
  virtual ReentrantMonitor& GetReentrantMonitor();

  // Constructs the time ranges representing what segments of the media
  // are buffered and playable.
  virtual nsresult GetBuffered(nsTimeRanges* aBuffered);

  virtual int64_t VideoQueueMemoryInUse();

  virtual int64_t AudioQueueMemoryInUse();

  virtual void NotifyDataArrived(const char* aBuffer, uint32_t aLength, int64_t aOffset);

  // Sets the length of the framebuffer used in MozAudioAvailable events.
  // The new size must be between 512 and 16384.
  virtual nsresult RequestFrameBufferLength(uint32_t aLength);

  // Return the current state. Can be called on any thread. If called from
  // a non-main thread, the decoder monitor must be held.
  PlayState GetState() {
    return mPlayState;
  }

  // Stop updating the bytes downloaded for progress notifications. Called
  // when seeking to prevent wild changes to the progress notification.
  // Must be called with the decoder monitor held.
  void StopProgressUpdates();

  // Allow updating the bytes downloaded for progress notifications. Must
  // be called with the decoder monitor held.
  void StartProgressUpdates();

  // Something has changed that could affect the computed playback rate,
  // so recompute it. The monitor must be held.
  void UpdatePlaybackRate();

  // The actual playback rate computation. The monitor must be held.
  double ComputePlaybackRate(bool* aReliable);

  // Make the decoder state machine update the playback position. Called by
  // the reader on the decoder thread (Assertions for this checked by
  // mDecoderStateMachine). This must be called with the decode monitor
  // held.
  void UpdatePlaybackPosition(int64_t aTime);
  /******
   * The following methods must only be called on the main
   * thread.
   ******/

  // Change to a new play state. This updates the mState variable and
  // notifies any thread blocking on this object's monitor of the
  // change. Call on the main thread only.
  void ChangeState(PlayState aState);

  // Called when the metadata from the media file has been read by the reader.
  // Call on the decode thread only.
  virtual void OnReadMetadataCompleted() { }

  // Called when the metadata from the media file has been loaded by the
  // state machine. Call on the main thread only.
  void MetadataLoaded(uint32_t aChannels,
                      uint32_t aRate,
                      bool aHasAudio,
                      const MetadataTags* aTags);

  // Called when the first frame has been loaded.
  // Call on the main thread only.
  void FirstFrameLoaded();

  // Called when the video has completed playing.
  // Call on the main thread only.
  void PlaybackEnded();

  // Seeking has stopped. Inform the element on the main
  // thread.
  void SeekingStopped();

  // Seeking has stopped at the end of the resource. Inform the element on the main
  // thread.
  void SeekingStoppedAtEnd();

  // Seeking has started. Inform the element on the main
  // thread.
  void SeekingStarted();

  // Called when the backend has changed the current playback
  // position. It dispatches a timeupdate event and invalidates the frame.
  // This must be called on the main thread only.
  void PlaybackPositionChanged();

  // Calls mElement->UpdateReadyStateForData, telling it which state we have
  // entered.  Main thread only.
  void NextFrameUnavailableBuffering();
  void NextFrameAvailable();
  void NextFrameUnavailable();

  // Calls mElement->UpdateReadyStateForData, telling it whether we have
  // data for the next frame and if we're buffering. Main thread only.
  void UpdateReadyStateForData();

  // Find the end of the cached data starting at the current decoder
  // position.
  int64_t GetDownloadPosition();

  // Updates the approximate byte offset which playback has reached. This is
  // used to calculate the readyState transitions.
  void UpdatePlaybackOffset(int64_t aOffset);

  // Provide access to the state machine object
  nsBuiltinDecoderStateMachine* GetStateMachine();

  // Drop reference to state machine.  Only called during shutdown dance.
  virtual void ReleaseStateMachine();

   // Called when a "MozAudioAvailable" event listener is added to the media
   // element. Called on the main thread.
   virtual void NotifyAudioAvailableListener();

  // Notifies the element that decoding has failed.
  virtual void DecodeError();

  // Schedules the state machine to run one cycle on the shared state
  // machine thread. Main thread only.
  nsresult ScheduleStateMachineThread();

  /******
   * The following members should be accessed with the decoder lock held.
   ******/

  // Current decoding position in the stream. This is where the decoder
  // is up to consuming the stream. This is not adjusted during decoder
  // seek operations, but it's updated at the end when we start playing
  // back again.
  int64_t mDecoderPosition;
  // Current playback position in the stream. This is (approximately)
  // where we're up to playing back the stream. This is not adjusted
  // during decoder seek operations, but it's updated at the end when we
  // start playing back again.
  int64_t mPlaybackPosition;
  // Data needed to estimate playback data rate. The timeline used for
  // this estimate is "decode time" (where the "current time" is the
  // time of the last decoded video frame).
  MediaChannelStatistics mPlaybackStatistics;

  // The current playback position of the media resource in units of
  // seconds. This is updated approximately at the framerate of the
  // video (if it is a video) or the callback period of the audio.
  // It is read and written from the main thread only.
  double mCurrentTime;

  // Volume that playback should start at.  0.0 = muted. 1.0 = full
  // volume.  Readable/Writeable from the main thread.
  double mInitialVolume;

  // Position to seek to when the seek notification is received by the
  // decode thread. Written by the main thread and read via the
  // decode thread. Synchronised using mReentrantMonitor. If the
  // value is negative then no seek has been requested. When a seek is
  // started this is reset to negative.
  double mRequestedSeekTime;

  // Duration of the media resource. Set to -1 if unknown.
  // Set when the metadata is loaded. Accessed on the main thread
  // only.
  int64_t mDuration;

  // True when playback should start with audio captured (not playing).
  bool mInitialAudioCaptured;

  // True if the media resource is seekable (server supports byte range
  // requests).
  bool mSeekable;

  /******
   * The following member variables can be accessed from any thread.
   ******/

  // The state machine object for handling the decoding. It is safe to
  // call methods of this object from other threads. Its internal data
  // is synchronised on a monitor. The lifetime of this object is
  // after mPlayState is LOADING and before mPlayState is SHUTDOWN. It
  // is safe to access it during this period.
  nsCOMPtr<nsBuiltinDecoderStateMachine> mDecoderStateMachine;

  // Media data resource.
  nsAutoPtr<MediaResource> mResource;

  // |ReentrantMonitor| for detecting when the video play state changes. A call
  // to |Wait| on this monitor will block the thread until the next state
  // change.
  // Using a wrapper class to restrict direct access to the |ReentrantMonitor|
  // object. Subclasses may override |nsBuiltinDecoder|::|GetReentrantMonitor|
  // e.g. |nsDASHRepDecoder|::|GetReentrantMonitor| returns the monitor in the
  // main |nsDASHDecoder| object. In this case, directly accessing the
  // member variable mReentrantMonitor in |nsDASHRepDecoder| is wrong.
  // The wrapper |RestrictedAccessMonitor| restricts use to the getter
  // function rather than the object itself.
private:
  class RestrictedAccessMonitor
  {
  public:
    RestrictedAccessMonitor(const char* aName) :
      mReentrantMonitor(aName)
    {
      MOZ_COUNT_CTOR(RestrictedAccessMonitor);
    }
    ~RestrictedAccessMonitor()
    {
      MOZ_COUNT_DTOR(RestrictedAccessMonitor);
    }

    // Returns a ref to the reentrant monitor
    ReentrantMonitor& GetReentrantMonitor() {
      return mReentrantMonitor;
    }
  private:
    ReentrantMonitor mReentrantMonitor;
  };

  // The |RestrictedAccessMonitor| member object.
  RestrictedAccessMonitor mReentrantMonitor;

public:
  // Data about MediaStreams that are being fed by this decoder.
  nsTArray<OutputStreamData> mOutputStreams;
  // The SourceMediaStream we are using to feed the mOutputStreams. This stream
  // is never exposed outside the decoder.
  // Only written on the main thread while holding the monitor. Therefore it
  // can be read on any thread while holding the monitor, or on the main thread
  // without holding the monitor.
  nsAutoPtr<DecodedStreamData> mDecodedStream;

  // Set to one of the valid play states.
  // This can only be changed on the main thread while holding the decoder
  // monitor. Thus, it can be safely read while holding the decoder monitor
  // OR on the main thread.
  // Any change to the state on the main thread must call NotifyAll on the
  // monitor so the decode thread can wake up.
  PlayState mPlayState;

  // The state to change to after a seek or load operation.
  // This can only be changed on the main thread while holding the decoder
  // monitor. Thus, it can be safely read while holding the decoder monitor
  // OR on the main thread.
  // Any change to the state must call NotifyAll on the monitor.
  // This can only be PLAY_STATE_PAUSED or PLAY_STATE_PLAYING.
  PlayState mNextState;

  // True when we have fully loaded the resource and reported that
  // to the element (i.e. reached NETWORK_LOADED state).
  // Accessed on the main thread only.
  bool mResourceLoaded;

  // True when seeking or otherwise moving the play position around in
  // such a manner that progress event data is inaccurate. This is set
  // during seek and duration operations to prevent the progress indicator
  // from jumping around. Read/Write from any thread. Must have decode monitor
  // locked before accessing.
  bool mIgnoreProgressData;

  // True if the stream is infinite (e.g. a webradio).
  bool mInfiniteStream;

  // True if NotifyDecodedStreamMainThreadStateChanged should retrigger
  // PlaybackEnded when mDecodedStream->mStream finishes.
  bool mTriggerPlaybackEndedWhenSourceStreamFinishes;
};

#endif
