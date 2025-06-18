#pragma once

#include <Geode/utils/cocos.hpp>

#if !defined(_GIF_LIB_H_)

#define gifbool unsigned char
typedef unsigned char GifByteType;
typedef int GifWord;

typedef struct GifColorType {
    GifByteType Red, Green, Blue;
} GifColorType;

typedef struct ColorMapObject {
    int ColorCount;
    int BitsPerPixel;
    gifbool SortFlag;
    GifColorType* Colors;    /* on malloc(3) heap */
} ColorMapObject;

typedef struct GifImageDesc {
    GifWord Left, Top, Width, Height;   /* Current image dimensions. */
    gifbool Interlace;                     /* Sequential/Interlaced lines. */
    ColorMapObject* ColorMap;           /* The local color map */
} GifImageDesc;


NS_CC_BEGIN;

//its only member reference and cast helper...
class CCGIFAnimatedSprite : public CCSprite { //728bytes
public:
    class GIFFrame : public CCObject {
    public:
        CCTexture2D* m_texture = nullptr;
        float m_delay = 0.1f;
        GifImageDesc imageDesc;
        int m_disposalMethod = 0;
        int m_transparentColorIndex = -1;
    };

    static CCGIFAnimatedSprite* create(const char* file) {
        auto spr = CCSprite::create(file);
        auto cast = geode::cast::typeinfo_cast<CCGIFAnimatedSprite*>(spr);
        return cast;
    }

    void play() { m_isPlaying = true; }
    void pause() { m_isPlaying = false; }
    void stop() { m_isPlaying = false; m_currentFrame = 0; }
    void setLoop(bool loop) { m_loop = loop; }
    bool isPlaying() const { return m_isPlaying; }

    unsigned int getCurrentFrame() const { return m_currentFrame; }
    void setCurrentFrame(unsigned int frame) {
        if (!m_frames or frame >= m_frames->count()) return;

        m_currentFrame = frame;
        m_frameTimer = 0.0f;

        GIFFrame* targetFrame = geode::cast::typeinfo_cast<GIFFrame*>(m_frames->objectAtIndex(frame));
        if (targetFrame and targetFrame->m_texture) {
            setTexture(targetFrame->m_texture);
        }
    }

    unsigned int getFrameCount() const { return m_frames ? m_frames->count() : 0; }

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
};

NS_CC_END;

#endif