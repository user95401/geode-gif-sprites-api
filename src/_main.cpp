#include <Geode/Geode.hpp>
using namespace geode::prelude;

#include <gif_lib.h>
#include <CCGIFAnimatedSprite.hpp>//asd

NS_CC_BEGIN;

//forward decl
class CCGIFAnimatedSprite;

struct CCGIFCacheData : public CCObject {
    CCArray* frames;
    GifWord canvasWidth;
    GifWord canvasHeight;
    bool hasTransparentBackground;
    std::string checksum;

    CCGIFCacheData() : frames(nullptr), canvasWidth(0), canvasHeight(0), hasTransparentBackground(false) {}

    virtual ~CCGIFCacheData() {
        CC_SAFE_RELEASE(frames);
    }

    static CCGIFCacheData* create() {
        CCGIFCacheData* data = new CCGIFCacheData();
        if (data) {
            data->autorelease();
            return data;
        }
        CC_SAFE_DELETE(data);
        return nullptr;
    }
};

class CCGIFCacheManager {
public:
    inline static CCGIFCacheManager* s_sharedInstance = nullptr;
    std::map<std::string, CCGIFCacheData*> m_cache;

    CCGIFCacheManager() {}

    static CCGIFCacheManager* get() {
        s_sharedInstance = s_sharedInstance ? s_sharedInstance : new CCGIFCacheManager();
        return s_sharedInstance;
    }

    static void destroyInstance() {
        if (!s_sharedInstance) return;
        s_sharedInstance->purgeCache();
        CC_SAFE_DELETE(s_sharedInstance);
        s_sharedInstance = nullptr;
    }

    //md5 checksum of file data
    std::string calculateChecksum(const unsigned char* data, unsigned long size) {
        unsigned int hash = 0x811c9dc5; // FNV-1a hash
        for (unsigned long i = 0; i < size; i++) {
            hash ^= data[i];
            hash *= 0x01000193;
        }
        char checksumStr[32];
        sprintf(checksumStr, "%08x", hash);
        return std::string(checksumStr);
    }

    CCGIFCacheData* getCachedGIF(const std::string& filename, const std::string& checksum) {
        std::string key = filename + "_" + checksum;
        auto a = m_cache.find(key);
        if (a != m_cache.end()) {
            log::debug("GIF cache hit for: {}", filename);
            return a->second;
        }
        return nullptr;
    }

    void cacheGIF(const std::string& filename, const std::string& checksum, CCGIFCacheData* data) {
        if (!data) return;

        std::string key = filename + "_" + checksum;

        //remove old entry if exists
        auto it = m_cache.find(key);
        if (it != m_cache.end()) {
            it->second->release();
            m_cache.erase(it);
        }

        //add new entry
        data->retain();
        m_cache[key] = data;

        log::debug("Cached GIF: {} (checksum: {})", filename, checksum);
    }

    // remove all entries for this filename (different checksums)
    void removeGIF(const std::string& filename) {
        for (auto it = m_cache.begin(); it != m_cache.end();) {
            if (it->first.find(filename + "_") == 0) {
                it->second->release();
                it = m_cache.erase(it);
            }
            else {
                ++it;
            }
        }
    }

    void purgeCache() {
        for (auto& pair : m_cache) {
            pair.second->release();
        }
        m_cache.clear();
        log::debug("GIF cache purged");
    }

    size_t getCacheSize() const {
        return m_cache.size();
    }

    void logCacheStats() {
        log::debug("GIF Cache Stats: {} entries", m_cache.size());
        for (const auto& pair : m_cache) {
            log::debug("  - {}", pair.first);
        }
    }
};

class CCGIFAnimatedSprite : public CCSprite {
public: //anyways its internal impl, why to private members
    class GIFFrame : public CCObject {
    public:
        CCTexture2D* m_texture = nullptr;
        float m_delay = 0.1f;
        GifImageDesc imageDesc;
        int m_disposalMethod = 0;
        int m_transparentColorIndex = -1;

        virtual ~GIFFrame() { CC_SAFE_RELEASE(m_texture); }

        //create a copy of this frame for caching
        GIFFrame* copy() {
            GIFFrame* newFrame = new GIFFrame();
            newFrame->m_delay = m_delay;
            newFrame->imageDesc = imageDesc;
            newFrame->m_disposalMethod = m_disposalMethod;
            newFrame->m_transparentColorIndex = m_transparentColorIndex;
            if (m_texture) {
                newFrame->m_texture = m_texture;
                newFrame->m_texture->retain();
            }
            return newFrame;
        }
    };

