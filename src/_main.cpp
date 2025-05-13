#include <Geode/Geode.hpp>
#include <gif_lib.h>

using namespace geode::prelude;

struct GifFrame {
    CCTexture2D* texture;
    float delay;
};

class GifCache {
public:
    static GifCache& get() {
        static GifCache instance;
        return instance;
    }

    // Retrieve frames for path; decode once if not cached
    const std::vector<GifFrame>& load(const std::string& path) {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_cache.find(path);
        if (it != m_cache.end()) {
            return it->second;
        }
        // decode synchronously into local vector
        std::vector<GifFrame> frames;
        decodeInternal(path, frames);
        m_cache[path] = frames;
        return m_cache[path];
    }

private:
    GifCache() = default;
    ~GifCache() {
        // release textures
        for (auto& pair : m_cache) {
            for (auto& frame : pair.second) {
                frame.texture->release();
            }
        }
    }
    GifCache(const GifCache&) = delete;
    GifCache& operator=(const GifCache&) = delete;

    void decodeInternal(const std::string& filePath, std::vector<GifFrame>& outFrames) {
        int err = 0;
        GifFileType* gif = DGifOpenFileName(filePath.c_str(), &err);
        if (!gif || DGifSlurp(gif) == GIF_ERROR) {
            if (gif) DGifCloseFile(gif);
            log::error("Failed to load GIF {}", filePath);
            return;
        }
        int w = gif->SWidth, h = gif->SHeight;
        std::vector<unsigned char> canvas(w * h * 4, 0);

        for (int i = 0; i < gif->ImageCount; ++i) {
            SavedImage* img = &gif->SavedImages[i];
            GraphicsControlBlock gcb;
            DGifSavedExtensionToGCB(gif, i, &gcb);
            float delay = gcb.DelayTime * 0.01f;
            int trans = (gcb.TransparentColor == NO_TRANSPARENT_COLOR ? -1 : gcb.TransparentColor);
            if (gcb.DisposalMode == DISPOSE_BACKGROUND) {
                int top = img->ImageDesc.Top;
                int left = img->ImageDesc.Left;
                int hh = img->ImageDesc.Height;
                int ww = img->ImageDesc.Width;
                for (int yy = 0; yy < hh; ++yy) {
                    for (int xx = 0; xx < ww; ++xx) {
                        int idx = ((top + yy) * w + (left + xx)) * 4;
                        std::fill_n(canvas.data() + idx, 4, 0);
                    }
                }
            }
            ColorMapObject* cmap = img->ImageDesc.ColorMap ? img->ImageDesc.ColorMap : gif->SColorMap;
            if (!cmap) continue;
            int top = img->ImageDesc.Top;
            int left = img->ImageDesc.Left;
            int hh = img->ImageDesc.Height;
            int ww = img->ImageDesc.Width;
            for (int yy = 0; yy < hh; ++yy) {
                for (int xx = 0; xx < ww; ++xx) {
                    int src = yy * ww + xx;
                    int idxc = img->RasterBits[src];
                    if (idxc == trans) continue;
                    GifColorType c = cmap->Colors[idxc];
                    int dst = ((top + yy) * w + (left + xx)) * 4;
                    canvas[dst + 0] = c.Red;
                    canvas[dst + 1] = c.Green;
                    canvas[dst + 2] = c.Blue;
                    canvas[dst + 3] = 255;
                }
            }
            // create texture on main thread synchronously
            CCTexture2D* tex = new CCTexture2D();
            if (tex->initWithData(canvas.data(), kCCTexture2DPixelFormat_RGBA8888, w, h, CCSizeMake(w, h))) {
                // retain for cache destruction
                outFrames.push_back({ tex, delay });
            }
            else {
                tex->release();
            }
        }
        DGifCloseFile(gif);
    }

    std::unordered_map<std::string, std::vector<GifFrame>> m_cache;
    std::mutex m_mutex;
};

class CCGIFAnimatedSprite : public CCSprite {
public:
    static CCGIFAnimatedSprite* create(const char* file) {
        auto* asd = new CCGIFAnimatedSprite();
        if (asd && asd->initWithGif(file)) {
            asd->autorelease();
            return asd;
        }
        CC_SAFE_DELETE(asd);
        return nullptr;
    }

    bool initWithGif(const char* file) {
        if (!CCSprite::init()) return false;
        if (!fileExistsInSearchPaths(file)) {
            log::error("GIF '{}' not found", file);
            return false;
        }

        //load all frames once via cache
        const auto& frames = GifCache::get().load(
            CCFileUtils::get()->fullPathForFilename(file, false).c_str()
        );
        if (frames.empty()) return false;

        // copy pointers + delays
        for (auto& f : frames) {
            m_frames.push_back(f.texture);
            m_delays.push_back(f.delay);
        }
        // set first frame
        this->setTexture(m_frames[0]);
        this->setTextureRect(CCRectMake(0, 0,
            m_frames[0]->getContentSize().width,
            m_frames[0]->getContentSize().height));
        this->scheduleUpdate();
        return true;
    }

    void update(float dt) override {
        m_elapsed += dt;
        if (m_elapsed >= m_delays[m_index]) {
            m_elapsed -= m_delays[m_index];
            m_index = (m_index + 1) % m_frames.size();
            this->setTexture(m_frames[m_index]);
            this->setTextureRect(CCRectMake(0, 0,
                m_frames[m_index]->getContentSize().width,
                m_frames[m_index]->getContentSize().height));
        }
    }

protected:
    CCGIFAnimatedSprite() : m_index(0), m_elapsed(0) {}
    //~CCGIFAnimatedSprite() {}

private:
    std::vector<CCTexture2D*> m_frames;
    std::vector<float> m_delays;
    size_t m_index;
    float m_elapsed;
};

bool isGif(const char* filename) {
    std::string_view name = filename;
    if (name.size() > 4 && name.substr(name.size() - 4) == ".gif") return true;

    auto path = CCFileUtils::get()->fullPathForFilename(filename, 0);
    std::ifstream file_stream(path.c_str(), std::ios::binary);
    if (!file_stream) return false;

    char header[6];
    file_stream.read(header, 6);
    return std::memcmp(header, "GIF87a", 6) == 0 or std::memcmp(header, "GIF89a", 6) == 0;
}

#include <Geode/modify/CCSprite.hpp>
class $modify(CCSprite) {
public:
    static CCSprite* create(const char* pszFileName) {
        if (isGif(pszFileName)) return CCGIFAnimatedSprite::create(pszFileName);
        return CCSprite::create(pszFileName);
    }
};