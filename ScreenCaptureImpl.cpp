
#include "ScreenCaptureImpl.hpp"
#include "FileUtils.h"
#include "../include/rapidjson/document.h"

#include <chrono>
#include <fstream>
#include <ctime>

#include "LogUtil.hpp"
#include "TimedMediaGrabber.hpp"

using namespace FileUtils;
using namespace LogUtils;

namespace CapUtils {

    AVD3D11VAContext* av_d3d11va_alloc_context2() {
        AVD3D11VAContext* res = (AVD3D11VAContext*)av_mallocz(sizeof(AVD3D11VAContext));
        if (!res) {
            return NULL;
        }
        res->context_mutex = INVALID_HANDLE_VALUE;
        return res;
    }

    void FFScreenSessionInfo::freeSessionInfo() {
        if (ofctx) {
            avformat_close_input(&ofctx);
        }
        if (ofctx && !(ofctx->oformat->flags & AVFMT_NOFILE)) {
            avio_closep(&ofctx->pb);
        }
        if (softwareVideoFrame) {
            av_frame_free(&softwareVideoFrame);
        }
        if (hardwareOutputVideoFrame) {
            av_frame_free(&hardwareOutputVideoFrame);
        }
        if (outputAVCodecContext) {
            avcodec_free_context(&outputAVCodecContext);
        }
        if (ofctx) {
            avformat_free_context(ofctx);
        }
        if (swsCtx) {
            sws_freeContext(swsCtx);
        }
        if (hardwareEncodeDeviceContext) {
            av_buffer_unref(&hardwareEncodeDeviceContext);
        }
        if (hardwareOutputFramesRef) {
            av_buffer_unref(&hardwareOutputFramesRef);
        }
    }

    void FFScreenSessionInfo::closeRecording() {
        if (ofctx) {
            av_write_trailer(ofctx);
            if (!(oformat->flags & AVFMT_NOFILE))
            {
                int err = avio_close(ofctx->pb);
                if (err < 0)
                {
                    ALOG(ERR, "Failed to close file", NV(err));
                }
            }
        }
    }

    ScreenCapture::Impl::Impl(std::string configFileName) : configFile(configFileName) {
        // Initialize handles for device contexts used for screen capture
        screenGDIInfoForCapture.hwndDesktop = GetDesktopWindow();
        screenGDIInfoForCapture.hwindowDC = GetDC(screenGDIInfoForCapture.hwndDesktop);
        screenGDIInfoForCapture.hwindowCompatibleDC = CreateCompatibleDC(screenGDIInfoForCapture.hwindowDC);
        SetStretchBltMode(screenGDIInfoForCapture.hwindowCompatibleDC, COLORONCOLOR);
        screenGDIInfoForCapture.bi.biSize = sizeof(BITMAPINFOHEADER);
        screenGDIInfoForCapture.bi.biPlanes = 1;
        screenGDIInfoForCapture.bi.biBitCount = 24;
        screenGDIInfoForCapture.bi.biCompression = BI_RGB;
        screenGDIInfoForCapture.bi.biSizeImage = 0;
        screenGDIInfoForCapture.bi.biXPelsPerMeter = 0;
        screenGDIInfoForCapture.bi.biYPelsPerMeter = 0;
        screenGDIInfoForCapture.bi.biClrUsed = 0;
        screenGDIInfoForCapture.bi.biClrImportant = 0;

        // Initialize recording state
        recordingState = ScreenRecordingState::ScreenRecordingNotStarted;
    }

    ScreenCapture::Impl::~Impl() {
        DeleteDC(screenGDIInfoForCapture.hwindowCompatibleDC);
        ReleaseDC(screenGDIInfoForCapture.hwndDesktop, screenGDIInfoForCapture.hwindowDC);
        DeleteObject(screenGDIInfoForCapture.hbwindow);
    }