    CCArray* m_frames = nullptr;
    unsigned int m_currentFrame = 0;
    float m_frameTimer = 0.0f;
    bool m_isPlaying = true;
    bool m_loop = true;
    GifWord m_canvasWidth = 0;
    GifWord m_canvasHeight = 0;
    GifByteType* m_canvasBuffer = nullptr;
    GifByteType* m_previousBuffer = nullptr;
    ColorMapObject* m_globalColorMap = nullptr;
    bool m_hasTransparentBackground = false;
    std::string m_filename = "";
    std::string m_checksum = "";

    static CCGIFAnimatedSprite* create(const char* pszFileName) {
        CCGIFAnimatedSprite* sprite = new CCGIFAnimatedSprite();
        if (sprite and sprite->initWithGIFFile(pszFileName)) {
            sprite->autorelease();
            return sprite;
        }
        CC_SAFE_DELETE(sprite);
        return nullptr;
    }

    ~CCGIFAnimatedSprite() {
        CC_SAFE_RELEASE(m_frames);
        if (m_canvasBuffer) {
            CC_SAFE_FREE(m_canvasBuffer);
            m_canvasBuffer = nullptr;
        }
        if (m_previousBuffer) {
            CC_SAFE_FREE(m_previousBuffer);
            m_previousBuffer = nullptr;
        }
        if (m_globalColorMap) {
            GifFreeMapObject(m_globalColorMap);
            m_globalColorMap = nullptr;
        }
    }

    bool initWithGIFFile(const char* pszFileName) {
        if (!pszFileName) {
            log::error("GIF filename is null...");
            return false;
        }

        m_filename = string::pathToString(pszFileName); //i think its useless to

        unsigned long fileSize = 0;
        unsigned char* fileData = CCFileUtils::get()->getFileData(pszFileName, "rb", &fileSize);
        if (!fileData or fileSize == 0) {
            log::error("Failed to read GIF file: {}", pszFileName);
            if (fileData) CC_SAFE_FREE(fileData);
            return false;
        }

        m_checksum = CCGIFCacheManager::get()->calculateChecksum(fileData, fileSize);

        //check cache first
        CCGIFCacheData* cachedData = CCGIFCacheManager::get()->getCachedGIF(m_filename, m_checksum);
        if (cachedData) {
            bool success = initWithCachedData(cachedData);
            CC_SAFE_FREE(fileData);
            return success;
        }

        struct GifMemoryData {
            unsigned char* data;
            unsigned long size;
            unsigned long position;
        };
        GifMemoryData memData = { fileData, fileSize, 0 };

        auto inputFunc = [](GifFileType* gif, GifByteType* buf, int count)
            {
                GifMemoryData* memData = static_cast<GifMemoryData*>(gif->UserData);
                if (!memData or !buf) return 0;

                int bytesToRead = count;
                if (memData->position + bytesToRead > memData->size) {
                    bytesToRead = memData->size - memData->position;
                }

                if (bytesToRead <= 0) return 0;

                memcpy(buf, memData->data + memData->position, bytesToRead);
                memData->position += bytesToRead;
                return bytesToRead;
            };

        int error = 0;
        GifFileType* gifFile = DGifOpen(&memData, inputFunc, &error);
        if (!gifFile) {
            log::error("Failed to open GIF file at {}: {}", pszFileName, GifErrorString(error));
            CC_SAFE_FREE(fileData);
            return false;
        }

        //read all gif data...
        if (DGifSlurp(gifFile) == GIF_ERROR) {
            log::error("Failed to read GIF data from {}: {}", pszFileName, GifErrorString(gifFile->Error));
            DGifCloseFile(gifFile);
            CC_SAFE_FREE(fileData);
            return false;
        }

        bool success = processGIFData(gifFile);

        DGifCloseFile(gifFile);
        CC_SAFE_FREE(fileData);

        if (!success) {
            log::error("Failed to process GIF data from {}", pszFileName);
            return false;
        }

        //cache the processed data pls
        cacheProcessedData();

        //init with first frame if available
        if (m_frames and m_frames->count() > 0) {
            GIFFrame* firstFrame = typeinfo_cast<GIFFrame*>(m_frames->objectAtIndex(0));
            if (firstFrame and firstFrame->m_texture) {
                initWithTexture(firstFrame->m_texture);
                scheduleUpdate();
                return true;
            }
        }

        log::error("No valid GIF frames found in {}!", pszFileName);
        return false;
    }

