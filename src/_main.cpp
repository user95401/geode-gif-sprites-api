#include <Geode/Geode.hpp>
#include <gif_lib.h>

using namespace geode::prelude;

struct GifFrame {
    CCTexture2D* texture;
    float delay;
};

class GifCache {
public:
    static GifCache& getInstance() {
        static GifCache instance;
        return instance;
    }

    // Retrieve frames for path; decode once if not cached
    const std::vector<GifFrame>& loadFrames(const std::string& path) {
        std::lock_guard<std::mutex> lock(m_mutex);

        auto it = m_cache.find(path);
        if (it != m_cache.end()) {
            return it->second;
        }
        
        std::vector<GifFrame> frames;
        if (decodeGif(path, frames)) m_cache[path] = std::move(frames);
        
        return m_cache[path];
    }

private:
    GifCache() = default;
    ~GifCache() {
        clearCache();
    }

    GifCache(const GifCache&) = delete;
    GifCache& operator=(const GifCache&) = delete;

    void clearCache() {
        for (auto& [_, frames] : m_cache) {
            for (auto& frame : frames) {
                if (frame.texture) frame.texture->release();
            }
        }
    }

    bool decodeGif(const std::string& filePath, std::vector<GifFrame>& outFrames) {
        int error = 0;
        GifFileType* gif = DGifOpenFileName(filePath.c_str(), &error);

        if (!gif) {
            log::error("Failed to open GIF file: {}", filePath);
            return false;
        }

        if (DGifSlurp(gif) == GIF_ERROR) {
            log::error("Failed to slurp GIF file: {}", filePath);
            DGifCloseFile(gif);
            return false;
        }

        int width = gif->SWidth;
        int height = gif->SHeight;

        if (width <= 0 || height <= 0) {
            log::error("Invalid GIF dimensions: {}x{}", width, height);
            DGifCloseFile(gif);
            return false;
        }

        std::vector<uint8_t> canvas(width * height * 4, 0);

        for (int i = 0; i < gif->ImageCount; ++i) {
            SavedImage *img = &gif->SavedImages[i];

            GraphicsControlBlock gcb; // maki :D
            DGifSavedExtensionToGCB(gif, i, &gcb);

            float delay = gcb.DelayTime * 0.01f;
            if (delay < 0.01f) delay = 0.1f;

            int transparentColor = (gcb.TransparentColor == NO_TRANSPARENT_COLOR) ? 
                                   -1 : gcb.TransparentColor;

            if (gcb.DisposalMode == DISPOSE_BACKGROUND) clearFrameRegion(canvas, img, width);

            ColorMapObject* colorMap = img->ImageDesc.ColorMap ? 
                                       img->ImageDesc.ColorMap : gif->SColorMap;
            
            if (!colorMap) {
                log::warn("No color map for frame {} in GIF {}", i, filePath);
                continue;
            }

            renderFrame(canvas, img, colorMap, transparentColor, width);

            CCTexture2D* texture = createTexture(canvas.data(), width, height);
            if (texture) outFrames.push_back({texture, delay});
        }

        DGifCloseFile(gif);

        if (outFrames.empty()) {
            log::error("No valid frames decoded from GIF: {}", filePath);
            return false;
        }

        return true;
    }

    // for DISPOSE_BACKGROUND mode
    void clearFrameRegion(std::vector<uint8_t>& canvas, SavedImage* img, int width) {
        int top = img->ImageDesc.Top;
        int left = img->ImageDesc.Left;
        int height = img->ImageDesc.Height;
        int frameWidth = img->ImageDesc.Width;

        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < frameWidth; ++x) {
                int idx = ((top + y) * width + (left + x)) * 4;
                if (idx >= 0 && idx + 3 < static_cast<int>(canvas.size())) std::fill_n(canvas.data() + idx, 4, 0);
            }
        }
    }

    void renderFrame(std::vector<uint8_t>& canvas, SavedImage* img, 
                    ColorMapObject* colorMap, int transparentColor, int width) {
        int top = img->ImageDesc.Top;
        int left = img->ImageDesc.Left;
        int height = img->ImageDesc.Height;
        int frameWidth = img->ImageDesc.Width;

        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < frameWidth; ++x) {
                int srcIndex = y * frameWidth + x;
                if (srcIndex >= 0 && srcIndex < img->ImageDesc.Width * img->ImageDesc.Height) {
                    int colorIndex = img->RasterBits[srcIndex];

                    if (colorIndex == transparentColor) continue;

                    if (colorIndex >= 0 && colorIndex < colorMap->ColorCount) {
                        GifColorType color = colorMap->Colors[colorIndex];
                        int dstIndex = ((top + y) * width + (left + x)) * 4;

                        if (dstIndex >= 0 && dstIndex + 3 < static_cast<int>(canvas.size())) {
                            canvas[dstIndex + 0] = color.Red;
                            canvas[dstIndex + 1] = color.Green;
                            canvas[dstIndex + 2] = color.Blue;
                            canvas[dstIndex + 3] = 255;
                        }
                    }
                }
            }
        }
    }

    CCTexture2D* createTexture(const uint8_t* data, int width, int height) {
        CCTexture2D* texture = new CCTexture2D();
        if (texture->initWithData(
            data,
            kCCTexture2DPixelFormat_RGBA8888,
            width,
            height,
            CCSizeMake(width, height)
        )) return texture;
        else {
            texture->release();
            log::error("Failed to create texture for GIF frame");
            return nullptr;
        }
    }

    std::unordered_map<std::string, std::vector<GifFrame>> m_cache;
    std::mutex m_mutex;
};

