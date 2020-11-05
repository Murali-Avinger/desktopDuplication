#pragma once

#include "ScreenCapture.hpp"
#include "LogUtil.hpp"

#include <iostream>
#include <atomic>
#include <thread>
#include <future>
#include <deque>

#include <opencv2/opencv.hpp>
#include <libavcodec/d3d11va.h>
#include <libavutil/hwcontext_d3d11va.h>

extern "C"
{
    #include <libavformat/avformat.h>
    #include <libavutil/channel_layout.h>
    #include <libavutil/mathematics.h>
    #include <libavutil/opt.h>
    #include <libswscale/swscale.h>
    #include <libavutil/error.h>
    #include <libavutil/mem.h>
    #include <libavutil/avutil.h>
    #include <libavutil/imgutils.h>
    #include <libavutil/hwcontext_internal.h>

    //extern void avpriv_set_pts_info(AVStream* s, int pts_wrap_bits, unsigned int pts_num, unsigned int pts_den);
    extern int64_t av_gettime(void);
    extern const HWContextType ff_hwcontext_type_d3d11va;
}

namespace CapUtils {

    AVD3D11VAContext* av_d3d11va_alloc_context2(void);

    /*
    * Datastructure to hold collection of FFMPEG session parameters that are used to generate segmented transport streams.
    */
    struct FFScreenSessionInfo
    {
        AVOutputFormat* oformat = nullptr;
        AVFormatContext* ofctx = nullptr;
        AVStream* outVideoStream = nullptr;
        AVFrame* softwareVideoFrame = nullptr;
        AVFrame* hardwareOutputVideoFrame = nullptr;

        AVCodec* codec = nullptr;
        AVCodecContext* outputAVCodecContext = nullptr;
        AVD3D11VAContext* inputAVCodecContext = nullptr;
        SwsContext* swsCtx = nullptr;
        AVDictionary* avDict = nullptr;
        AVBufferRef* hardwareEncodeDeviceContext = nullptr;
        AVBufferRef* hardwareOutputFramesRef = nullptr;

        int64_t prev_pts = 0;
        int64_t time_counter = 0;
        int64_t frameCounter = 0;
        int fps = 30;
        int crf = 23;
        int outputBitrateInMB = 0;

        virtual ~FFScreenSessionInfo() {
            closeRecording();
            freeSessionInfo();
        }

    private:
        /*
        * Internal helper function to free resources of FFMPEG screen session parameters
        */
        void freeSessionInfo();

        /*
        * Internal helper function to close recording of screen capture session upon responding to stop command
        */
        void closeRecording();
    };

    enum class GPUContextType {
        UNKNOWN_GPU_CONTEXT = 0,
        INTEL_GPU_CONTEXT   = 1,
        NVIDIA_GPU_CONTEXT  = 2
    };

    /*
    * Datastructure to hold screen coordinate positions for screen capture. 
    * Members are used by FFMPEG session to generate segmented transport strems.
    */
    struct ScreenCaptureParams {
        int resoutionWidth = 3240;
        int resoutionHeight = 2160;
        int topLeftX1 = 0;
        int topLeftY1 = 0;
        int bottomRightX2 = 0;
        int bottomRightY2 = 0;
    };

    /*
    * Datastructure to hold handles and device context related to GDI for screen capture.
    */
    struct ScreenGDIInfoForCapture {
        HWND hwndDesktop = nullptr;
        HDC hwindowDC = nullptr;
        HDC hwindowCompatibleDC = nullptr;
        HBITMAP hbwindow = nullptr;
        BITMAPINFOHEADER  bi;
    };

    /*
    * Datastructure to hold different state values for screen recording.
    */
    enum class ScreenRecordingState {
        ScreenRecordingNotStarted, // Uninitailized state for recording
        ScreenRecordingStarted, // We received StartRec command to start the recording
        ScreenRecordingAboutToStop, // We received StopRec coommand, but we have to do extra recording to recover lost buffers with FFMPEG
        ScreenRecordingTerminated // We finally terminate FFMPEG session
    };

    constexpr auto CUDA_ENCODER = "h264_nvenc";

    constexpr int kExtraCaptureDuration = 2; // Extra time duration of screen capture to continue upon receiving StopRec command

    /*
    * Screen capture implementation class to grab screen region from desktop and store it as a continuous 
    * segmented transport stream through FFMPEG session.
    */
    class ScreenCapture::Impl {

    public:

        /**
         * ScreenCapture implementation constructor. Class instance is instantiated by top level ScreenCapture class
         *
         * @param configFileName
         *     Configuration JSON file that defines screen capture parameters to be used by FFMPEG session.
         *
         */
        explicit Impl(std::string configFileName);
        
        virtual ~Impl();