    void ScreenCapture::Impl::setupFFMPEGBasedScreenEncode(int width, int height, int fps, 
                                                    int segmentDurationInSeconds, 
                                                    std::string outDirPath,
                                                    std::string masterPlaylistFile) {
        screenCaptureParams.topLeftX1 = 0;
        screenCaptureParams.topLeftY1 = 0;

        screenCaptureParams.bottomRightX2 = width;
        screenCaptureParams.bottomRightY2 = height;

        screenCaptureParams.resoutionWidth = width;
        screenCaptureParams.resoutionHeight = height;

        // Compute source width and height for screen region capture
        srcwidth = screenCaptureParams.bottomRightX2 - screenCaptureParams.topLeftX1;
        srcheight = screenCaptureParams.bottomRightY2 - screenCaptureParams.topLeftY1;

        ffScreenSessionInfo.fps = fps;
        ffScreenSessionInfo.crf = 23;
        ffScreenSessionInfo.outputBitrateInMB = 0;

        segmentDuration = segmentDurationInSeconds;

        outputFilePath = outDirPath;
        playListFileName = masterPlaylistFile;

        setupFFSessionInfo();
    }

    bool ScreenCapture::Impl::parseConfigFile() {
        std::ifstream configFileStream;
        std::string jsonContent = "";

        configFileStream.open(configFile);
        if (configFileStream.is_open())
        {
            std::string lineInfo;
            while (std::getline(configFileStream, lineInfo))
            {
                jsonContent += lineInfo;
            }
        } else {
            ALOG(ERR, "Cannot open config file");
            return false;
        }
        configFileStream.close();

        rapidjson::Document doc;
        doc.Parse(jsonContent.c_str());

        // Validating JSON
        if (doc.HasMember("ScreenRecord"))
        {
            ALOG(TRACE, "Validating JSON file...");
            if (doc["ScreenRecord"].HasMember("ScreenDimensions") &&
                doc["ScreenRecord"].HasMember("Resolution") &&
                doc["ScreenRecord"].HasMember("fps") &&
                doc["ScreenRecord"].HasMember("outputBitrateInMB") &&
                doc["ScreenRecord"].HasMember("Recording")
                )
            {
                ALOG(TRACE, "Has ScreenRecording");
                if (doc["ScreenRecord"]["ScreenDimensions"].HasMember("topX1") &&
                    doc["ScreenRecord"]["ScreenDimensions"].HasMember("topY1") &&
                    doc["ScreenRecord"]["ScreenDimensions"].HasMember("bottomX2") &&
                    doc["ScreenRecord"]["ScreenDimensions"].HasMember("bottomY2") &&
                    doc["ScreenRecord"]["Resolution"].HasMember("resWidth") &&
                    doc["ScreenRecord"]["Resolution"].HasMember("resHeight")
                    )
                {
                    ALOG(TRACE, "Has Dimensions");
                }
                else
                {
                    ALOG(ERR, "Missing ScreenDimensions or Resolution parameter: srcWidth, srcHeight, resWidth, OR resHeight.");
                    return false;
                }

                if (doc["ScreenRecord"]["Recording"].HasMember("segmentDuration") &&
                    doc["ScreenRecord"]["Recording"].HasMember("fileName")
                    )
                {
                    ALOG(TRACE, "Has Recording params");
                }
                else
                {
                    ALOG(ERR, "Missing Recording parameter: isRecord, segmentDuration, recordDurationInSec, fileName OR filePath.");
                    return false;
                }
            }
            else
            {
                ALOG(ERR, "Missing ScreenRecording parameter: ScreenDimensions, Resolution, fps, bitrate, OR Recording.");
                return false;
            }
        }
        else
            return false;

        ALOG(TRACE, "Valid file... Beginning parsing...");

        screenCaptureParams.topLeftX1 = std::atoi(doc["ScreenRecord"]["ScreenDimensions"]["topX1"].GetString());
        screenCaptureParams.topLeftY1 = std::atoi(doc["ScreenRecord"]["ScreenDimensions"]["topY1"].GetString());

        screenCaptureParams.bottomRightX2 = std::atoi(doc["ScreenRecord"]["ScreenDimensions"]["bottomX2"].GetString());
        screenCaptureParams.bottomRightY2 = std::atoi(doc["ScreenRecord"]["ScreenDimensions"]["bottomY2"].GetString());

        screenCaptureParams.resoutionWidth = std::atoi(doc["ScreenRecord"]["Resolution"]["resWidth"].GetString());
        screenCaptureParams.resoutionHeight = std::atoi(doc["ScreenRecord"]["Resolution"]["resHeight"].GetString());

        // Compute source width and height for screen region capture
        srcwidth = screenCaptureParams.bottomRightX2 - screenCaptureParams.topLeftX1;
        srcheight = screenCaptureParams.bottomRightY2 - screenCaptureParams.topLeftY1;

        // create a bitmap
        screenGDIInfoForCapture.hbwindow = CreateCompatibleBitmap(screenGDIInfoForCapture.hwindowDC, screenCaptureParams.resoutionWidth, 
                                                                  screenCaptureParams.resoutionHeight);

        screenGDIInfoForCapture.bi.biWidth = screenCaptureParams.resoutionWidth;
        screenGDIInfoForCapture.bi.biHeight = -screenCaptureParams.resoutionHeight;

        ffScreenSessionInfo.fps = std::atoi(doc["ScreenRecord"]["fps"].GetString());
        ffScreenSessionInfo.crf = std::atoi(doc["ScreenRecord"]["crf"].GetString());
        ffScreenSessionInfo.outputBitrateInMB = std::atoi(doc["ScreenRecord"]["outputBitrateInMB"].GetString());

        // Limit output birate within a range from 1 - 100 MBPS
        ffScreenSessionInfo.outputBitrateInMB = (ffScreenSessionInfo.outputBitrateInMB <= 0 || ffScreenSessionInfo.outputBitrateInMB > 100) ?
                                                 0 : ffScreenSessionInfo.outputBitrateInMB;

        // Validate crf by checking to see if value lies between 0 and 51 supported by FFMPEG
        ffScreenSessionInfo.crf = (ffScreenSessionInfo.crf <= 51 && ffScreenSessionInfo.crf >= 0) ? ffScreenSessionInfo.crf : 23;

        segmentDuration = std::atoi(doc["ScreenRecord"]["Recording"]["segmentDuration"].GetString());

        playListFileName = doc["ScreenRecord"]["Recording"]["fileName"].GetString();

        // Log all screen parameters
        {
            std::string screenParamsToBeLogged = " " + NVV(topLeftX1, screenCaptureParams.topLeftX1) + " " +
                NVV(topLeftY1, screenCaptureParams.topLeftY1) + " " +
                NVV(bottomRightX2, screenCaptureParams.bottomRightX2) + " " +
                NVV(bottomRightY2, screenCaptureParams.bottomRightY2)  + " " + 
                NVV(resoutionWidth, screenCaptureParams.resoutionWidth) + " " +
                NVV(resoutionHeight, screenCaptureParams.resoutionHeight);

            ALOG(INFO, "Screen params:", screenParamsToBeLogged);
        }
        return true;
    }