class CCGIFAnimatedSprite : public CCSprite {
public:
    /**
     * Create animated GIF sprite from a file
     */
    static CCGIFAnimatedSprite* create(const char* filePath) {
        auto* sprite = new CCGIFAnimatedSprite();
        if (sprite && sprite->initWithGif(filePath)) {
            sprite->autorelease();
            return sprite;
        }
        CC_SAFE_DELETE(sprite);
        return nullptr;
    }

    /**
     * Initialize a sprite from a GIF file
     */
    bool initWithGif(const char* filePath) {
        if (!CCSprite::init()) return false;

        if (!fileExistsInSearchPaths(filePath)) {
            log::error("GIF file not found: {}", filePath);
            return false;
        }

        std::string fullPath = CCFileUtils::get()->fullPathForFilename(filePath, false);

        const auto& frames = GifCache::getInstance().loadFrames(fullPath);

        if (frames.empty()) {
            log::error("No frames loaded for GIF: {}", filePath);
            return false;
        }

        m_frames.reserve(frames.size());
        m_delays.reserve(frames.size());

        for (const auto& frame : frames) {
            m_frames.push_back(frame.texture);
            m_delays.push_back(frame.delay);
        }

        setFirstFrame();
        scheduleUpdate();

        return true;
    }

    void update(float dt) override {
        if (m_frames.empty() || m_delays.empty()) return;

        m_elapsed += dt;

        if (m_elapsed >= m_delays[m_index]) {
            m_elapsed -= m_delays[m_index];
            m_index = (m_index + 1) % m_frames.size();

            setFrame(m_index);
        }
    }

    void setFrame(size_t frameIndex) {
        if (frameIndex < m_frames.size() && m_frames[frameIndex]) {
            setTexture(m_frames[frameIndex]);
            setTextureRect(CCRectMake(
                0, 0,
                m_frames[frameIndex]->getContentSize().width,
                m_frames[frameIndex]->getContentSize().height
            ));
        }
    }

protected:
    CCGIFAnimatedSprite() : m_index(0), m_elapsed(0.0f) {}

    void setFirstFrame() {
        if (!m_frames.empty() && m_frames[0]) setFrame(0);
    }

private:
    std::vector<CCTexture2D*> m_frames;     // Frame textures
    std::vector<float> m_delays;            // Delays between frames
    size_t m_index;                         // Current frame index
    float m_elapsed;                        // Elapsed time since last frame
};

bool isGif(const char* fileName) {
    if (!fileName || !*fileName) return false;

    std::string_view fileNameView = fileName;
    bool hasGifExtension = fileNameView.size() > 4 &&
                          fileNameView.substr(fileNameView.size() - 4) == ".gif";

    if (!hasGifExtension) {
        std::string fullPath;
        try {
            fullPath = CCFileUtils::get()->fullPathForFilename(fileName, false);
        } catch(const std::exception& e) {
            log::warn("Error getting full path for file {}: {}", fileName, e.what());
            return false;
        }

        std::ifstream fileStream(fullPath.c_str(), std::ios::binary);
        if (!fileStream) {
            log::warn("Failed to open file for GIF verification: {}", fullPath);
            return false;
        }

        char header[6] = {0};
        if (!fileStream.read(header, 6)) {
            log::warn("Failed to read header from file: {}", fullPath);
            return false;
        }

        return std::memcmp(header, "GIF87a", 6) == 0 || 
               std::memcmp(header, "GIF89a", 6) == 0;
    }

    return hasGifExtension;
}

#include <Geode/modify/CCSprite.hpp>
class $modify(CCSprite) {
public:
    static CCSprite* create(const char* pszFileName) {
        if (!pszFileName || !*pszFileName) return CCSprite::createWithTexture(nullptr);

        try {
            if (isGif(pszFileName)) {
                auto gifSprite = CCGIFAnimatedSprite::create(pszFileName);
                if (gifSprite) return gifSprite;

                log::warn("Failed to create GIF sprite, falling back to regular sprite: {}", pszFileName);
            }
        } catch (const std::exception& e) {
            log::error("Exception while checking/creating GIF sprite: {}", e.what());
        }

        return CCSprite::create(pszFileName);
    }
};
