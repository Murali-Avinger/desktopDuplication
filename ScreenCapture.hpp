#pragma once

#include <string>
#include <memory>

#include "ScreenCaptureInterface.hpp"

namespace CapUtils {

    /*
    * Screen capture class to grab screen region from desktop and stores them as a continuous
    * segmented transport streams. This is a top level interface class that can be used by
    * any app to carry out screen recording. 
    */
    class ScreenCapture : public ScreenCaptureInterface {
    public:

        /**
         * ScreenCapture constructor. Class instance is instantiated by any App that intends to perform screen recording
         *
         * @param configFileName
         *     Configuration JSON file that defines screen capture parameters.
         *
         */
        explicit ScreenCapture(std::string configFileName);

        virtual ~ScreenCapture();

        /*
        * @name Copy and move
        *
        * No copying and moving allowed. These objects are always held by a smart pointer
        */

        ScreenCapture(const ScreenCapture&) = delete;
        ScreenCapture& operator=(const ScreenCapture&) = delete;

        ScreenCapture(ScreenCapture&&) = delete;
        ScreenCapture& operator=(ScreenCapture&&) = delete;

        /*
        * Capture interface overrides
        */
        bool init(std::string outFilePath, std::string commandFile, int keepAliveFrequency) override;
        bool start() override;
        void stop() override;
        bool isCaptureSessionRunning() override;

    private:

        /*
        * Core screen capture session is implemented by PImpl technique to separate implementation details from interface
        */
        class Impl;
        std::shared_ptr<Impl> impl;
    };

    using ScreenCapture_p = std::shared_ptr<ScreenCapture>;

} // CapUtils