    int ScreenCapture::Impl::setHardwareFrameContext() {
        AVBufferRef* hardwareFramesRef;
        AVHWFramesContext* framesContext = NULL;
        int err = 0;

        if (!(hardwareFramesRef = av_hwframe_ctx_alloc(ffScreenSessionInfo.hardwareEncodeDeviceContext))) {
            ALOG(ERR, "Failed to create CUDA frame context.");
            return -1;
        }

        framesContext = (AVHWFramesContext*)(hardwareFramesRef->data);
        framesContext->format = AV_PIX_FMT_CUDA;
        framesContext->sw_format = AV_PIX_FMT_YUV420P;
        framesContext->width = screenCaptureParams.resoutionWidth;
        framesContext->height = screenCaptureParams.resoutionHeight;
        framesContext->initial_pool_size = 20;

        if ((err = av_hwframe_ctx_init(hardwareFramesRef)) < 0) {
            ALOG(ERR, "Failed to initialize CUDA frame context.", NV(err));
            av_buffer_unref(&hardwareFramesRef);
            return err;
        }
        ffScreenSessionInfo.outputAVCodecContext->hw_frames_ctx = av_buffer_ref(hardwareFramesRef);
        if (!ffScreenSessionInfo.outputAVCodecContext->hw_frames_ctx)
            err = AVERROR(ENOMEM);

        av_buffer_unref(&hardwareFramesRef);
        return err;
    }