        /*
        * @name Copy and move
        *
        * No copying and moving allowed. These objects are always held by a smart pointer
        */
        Impl(const Impl&) = delete;
        Impl& operator=(const Impl&) = delete;

        Impl(Impl&&) = delete;
        Impl& operator=(Impl&&) = delete;

        /**
         * Initialize capture module. Validates input configuration JSON file
         *
         * @param outFilePath
         *          Output file path where segmented files should be placed.
         *
         * @param commandFile
         *          Command file name to trigger start/stop of screen capture session.
         *
         * @param keepAliveFrequency
         *          Keepalive frequency in seconds to check to see if L300 is responsive in terms of updating
         *          command file periodically. If last modified time of command file exceeds keepAliveFrequency time
         *          then, we need to terminate capture session assuming L300 had crashed due to whatever reasons.
         *          if keepAliveFrequency is set to "Zero", then we need to perform this check and continue
         *          to capture until we receive StopRec through command file
         *
         * @return  True if capture session can be initialized after successfully parsing configuration JSON.
         */
        bool init(std::string outFilePath, std::string commandFile, int keepAliveFrequency);
        
        /**
         * Start capture module. This interface is called only after successful init
         * This method blocks until screen capture is terminated by Stop command
         *
         * @return  True if capture session can be started after successfully parsing configuration JSON.
         */
        bool start();

        /*
        * Stop capture module. Should stop any screen capture session that was already started
        */
        void stop();

        /*
        * Check if we screen capture session is still running
         *
         * @return  True if capture session is still running.
        */
        bool isCaptureSessionRunning() const {
            return (recordingState != ScreenRecordingState::ScreenRecordingTerminated);
        }

        void setupFFMPEGBasedScreenEncode(int width, int height, int fps, int segmentDurationInSeconds,
                                            std::string outDirPath, std::string masterPlaylistFile);

        int getFPS() const {
            return ffScreenSessionInfo.fps;
        }

    private:

        /**
         * Internal helper function to parse config JSON file to obtain screen session parameters
         *
         * @return  True if configuration JSON is successfully parsed.
         */
        bool parseConfigFile();

        /**
         * Internal helper function to grab desktop screen pixels. Called by addFrame method
         *
         * @return  cv::Mat
         *      dense numerical channel array representing pixels
         */
        cv::Mat windowAsMatrix();

        /**
         * Add captured screen frame data to a segmented video transport stream through FFMPEG session
         *
         * @param data
         *     Screen pixel data captured from desktop.
         */
        void addFrame(uint8_t* data);

        /**
         * Start command processing thread to respond to start/stop of screen capture session
         *
         * @param pr
         *     promise object to signal caller thread whether or not to start/stop screen capture session.
         */
        void startCommandProcessing(std::promise<bool>&& pr);

        /**
         * Main screen capture thread function that grabs desktop screen pixels and writes it 
         * to video transport stream. This continues until main thread issues a stop command
         */
        void startScreenRecording();

        /**
         * Internal helper function to initialize FFMPEG session based on screen parameters 
         *
         * @return  bool
         *      Return True if FFScreenSessionInfo is populated correctly
         */
        bool setupFFSessionInfo();

        /**
         * Internal helper function to hardware context for CUDA based encoding for screen capture
         *
         * @return  int
         *      Return 0 upon successful setting of hardware context; otherwise return error code
         */
        int setHardwareFrameContext();

        /**
         * Dedicated thread function that consumes data from Screen frame buffer queue and
         * generates segmented mp4 video files along with a master playlist file
         */
        void produceSegmentedVideosFromScreenCapture();

        std::string configFile;  // Config JSON file that defines screen capture parameters
        std::string playListFileName; // Playlist file to be written for playback
        std::string commandFileName; // Command file to execute start/stop screen video recording
        std::string outputFilePath; // Output file path where segmented tarnsport streams should go
        int keepaliveFrequencyInSeconds = 0; // Keepalive frequency to contro the capture session

        int segmentDuration = 10; // Video segment duration that each transport stream should correspond to
        std::atomic<ScreenRecordingState> recordingState; // Atomic state flag to denote recording transition states
        int srcheight = 0; // Source screen region height
        int srcwidth = 0;  // Source screen region width

        FFScreenSessionInfo ffScreenSessionInfo; // FMMPEG session info object to be used to output segmented streams.
        ScreenCaptureParams screenCaptureParams; // Structure to hold various Screen capture coordinates and resolution.
        ScreenGDIInfoForCapture screenGDIInfoForCapture; // Structure to hold GDI related handles and device contexts.
        std::deque<cv::Mat> screenDataList; // Screen frame buffer queue. This is guarded by a mutex
        std::mutex recordMutex; // Mutex to guard Screen frame buffer queue
    };
}