    bool initWithCachedData(CCGIFCacheData* cachedData) {
        if (!cachedData or !cachedData->frames or cachedData->frames->count() == 0) {
            log::error("Failed to create GIF sprite from cached data.");
            log::error("{}->cachedData = {}", this, cachedData);
            if (auto a = cachedData) {
                log::error("{}->cachedData->frames = {}", this, a->frames);
				log::error("{}->cachedData->frames->count() = {}", this, a->frames->count());
            }
            return false;
        }

        m_canvasWidth = cachedData->canvasWidth;
        m_canvasHeight = cachedData->canvasHeight;
        m_hasTransparentBackground = cachedData->hasTransparentBackground;

        //alloc canvas buffers
        size_t canvasSize = m_canvasWidth * m_canvasHeight * 4; //rgba
        m_canvasBuffer = static_cast<GifByteType*>(malloc(canvasSize));
        m_previousBuffer = static_cast<GifByteType*>(malloc(canvasSize));

        if (!m_canvasBuffer or !m_previousBuffer) {
            log::error("Failed to allocate canvas buffers.");
            return false;
        }

        initializeCanvas();

        //copy frames from cache
        m_frames = CCArray::create();
        m_frames->retain();

        for (unsigned int i = 0; i < cachedData->frames->count(); i++) {
            GIFFrame* cachedFrame = typeinfo_cast<GIFFrame*>(cachedData->frames->objectAtIndex(i));
            if (cachedFrame) {
                GIFFrame* frameCopy = cachedFrame->copy();
                m_frames->addObject(frameCopy);
                frameCopy->release();
            }
        }

        if (m_frames->count() == 0) {
            log::error("No frames copied from cache");
            return false;
        }

        //init with first frame
        GIFFrame* firstFrame = typeinfo_cast<GIFFrame*>(m_frames->objectAtIndex(0));
        if (firstFrame and firstFrame->m_texture) {
            initWithTexture(firstFrame->m_texture);
            scheduleUpdate();
            log::debug(
                "Successfully initialized GIF from cache for {} ({} frames)",
                m_filename, m_frames->count()
            );
            return true;
        }

        return false;
    }

    void cacheProcessedData() {
        if (!m_frames or m_frames->count() == 0) return; //ok

        CCGIFCacheData* cacheData = CCGIFCacheData::create();
        if (!cacheData) {
            log::error("Failed to create cache data");
            log::error("{}->cacheData = {}", this, cacheData);
            return;
        }

        cacheData->canvasWidth = m_canvasWidth;
        cacheData->canvasHeight = m_canvasHeight;
        cacheData->hasTransparentBackground = m_hasTransparentBackground;
        cacheData->checksum = m_checksum;

        //copy frames for caching
        cacheData->frames = CCArray::create();
        cacheData->frames->retain();

        for (unsigned int i = 0; i < m_frames->count(); i++) {
            GIFFrame* frame = typeinfo_cast<GIFFrame*>(m_frames->objectAtIndex(i));
            if (frame) {
                GIFFrame* frameCopy = frame->copy();
                cacheData->frames->addObject(frameCopy);
                frameCopy->release();
            }
        }

        CCGIFCacheManager::get()->cacheGIF(m_filename, m_checksum, cacheData);
    }