    bool ScreenCapture::Impl::setupFFSessionInfo() {
        // String concat for sequence name, output file name and path
        std::string outputFile = outputFilePath + "\\" + playListFileName;
        std::string seq = "fsequence%d.ts";
        std::string path = outputFilePath + "\\" + seq;

        int err = av_hwdevice_ctx_create(&ffScreenSessionInfo.hardwareEncodeDeviceContext, AV_HWDEVICE_TYPE_CUDA, NULL, NULL, 0);

        if (err < 0) {
            ALOG(ERR, "Failed to initialize CUDA frame context.", NV(err));
            return false;
        }

        if (!(ffScreenSessionInfo.oformat = av_guess_format(NULL, outputFile.c_str(), NULL)))
        {
            ALOG(ERR, "Failed to define output format");
            return false;
        }

        if ((err = avformat_alloc_output_context2(&ffScreenSessionInfo.ofctx, ffScreenSessionInfo.oformat, NULL, outputFile.c_str()) < 0))
        {
            ALOG(ERR, "Failed to allocate output context", NV(err));
            return false;
        }

        if (!(ffScreenSessionInfo.codec = avcodec_find_encoder_by_name(CUDA_ENCODER)))
        {
            ALOG(ERR, "Failed to find encoder");
            return false;
        }

        if (!(ffScreenSessionInfo.outVideoStream = avformat_new_stream(ffScreenSessionInfo.ofctx, ffScreenSessionInfo.codec)))
        {
            ALOG(ERR, "Failed to create new stream");
            return false;
        }

        if (!(ffScreenSessionInfo.outputAVCodecContext = avcodec_alloc_context3(ffScreenSessionInfo.codec)))
        {
            ALOG(ERR, "Failed to allocate codec context");
            return false;
        }

        ffScreenSessionInfo.outputAVCodecContext->width = screenCaptureParams.resoutionWidth;
        ffScreenSessionInfo.outputAVCodecContext->height = screenCaptureParams.resoutionHeight;
        ffScreenSessionInfo.outputAVCodecContext->time_base = { 1, ffScreenSessionInfo.fps };
        ffScreenSessionInfo.outputAVCodecContext->framerate = { ffScreenSessionInfo.fps, 1 };
        ffScreenSessionInfo.outputAVCodecContext->sample_aspect_ratio = { 1, 1 };
        ffScreenSessionInfo.outputAVCodecContext->pix_fmt = AV_PIX_FMT_CUDA;
        ffScreenSessionInfo.outputAVCodecContext->max_b_frames = 0;
        ffScreenSessionInfo.outputAVCodecContext->gop_size = 12;

        ffScreenSessionInfo.outVideoStream->codecpar->codec_id = ffScreenSessionInfo.oformat->video_codec;
        ffScreenSessionInfo.outVideoStream->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
        ffScreenSessionInfo.outVideoStream->codecpar->width = screenCaptureParams.resoutionWidth;
        ffScreenSessionInfo.outVideoStream->codecpar->height = screenCaptureParams.resoutionHeight;
        ffScreenSessionInfo.outVideoStream->codecpar->format = AV_PIX_FMT_CUDA;
        ffScreenSessionInfo.outVideoStream->time_base = { 1, ffScreenSessionInfo.fps };

        if (ffScreenSessionInfo.outputBitrateInMB != 0) {
            ALOG(INFO, "Setting output bitrate to ", NVV(outputBitrateInMB, ffScreenSessionInfo.outputBitrateInMB), " Mbps");
            ffScreenSessionInfo.outVideoStream->codecpar->bit_rate = ffScreenSessionInfo.outputBitrateInMB * 1000 * 1000;
        }

        avcodec_parameters_to_context(ffScreenSessionInfo.outputAVCodecContext, ffScreenSessionInfo.outVideoStream->codecpar);

        // Can change the value of preset to slow, fast or ultrafast
        if (ffScreenSessionInfo.outVideoStream->codecpar->codec_id == AV_CODEC_ID_H264)
        {
            av_opt_set(ffScreenSessionInfo.outputAVCodecContext, "preset", "ultrafast", 0);
            av_opt_set(ffScreenSessionInfo.outputAVCodecContext, "crf", std::to_string(ffScreenSessionInfo.crf).c_str(), AV_OPT_SEARCH_CHILDREN);
        }

        avcodec_parameters_from_context(ffScreenSessionInfo.outVideoStream->codecpar, ffScreenSessionInfo.outputAVCodecContext);

        // Set hardware context for encoder's AVCodecContext
        if ((err = setHardwareFrameContext()) < 0) {
            ALOG(ERR, "Failed to set hardware frame context.");
            return false;
        }

        if ((err = avcodec_open2(ffScreenSessionInfo.outputAVCodecContext, ffScreenSessionInfo.codec, NULL)) < 0)
        {
            ALOG(ERR, "Failed to open codec");
            return false;
        }

        // HLS parameters that define the segment duration, sequence name, start index of filename and playlist type
        av_dict_set(&ffScreenSessionInfo.avDict, "hls_time", std::to_string(segmentDuration).c_str(), 0);
        av_dict_set(&ffScreenSessionInfo.avDict, "hls_segment_filename", path.c_str(), 0);
        av_dict_set(&ffScreenSessionInfo.avDict, "start_number", "1", 0);
        av_dict_set(&ffScreenSessionInfo.avDict, "hls_playlist_type", "event", 0);

        if (!(ffScreenSessionInfo.oformat->flags & AVFMT_NOFILE))
        {
            if ((err = avio_open2(&ffScreenSessionInfo.ofctx->pb, outputFile.c_str(), AVIO_FLAG_WRITE, NULL, &ffScreenSessionInfo.avDict)) < 0)
            {
                ALOG(ERR, "Failed to open file", NV(err));
                return false;
            }
        }

        // Important logic to write the header and wrap up function
        if ((err = avformat_write_header(ffScreenSessionInfo.ofctx, &ffScreenSessionInfo.avDict)) < 0)
        {
            ALOG(ERR, "Failed to write header", NV(err));
            return false;
        }

        av_dump_format(ffScreenSessionInfo.ofctx, 0, outputFile.c_str(), 1);
        ffScreenSessionInfo.time_counter = 0;

        // Setup software videoFrame
        ffScreenSessionInfo.softwareVideoFrame = av_frame_alloc();
        ffScreenSessionInfo.softwareVideoFrame->format = AV_PIX_FMT_YUV420P;
        ffScreenSessionInfo.softwareVideoFrame->width = ffScreenSessionInfo.outputAVCodecContext->width;
        ffScreenSessionInfo.softwareVideoFrame->height = ffScreenSessionInfo.outputAVCodecContext->height;
        ffScreenSessionInfo.outVideoStream->time_base = { 1, ffScreenSessionInfo.fps };

        if ((err = av_frame_get_buffer(ffScreenSessionInfo.softwareVideoFrame, 0)) < 0)
        {
            ALOG(ERR, "Failed to allocate picture", NV(err));
            return false;
        }

        // Setup hardware video frame
        ffScreenSessionInfo.hardwareOutputVideoFrame = av_frame_alloc();
        if ((err = av_hwframe_get_buffer(ffScreenSessionInfo.outputAVCodecContext->hw_frames_ctx, ffScreenSessionInfo.hardwareOutputVideoFrame, 0)) < 0) {
            ALOG(ERR, "Failed to get hardware frame buffer", NV(err));
            return false;
        }
        if (!ffScreenSessionInfo.outputAVCodecContext->hw_frames_ctx) {
            err = AVERROR(ENOMEM);
            return false;
        }

        // Convert from RGB to YUV
        ffScreenSessionInfo.swsCtx = sws_getContext(ffScreenSessionInfo.outputAVCodecContext->width, ffScreenSessionInfo.outputAVCodecContext->height, 
                                                    AV_PIX_FMT_BGR24, ffScreenSessionInfo.outputAVCodecContext->width, 
                                                    ffScreenSessionInfo.outputAVCodecContext->height, AV_PIX_FMT_YUV420P,
                                                    SWS_X, 0, 0, 0);

        // Log all FFMPEG paramters that we use for screen capture encoding
        {
            std::string ffMPEGParamsToBeLogged = " " + NVV(FrameRate, ffScreenSessionInfo.fps) + " " +
                                                        NVV(ConstantRateFactor, ffScreenSessionInfo.crf) + " " +
                                                        NVV(OutputBitrateInMB, ffScreenSessionInfo.outputBitrateInMB) + " " +
                                                        NVV(SegmentDuration, segmentDuration) + " " +
                                                        NVV(PlayListFileName, playListFileName) + " " +
                                                        NVV(Encoder, CUDA_ENCODER);

            ALOG(INFO, "FFMPEG params:", ffMPEGParamsToBeLogged);
        }

        return true;
    }

