#pragma once

namespace CapUtils {

    /*
    * Screen capture interface class that declares basic interfaces to perform screen capture recording
    * Derived class must implement all basic operations defined by this interface class.
    */
    class ScreenCaptureInterface {

    public:
        virtual ~ScreenCaptureInterface() = default;

        /*
        * @name Copy and move
        * 
        * No copying and moving allowed. These objects are always held by a smart pointer
        */
        ScreenCaptureInterface(const ScreenCaptureInterface&) = delete;
        ScreenCaptureInterface& operator=(const ScreenCaptureInterface&) = delete;

        ScreenCaptureInterface(ScreenCaptureInterface&&) = delete;
        ScreenCaptureInterface& operator=(ScreenCaptureInterface&&) = delete;

        /**
         * Initialize capture module.
         *
         * @param outFilePath
         *          Output file path where segmented files should be placed.
         *
         * @param commandFile
         *          Command file name to trigger start/stop of screen capture session.
         *
         * @param keepAliveFrequency
         *          Keepalive frequency time in seconds to check for command file update and determine
         *          whether or not to continue with screen capture session based on L300 availability
         *
         * @return  True if capture session can be initialized.
         */
        virtual bool init(std::string outFilePath, std::string commandFile, int keepAliveFrequency) = 0;

        /**
         * Start capture module. This interface is called only after successful init
         *
         * @return  True if capture session can be started.
         */
        virtual bool start() = 0;

        /*
        * Stop capture module. Should stop any screen capture session that was already started
        */
        virtual void stop() = 0;

        /*
        * Check if we screen capture session is still running
         *
         * @return  True if capture session is still running.
        */
        virtual bool isCaptureSessionRunning() = 0;

    protected:
        ScreenCaptureInterface() = default;
    };
}