    bool processGIFData(GifFileType* gifFile) {
        if (!gifFile or gifFile->ImageCount <= 0) {
            log::error("Invalid GIF file or no images");
            return false;
        }

        m_canvasWidth = gifFile->SWidth;
        m_canvasHeight = gifFile->SHeight;

        if (m_canvasWidth == 0 or m_canvasHeight == 0) {
            log::error("Invalid GIF canvas dimensions: {}x{}", m_canvasWidth, m_canvasHeight);
            return false;
        }

        //alloc canvas buff
        size_t canvasSize = m_canvasWidth * m_canvasHeight * 4; //rgba
        m_canvasBuffer = static_cast<GifByteType*>(malloc(canvasSize));
        m_previousBuffer = static_cast<GifByteType*>(malloc(canvasSize));

        if (!m_canvasBuffer or !m_previousBuffer) {
            log::error("Failed to allocate canvas buffers");
            return false;
        }

        //store global color map
        if (gifFile->SColorMap) {
            m_globalColorMap = GifMakeMapObject(gifFile->SColorMap->ColorCount, gifFile->SColorMap->Colors);
            if (!m_globalColorMap) {
                log::error("Failed to copy global color map");
                return false;
            }
        }

        //check if any fucking frame has transparencyyyyyaa
        m_hasTransparentBackground = false;
        for (int i = 0; i < gifFile->ImageCount; i++) {
            GraphicsControlBlock gcb;
            if (DGifSavedExtensionToGCB(gifFile, i, &gcb) == GIF_OK) {
                if (gcb.TransparentColor != NO_TRANSPARENT_COLOR) {
                    m_hasTransparentBackground = true;
                    break;
                }
            }
        }

        initializeCanvas();

        m_frames = CCArray::create();
        m_frames->retain();

        //process each frame
        for (int i = 0; i < gifFile->ImageCount; i++) {
            SavedImage* savedImage = &gifFile->SavedImages[i];
            if (!savedImage or !savedImage->RasterBits) {
                log::warn("Skipping invalid frame {}", i);
                continue;
            }

            GIFFrame* frame = new GIFFrame();
            if (!processFrame(frame, savedImage, gifFile, i)) {
                log::warn("Failed to process frame {}", i);
                CC_SAFE_DELETE(frame);
                continue;
            }

            m_frames->addObject(frame);
            frame->release(); //CCArray retains it
        }

        if (m_frames->count() == 0) {
            log::error("No valid frames processed");
            return false;
        }

        log::debug(
            "Successfully loaded GIF with {} frames ({}x{})",
            m_frames->count(), m_canvasWidth, m_canvasHeight
        );
        return true;
    }

    void initializeCanvas() {
        if (!m_canvasBuffer) return;

        size_t canvasSize = m_canvasWidth * m_canvasHeight * 4;

        for (size_t i = 0; i < canvasSize; i += 4) {
            m_canvasBuffer[i] = 0;
            m_canvasBuffer[i + 1] = 0;
            m_canvasBuffer[i + 2] = 0;
            m_canvasBuffer[i + 3] = 0;
        }

        memcpy(m_previousBuffer, m_canvasBuffer, canvasSize);
    }

    bool processFrame(GIFFrame* frame, SavedImage* savedImage, GifFileType* gifFile, int frameIndex) {
        if (!frame or !savedImage) return false;

        frame->imageDesc = savedImage->ImageDesc;
        frame->m_delay = 0.1f; //default delay
        frame->m_disposalMethod = DISPOSE_DO_NOT;
        frame->m_transparentColorIndex = NO_TRANSPARENT_COLOR;

        //parse graphics control block
        GraphicsControlBlock gcb;
        if (DGifSavedExtensionToGCB(gifFile, frameIndex, &gcb) == GIF_OK) {
            frame->m_delay = gcb.DelayTime > 0 ? gcb.DelayTime / 100.0f : 0.1f;
            frame->m_disposalMethod = gcb.DisposalMode;
            frame->m_transparentColorIndex = gcb.TransparentColor;
        }

        //choose color map (local takes precedence over global)
        ColorMapObject* colorMap = savedImage->ImageDesc.ColorMap 
            ? savedImage->ImageDesc.ColorMap 
            : m_globalColorMap;

        if (!colorMap) {
            log::error("No color map available for frame {}", frameIndex);
            return false;
        }

        //apply disposal method from previous frame BEFORE rendering current frame
        if (frameIndex > 0) {
            GIFFrame* prevFrame = typeinfo_cast<GIFFrame*>(m_frames->objectAtIndex(frameIndex - 1));
            if (prevFrame) {
                applyDisposalMethodForFrame(prevFrame);
            }
        }

        //render current frame to canvas
        if (!renderFrameToCanvas(savedImage, colorMap, frame->m_transparentColorIndex)) {
            log::error("Failed to render frame {} to canvas", frameIndex);
            return false;
        }

        //create texture from current canvas state
        frame->m_texture = createTextureFromCanvas();
        if (!frame->m_texture) {
            log::error("Failed to create texture for frame {}", frameIndex);
            return false;
        }

        frame->m_texture->retain();

        return true;
    }

    void applyDisposalMethodForFrame(GIFFrame* frame) {
        if (!frame) return;

        switch (frame->m_disposalMethod) {
        case DISPOSE_BACKGROUND: //clear frame area to transparent
            clearFrameAreaToTransparent(frame->imageDesc);
            break;
        case DISPOSE_PREVIOUS: //restore previous canvas state
            memcpy(m_canvasBuffer, m_previousBuffer, m_canvasWidth * m_canvasHeight * 4);
            break;
        default: //other disposal methods are treated as DISPOSE_DO_NOT
            break;
        };
    }