    cv::Mat ScreenCapture::Impl::windowAsMatrix() {
        cv::Mat src;
        src.create(screenCaptureParams.resoutionHeight, screenCaptureParams.resoutionWidth, CV_8UC(8));

        //http://msdn.microsoft.com/en-us/library/windows/window/dd183402%28v=vs.85%29.aspx
        // use the previously created device context with the bitmap
        SelectObject(screenGDIInfoForCapture.hwindowCompatibleDC, screenGDIInfoForCapture.hbwindow);
        // copy from the window device context to the bitmap device context
        //change SRCCOPY to NOTSRCCOPY for wacky colors !

        StretchBlt(screenGDIInfoForCapture.hwindowCompatibleDC, 0, 0, screenCaptureParams.resoutionWidth, 
                   screenCaptureParams.resoutionHeight, screenGDIInfoForCapture.hwindowDC,
                   screenCaptureParams.topLeftX1, screenCaptureParams.topLeftY1, 
                   srcwidth, srcheight, SRCCOPY);

        //copy from hwindowCompatibleDC to hbwindow
        GetDIBits(screenGDIInfoForCapture.hwindowCompatibleDC, screenGDIInfoForCapture.hbwindow, 0, 
                  screenCaptureParams.resoutionHeight, src.data, 
                  (BITMAPINFO*)&screenGDIInfoForCapture.bi, DIB_RGB_COLORS);

        return src;
    }

