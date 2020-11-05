#pragma once
#include <Windows.h>
#include <chrono>

#include "LogUtil.hpp"
using namespace LogUtils;

namespace CapUtils {

    // Default to 30 frames per second;
    constexpr auto DEFAULT_MEDIA_FPS = 30;

    /*
    * enum class to set calback type for the timed media grabbing session. This needs to be set before starting a session
    * Multimedia timer type will instatiate windows multimedia timer to fire callbacks at precise intervals
    * Sleep based callback will default to system sleep in milliseconds based on the frequency
    */
    enum class MediaCallbackType {
        UNKNOWN_CALLBACK = 0,
        MULTIMEDIA_TIMER = 1,
        SYSTEM_SLEEP = 2
    };

    /*
    * Templated multimedia timer class to handle  media or screen grabbing in a periodic manner.
    * Custom callback is passed as input parameter to the class object for periodic execution.
    * This calss can be configured to fire updates either through multimedia based timer or
    * through system sleep.
    */
    template<typename Callback>
    class TimedMediaGrabber {

    public:
        /**
         * TimedMediaGrabber implementation constructor. Class instance is instantiated by top level media capture thread
         *
         * @param fps
         *     Frames per second is passed to determine the frequency of callback.
         *
         * @param callback
         *     Custom callback to be fired by the multimedia timer through static function callbackRoutine.
         *
         * @param timeDuration
         *     Duration in seconds for media grabbing. If set to zero, grabbing will continue for ever.
         *
         * @param grabFactor
         *     Optional grab factor applied to frames per second to increase capture rate to test the quality of output.
         *
         */
        TimedMediaGrabber(int fps, Callback&& callback, int timeDuration = 0, int grabFactor = 1) :
            timerCallback(std::move(callback)),
            mediaCaptureDurationInSeconds(timeDuration) {
            int factoredFps = grabFactor * fps;
            factoredFps = (factoredFps > 0 && factoredFps <= 1000) ? factoredFps : DEFAULT_MEDIA_FPS;
            frequency = 1000 / factoredFps;
        }

        /**
         * Cleanup of multimedia timer upon scope exit
         */
        ~TimedMediaGrabber() {
            deleteTimer();
        }

        /**
         * Get the frequency of timed callbacks in milliseconds
         */
        int getFrequency() const {
            return frequency;
        }

        /*
        * @name Copy and move
        *
        * No copying and moving allowed.
        */
        TimedMediaGrabber(const ALogger&) = delete;
        TimedMediaGrabber& operator=(const TimedMediaGrabber&) = delete;

        TimedMediaGrabber(TimedMediaGrabber&&) = delete;
        TimedMediaGrabber& operator=(TimedMediaGrabber&&) = delete;

        /**
         * Setup multimedia timer and start periodic timer calls
         *
         * @return  True if multimedia timer is setup properly.
         */
        bool start() {
            // Use an event object to track the TimerRoutine execution
            doneEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
            if (doneEvent == nullptr)
            {
                ALOG(LogLevel::ERR, "CreateEvent failed!", NVV(errorCode, GetLastError()));
                return false;
            }

            startTimePoint = std::chrono::steady_clock::now();

            if (mediaCallbackType == MediaCallbackType::MULTIMEDIA_TIMER) {
                return setupMultimediaTimer();
            }
            else if (mediaCallbackType == MediaCallbackType::SYSTEM_SLEEP) {
                timerRunState = true;
                callbackHandlerThread = std::thread{ &runSleepBasedCallback, this };

                if (callbackHandlerThread.joinable()) {
                    callbackHandlerThread.detach();
                }
            }

            return true;
        }

        /**
         * Signal a client that is waiting on this handle for timer completion
         *
         * @return  Handle to the timer event.
         */
        const HANDLE& getEventHandle() const {
            return doneEvent;
        }

        /**
         * Set type of callback scheme to be used when firing periodic
         * Type can be either windows multimedia timer or system sleep
         *
         * @param  callbackType
         *     Callback type as an enum to be set for media grabber
         */
        void setMediaCallbackType(MediaCallbackType callbackType) {
            mediaCallbackType = callbackType;
        }

        /**
         * Get the type of timer callback in string format to be used for logging purposes.
         *
         * @return  Type of callback in string format.
         */
        const std::string getMediaCallbackType() const {
            std::string result = "";
            if (mediaCallbackType == MediaCallbackType::MULTIMEDIA_TIMER) {
                result = "MULTIMEDIA_TIMER";
            }
            else if (mediaCallbackType == MediaCallbackType::SYSTEM_SLEEP) {
                result = "SYSTEM_SLEEP";
            }
            return result;
        }

