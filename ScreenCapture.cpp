
#include "ScreenCaptureImpl.hpp"

namespace CapUtils {

    ScreenCapture::ScreenCapture(std::string configFileName) : impl(std::make_shared<ScreenCapture::Impl>(std::move(configFileName))) {
    }

    ScreenCapture::~ScreenCapture() {
        impl.reset();
    }

    bool ScreenCapture::init(std::string outFilePath, std::string commandFile, int keepAliveFrequency) {
        return impl->init(outFilePath, commandFile, keepAliveFrequency);
    }

    bool ScreenCapture::start() {
        return impl->start();
    }

    void ScreenCapture::stop() {
        impl->stop();
    }

    bool ScreenCapture::isCaptureSessionRunning() {
        return impl->isCaptureSessionRunning();
    }
}