    void ScreenCapture::Impl::addFrame(uint8_t* data) {
        int err;

        int inLinesize[1] = { 3 * ffScreenSessionInfo.outputAVCodecContext->width };

        // Scale function to use uint8 data and line it up with frame context
        sws_scale(ffScreenSessionInfo.swsCtx, (const uint8_t* const*)&data, inLinesize, 0,
                  ffScreenSessionInfo.outputAVCodecContext->height, ffScreenSessionInfo.softwareVideoFrame->data,
                  ffScreenSessionInfo.softwareVideoFrame->linesize);

        // Set presentation timestamp for both software and hardware frames
        int64_t currTime = av_gettime();
        const AVRational codecContextTimebase = ffScreenSessionInfo.outputAVCodecContext->time_base;
        int64_t rescaledCurrTime = av_rescale_q(currTime, { 1, 1000000 }, codecContextTimebase);

        ffScreenSessionInfo.softwareVideoFrame->pts = rescaledCurrTime;
        ffScreenSessionInfo.hardwareOutputVideoFrame->pts = rescaledCurrTime;

        if ((err = av_hwframe_transfer_data(ffScreenSessionInfo.hardwareOutputVideoFrame, ffScreenSessionInfo.softwareVideoFrame, 0)) < 0) {
            ALOG(ERR, "Failed to transfer hardware frame buffer", NV(err));
            return;
        }

        if ((err = avcodec_send_frame(ffScreenSessionInfo.outputAVCodecContext, ffScreenSessionInfo.hardwareOutputVideoFrame)) < 0)
        {
            ALOG(ERR, "Failed to send frame", NV(err));
            return;
        }

        AVPacket pkt;
        av_init_packet(&pkt);

        pkt.stream_index = ffScreenSessionInfo.outVideoStream->index;

        if (avcodec_receive_packet(ffScreenSessionInfo.outputAVCodecContext, &pkt) == 0)
        {
            if ((err = av_interleaved_write_frame(ffScreenSessionInfo.ofctx, &pkt)) < 0)
            {
                ALOG(ERR, "Failed to mux packet", NV(err));
                av_packet_unref(&pkt);
                return;
            }
            av_packet_unref(&pkt);
        }
    }