    private:

        /*
        * Callback routine fired by the multimedia timer in a periodic way based on input configuration
        *
        * @param param
        *     TimedMediaGrabber instance passed as param when multimedia timer is created
        *
        * @param timerOrWait
        *     Internally set by the multimedia timer
        *
        */
        static void _stdcall callbackRoutine(void* param, BOOLEAN timerOrWait) {
            auto callbackObj = static_cast<TimedMediaGrabber<Callback>*>(param);
            bool result = callbackObj->timerCallback();

            if (!result) {
                if (callbackObj->mediaCallbackType == MediaCallbackType::MULTIMEDIA_TIMER) {
                    callbackObj->deleteTimer();
                }
                callbackObj->timerRunState = false;
                SetEvent(callbackObj->doneEvent);
            }
            else if (callbackObj->mediaCaptureDurationInSeconds > 0) {
                auto timeDifference = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - callbackObj->startTimePoint).count();

                if (timeDifference >= (callbackObj->mediaCaptureDurationInSeconds * 1000)) {
                    if (callbackObj->mediaCallbackType == MediaCallbackType::MULTIMEDIA_TIMER) {
                        callbackObj->deleteTimer();
                    }
                    callbackObj->timerRunState = false;
                    SetEvent(callbackObj->doneEvent);
                }
            }
        }

        /*
        * Callback routine fired by "start" function to start executing periodic callbacks through system sleep mechanism
        *
        * @param param
        *     TimedMediaGrabber instance passed as param to this dedicated
        *
        */
        static void _stdcall runSleepBasedCallback(void* param) {
            const auto callbackObj = static_cast<TimedMediaGrabber<Callback>*>(param);
            while (callbackObj->timerRunState) {
                std::this_thread::sleep_for(std::chrono::milliseconds(callbackObj->getFrequency()));
                callbackRoutine(param, 1);
            }
        }

        /*
        * Helper function to setup multimedia timer from the OS to start executing periodic callbacks through the timer
        */
        bool setupMultimediaTimer() {
            timerQueue = CreateTimerQueue();
            if (timerQueue == nullptr)
            {
                ALOG(LogLevel::ERR, "CreateTimerQueue failed!", NVV(errorCode, GetLastError()));
                return false;
            }

            if (!CreateTimerQueueTimer(&newTimer, timerQueue, callbackRoutine,
                this, 0, frequency, 0))
            {
                ALOG(LogLevel::ERR, "CreateTimerQueueTimer failed!", NVV(errorCode, GetLastError()));
                return false;
            }

            return true;
        }

        /*
        * Helper function to delete the multimedia timer based on application state
        */
        void deleteTimer() {
            if (newTimer != nullptr) {
                if (!DeleteTimerQueueTimer(timerQueue, newTimer, NULL)) {
                    DWORD errorCode = GetLastError();

                    // Do not flag ERROR_IO_PENDING as error as we get this error while we are running the timer.
                    if (errorCode != ERROR_IO_PENDING) {
                        ALOG(LogLevel::ERR, "DeleteTimerQueue failed!", NV(errorCode));
                    }
                }
                else {
                    ALOG(LogLevel::TRACE, "Timer deleted");
                }
                newTimer = NULL;
                timerQueue = NULL;
            }
        }

        int frequency = 1000 / DEFAULT_MEDIA_FPS; // Frequency of callback computed based on fps passed as input parameter
        HANDLE timerQueue = nullptr; // A pointer to a buffer that receives a handle to the timer-queue timer on return.
        HANDLE newTimer = nullptr; // A handle to the timer queue . Handle is returned by the CreateTimerQueue function.
        Callback timerCallback; // Custom callback lambda from the client
        int mediaCaptureDurationInSeconds = 0; // How long we want to run the media grabber timer. Default is zero
                                               // That is we want to run until it is signalled for stop
        std::chrono::steady_clock::time_point startTimePoint; // Store initial time point for the media grabber
        HANDLE doneEvent = nullptr; // Event handle to signal after sliced media grabbing session is over
        MediaCallbackType mediaCallbackType = MediaCallbackType::MULTIMEDIA_TIMER; // Default callback type for 
                                                                                   // media grabbing session
        std::thread callbackHandlerThread; // Secondary thread to be used to fire callbacks using system sleep
        std::atomic<bool> timerRunState = false; // Atomic state flag to denote recording transition states
    };
} // End of CapUtils namespace