    void clearFrameAreaToTransparent(const GifImageDesc& imageDesc) {
        int left = imageDesc.Left;
        int top = imageDesc.Top;
        int width = imageDesc.Width;
        int height = imageDesc.Height;

        //clamp to canvas bounds
        if (left < 0) { width += left; left = 0; }
        if (top < 0) { height += top; top = 0; }
        if (left + width > m_canvasWidth) width = m_canvasWidth - left;
        if (top + height > m_canvasHeight) height = m_canvasHeight - top;

        if (width <= 0 or height <= 0) return;

        //clear area to transparent
        for (int y = top; y < top + height; y++) {
            for (int x = left; x < left + width; x++) {
                int pixelIndex = (y * m_canvasWidth + x) * 4;
                m_canvasBuffer[pixelIndex] = 0;
                m_canvasBuffer[pixelIndex + 1] = 0;
                m_canvasBuffer[pixelIndex + 2] = 0;
                m_canvasBuffer[pixelIndex + 3] = 0;
            }
        }
    }

    bool renderFrameToCanvas(SavedImage* savedImage, ColorMapObject* colorMap, int transparentColorIndex) {
        if (!savedImage or !savedImage->RasterBits or !colorMap) return false;

        GifImageDesc& imageDesc = savedImage->ImageDesc;
        int left = imageDesc.Left;
        int top = imageDesc.Top;
        int width = imageDesc.Width;
        int height = imageDesc.Height;

        //validate bounds
        if (left < 0 or top < 0 or left + width > m_canvasWidth or top + height > m_canvasHeight) {
            log::warn("Frame extends beyond canvas bounds: {}x{} at ({},{})", width, height, left, top);
            //clamp to valid region
            if (left < 0) { width += left; left = 0; }
            if (top < 0) { height += top; top = 0; }
            if (left + width > m_canvasWidth) width = m_canvasWidth - left;
            if (top + height > m_canvasHeight) height = m_canvasHeight - top;

            if (width <= 0 or height <= 0) return false;
        }

        //save current canvas state for DISPOSE_PREVIOUS
        memcpy(m_previousBuffer, m_canvasBuffer, m_canvasWidth * m_canvasHeight * 4);

        GifByteType* rasterBits = savedImage->RasterBits;

        //handle interlaced images
        int* passHeights = nullptr;
        int* passStarts = nullptr;
        int passCount = 1;

        if (imageDesc.Interlace) {
            static int interlaceOffsets[] = { 0, 4, 2, 1 };
            static int interlaceJumps[] = { 8, 8, 4, 2 };
            passCount = 4;

            passStarts = interlaceOffsets;
            passHeights = interlaceJumps;
        }

        int rasterIndex = 0;

        for (int pass = 0; pass < passCount; pass++) {
            int startRow = imageDesc.Interlace ? passStarts[pass] : 0;
            int rowStep = imageDesc.Interlace ? passHeights[pass] : 1;

            for (int y = startRow; y < height; y += rowStep) {
                for (int x = 0; x < width; x++) {
                    if (rasterIndex >= width * height) {
                        log::error("Raster index out of bounds");
                        return false;
                    }

                    GifByteType colorIndex = rasterBits[rasterIndex++];

                    //skip transparent pixels - leave existing pixel
                    if (transparentColorIndex != NO_TRANSPARENT_COLOR and colorIndex == transparentColorIndex) {
                        continue;
                    }

                    //validate color index
                    if (colorIndex >= colorMap->ColorCount) {
                        log::warn("Color index {} out of range (max {})", colorIndex, colorMap->ColorCount - 1);
                        continue;
                    }

                    GifColorType& color = colorMap->Colors[colorIndex];

                    int canvasX = left + x;
                    int canvasY = top + y;
                    int pixelIndex = (canvasY * m_canvasWidth + canvasX) * 4;

                    m_canvasBuffer[pixelIndex] = color.Red;
                    m_canvasBuffer[pixelIndex + 1] = color.Green;
                    m_canvasBuffer[pixelIndex + 2] = color.Blue;
                    m_canvasBuffer[pixelIndex + 3] = 255; //opaque for non transparent pixels
                }
            }
        }

        return true;
    }