    bool ScreenCapture::Impl::init(std::string outFilePath, std::string commandFile, int keepAliveFrequency) {
        if (!parseConfigFile()) {
            ALOG(ERR, "Failed to parse config file.");
            return false;
        }

        commandFileName = commandFile;
        outputFilePath = outFilePath;
        keepaliveFrequencyInSeconds = keepAliveFrequency;
        ALOG(INFO, NV(keepAliveFrequency));
        return true;
    }

    void ScreenCapture::Impl::produceSegmentedVideosFromScreenCapture() {
        TimedMediaGrabber timedGrabber(ffScreenSessionInfo.fps, [&]() -> bool {
            if (isCaptureSessionRunning() || !screenDataList.empty()) {
                cv::Mat src;
                std::lock_guard<std::mutex> lock(recordMutex);
                if (!screenDataList.empty()) {
                    src = screenDataList.front();
                    screenDataList.pop_front();
                    addFrame(src.data);
                }
                return true;
            }

            return false;
        });

        timedGrabber.start();
        timedGrabber.setMediaCallbackType(MediaCallbackType::SYSTEM_SLEEP);

        if (WaitForSingleObject(timedGrabber.getEventHandle(), INFINITE) != WAIT_OBJECT_0) {
            ALOG(LogLevel::ERR, "WaitForSingleObject failed!", NVV(errorCode, GetLastError()));
        }

        CloseHandle(timedGrabber.getEventHandle());
    }

    void ScreenCapture::Impl::startScreenRecording() {
        ScreenRecordingState state = recordingState;

        const auto screenGrabAndEncodeFrame = [&]() {
            if (recordingState == state) {
                cv::Mat src = windowAsMatrix();
                {
                    std::lock_guard<std::mutex> lock(recordMutex);
                    screenDataList.emplace_back(std::move(src));
                }
                return true;
            }

            return false;
        };

        TimedMediaGrabber timedGrabber(ffScreenSessionInfo.fps, [&]() -> bool {
            return screenGrabAndEncodeFrame();
        });

        //timerGrabber.setMediaCallbackType(MediaCallbackType::SYSTEM_SLEEP);
        timedGrabber.start();

        if (WaitForSingleObject(timedGrabber.getEventHandle(), INFINITE) != WAIT_OBJECT_0) {
            ALOG(LogLevel::ERR, "WaitForSingleObject failed!", NVV(errorCode, GetLastError()));
        }

        CloseHandle(timedGrabber.getEventHandle());

        // FFMPEG seems to hold some of screen frames to its internal buffer. When the session is stopped, we seem to miss 
        // slightly over a second of screen capture data at the end. To circumvent this issue, we continue to capture an extra 
        // duration to get back those lost frames that were held by FFMPEG session before the stop command was issued.
        state = recordingState;
        TimedMediaGrabber timedGrabberExtraDuartion(ffScreenSessionInfo.fps, [&]() -> bool {
            return screenGrabAndEncodeFrame();
        }, kExtraCaptureDuration);

        //timerGrabberExtraDuartion.setMediaCallbackType(MediaCallbackType::SYSTEM_SLEEP);
        timedGrabberExtraDuartion.start();
        if (WaitForSingleObject(timedGrabberExtraDuartion.getEventHandle(), INFINITE) != WAIT_OBJECT_0) {
            ALOG(LogLevel::ERR, "WaitForSingleObject failed!", NVV(errorCode, GetLastError()));
        }

        CloseHandle(timedGrabberExtraDuartion.getEventHandle());
        recordingState = ScreenRecordingState::ScreenRecordingTerminated;
    }

    void ScreenCapture::Impl::startCommandProcessing(std::promise<bool>&& pr) {
        time_t stLocal = 0;
        std::once_flag promiseSet;

        int keepaliveFactor = 3;
        int maxWaitTime = keepaliveFrequencyInSeconds * keepaliveFactor;

        // Helper lambda function to set state for screen recording. Once promise value is set to true, 
        // caller thread can start recording thread and generate segmented mp4 files
        const auto setScreenSessionState = [&](const bool state) {
            std::call_once(promiseSet, [&]() {
                if (state) {
                    ALOG(INFO, "Received StartRec command to start a screen capture session...");
                    recordingState = ScreenRecordingState::ScreenRecordingStarted;
                } else {
                    // We received state as "false". This denotes we need to stop command processing thread
                    // If we had not started screen recording session earlier on, then we must 
                    // signal the app to terminate the screen session
                    if (recordingState == ScreenRecordingState::ScreenRecordingNotStarted) {
                        recordingState = ScreenRecordingState::ScreenRecordingTerminated;
                    }
                }
                pr.set_value(state);
            });
        };

        // Helper lambda to check to see if L300 is still running. If not, terminate capture session
        const auto shouldCaptureSessionContinue = [&](time_t stCurrLocal) {
            std::chrono::system_clock::time_point tp = std::chrono::system_clock::from_time_t(stCurrLocal);
            std::chrono::system_clock::duration modifiedDuration = std::chrono::system_clock::now() - tp;
            auto timeDifference = std::chrono::duration_cast<std::chrono::milliseconds>(modifiedDuration).count();

            if (timeDifference > (maxWaitTime  * 1000)) {
                ALOG(FATAL, "Failed to receive keepalives from L300. Terminating capture session!", NVV(timeDifference, timeDifference / 1000));
                return false;
            }
            return true;
        };

        // Command thread will loop indefinitely until it responds to "StopRec"
        while (true) {
            std::time_t stCurrLocal = getLastWriteTime(commandFileName);
            // Check to see if we should continue based on keepAlive mechanism from L300
            if (maxWaitTime > 0 && !shouldCaptureSessionContinue(stCurrLocal)) {
                recordingState = ScreenRecordingState::ScreenRecordingTerminated;
                break;
            }

            if (stCurrLocal != 0 && stLocal == 0) {
                stLocal = stCurrLocal;
            } else if (stCurrLocal == stLocal) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }

            std::string line;
            std::ifstream commandFile(commandFileName);

            if (commandFile.is_open())
            {
                getline(commandFile, line);
                commandFile.close();

                if (line == "StartRec") {
                    setScreenSessionState(true);
                }
                else if (line == "StopRec") {
                    // We received a command to stop screen recording. Now, we must start extra capture 
                    // session to get back previous capture buffers held by FFMPEG
                    ALOG(INFO, "Received StopRec command to stop recording...");
                    recordingState = ScreenRecordingState::ScreenRecordingAboutToStop;
                    break;
                }
            }
        }

        setScreenSessionState(false);
    }

    bool ScreenCapture::Impl::start() {
        std::promise<bool> pr;
        std::future<bool> fut = pr.get_future();

        std::thread commandThread(&ScreenCapture::Impl::startCommandProcessing, this, std::move(pr));

        if (commandThread.joinable()) {
            commandThread.detach();
        }

        bool recordingSet = fut.get();

        if (recordingSet) {
            if (!setupFFSessionInfo()) {
                return false;
            }

            // Create a consumer thread that processes screen frame and dispatches to FFMPEG to create segmented videos
            std::thread screenThread(&ScreenCapture::Impl::produceSegmentedVideosFromScreenCapture, this);
            // Create a producer thread that grabs screen from GDI and pushes it to a queue for FFMPEG to process
            std::thread recordThread(&ScreenCapture::Impl::startScreenRecording, this);

            recordThread.join();
            screenThread.join();
        }

        return recordingSet;
    }

    void ScreenCapture::Impl::stop() {
        ALOG(INFO, "Stopping a timed capture recording...");
        recordingState = ScreenRecordingState::ScreenRecordingAboutToStop;
    }
}