    CCTexture2D* createTextureFromCanvas() {
        if (!m_canvasBuffer) return nullptr;

        CCTexture2D* texture = new CCTexture2D();
        if (!texture) return nullptr;

        //create a copy of canvas data for texture
        void* textureData = malloc(m_canvasWidth * m_canvasHeight * 4);
        if (!textureData) {
            CC_SAFE_DELETE(texture);
            return nullptr;
        }

        memcpy(textureData, m_canvasBuffer, m_canvasWidth * m_canvasHeight * 4);

        bool success = texture->initWithData(
            textureData,
            kCCTexture2DPixelFormat_RGBA8888,
            m_canvasWidth,
            m_canvasHeight,
            CCSizeMake(m_canvasWidth, m_canvasHeight)
        );

        CC_SAFE_FREE(textureData);

        if (!success) {
            CC_SAFE_DELETE(texture);
            return nullptr;
        }

        return texture;
    }

    virtual void update(float dt) override {
        if (!m_isPlaying or !m_frames or m_frames->count() <= 1) {
            return;
        }

        m_frameTimer += dt;

        GIFFrame* currentFrame = typeinfo_cast<GIFFrame*>(m_frames->objectAtIndex(m_currentFrame));
        if (currentFrame and m_frameTimer >= currentFrame->m_delay) {
            m_frameTimer = 0.0f;

            // Apply disposal method for current frame before moving to next
            applyDisposalMethodForFrame(currentFrame);

            m_currentFrame++;

            if (m_currentFrame >= m_frames->count()) {
                if (m_loop) {
                    m_currentFrame = 0;
                    // Reset canvas to initial state for looping
                    initializeCanvas();
                }
                else {
                    m_currentFrame = m_frames->count() - 1;
                    m_isPlaying = false;
                    return;
                }
            }

            // Re-render current frame
            GIFFrame* nextFrame = typeinfo_cast<GIFFrame*>(m_frames->objectAtIndex(m_currentFrame));
            if (nextFrame and nextFrame->m_texture) {
                setTexture(nextFrame->m_texture);
            }
        }
    }

    void play() { m_isPlaying = true; }
    void pause() { m_isPlaying = false; }
    void stop() { m_isPlaying = false; m_currentFrame = 0; }
    void setLoop(bool loop) { m_loop = loop; }
    bool isPlaying() const { return m_isPlaying; }
    unsigned int getCurrentFrame() const { return m_currentFrame; }
    unsigned int getFrameCount() const { return m_frames ? m_frames->count() : 0; }

    void setCurrentFrame(unsigned int frame) {
        if (!m_frames or frame >= m_frames->count()) return;

        m_currentFrame = frame;
        m_frameTimer = 0.0f;

        GIFFrame* targetFrame = typeinfo_cast<GIFFrame*>(m_frames->objectAtIndex(frame));
        if (targetFrame and targetFrame->m_texture) {
            setTexture(targetFrame->m_texture);
        }
    }

    //cache management methods
    static void purgeCachedGIFs() {
        CCGIFCacheManager::get()->purgeCache();
    }
    static void removeCachedGIF(const char* filename) {
        if (filename) CCGIFCacheManager::get()->removeGIF(std::string(filename));
    }
    static size_t getCacheSize() {
        return CCGIFCacheManager::get()->getCacheSize();
    }
    static void logCacheStats() {
        CCGIFCacheManager::get()->logCacheStats();
    }

    //get cache info for this sprite
    const std::string& getFilename() const { return m_filename; }
    const std::string& getChecksum() const { return m_checksum; }
};

NS_CC_END;

#include <Geode/modify/CCSprite.hpp>
class $modify(CCSpriteGifExt, CCSprite) {
public:
    static bool isGifHeader(const char* filename) {
        //im gonna to self-harm if it will be slow on android
        unsigned long size = 0;
        auto data = CCFileUtils::get()->getFileData(filename, "rb", &size);
        if (!data or size < 6) {
            if (data) CC_SAFE_FREE(data);
            return false;
        }
        bool is_gif = (memcmp(data, "GIF87a", 6) == 0 or memcmp(data, "GIF89a", 6) == 0);
        CC_SAFE_FREE(data);
        return is_gif;
    }

    static CCSprite* create(const char* pszFileName) {
        //header check allows users to hack around extension in filenames
        if (isGifHeader(pszFileName)) {
            if (auto gifSprite = CCGIFAnimatedSprite::create(pszFileName)) {
                return gifSprite;
            }
            else log::error("Failed to create GIF sprite from {}", pszFileName);
        }
        return CCSprite::create(pszFileName);
    }
};