#pragma once

#include <vector>
#include <cocos2d.h>
#include <Geode/utils/casts.hpp>

//its only member reference and cast helper...
class CCGIFAnimatedSprite : public cocos2d::CCSprite {
public:

    static CCGIFAnimatedSprite* create(const char* file) {
        return geode::cast::typeinfo_cast<CCGIFAnimatedSprite*>(
            cocos2d::CCSprite::create(file)
        );
    }

    // Frame textures
    const std::vector<cocos2d::CCTexture2D*>& getFrames() const { return m_frames; }
    // Delays between frames
    const std::vector<float>& getDelays() const { return m_delays; }
    // Current frame index
    size_t getCurrentIndex() const { return m_index; }
    // Elapsed time since last frame
    float getElapsed() const { return m_elapsed; }

    std::vector<cocos2d::CCTexture2D*> m_frames;     // Frame textures
    std::vector<float> m_delays;            // Delays between frames
    size_t m_index;                         // Current frame index
    float m_elapsed;                        // Elapsed time since last frame
};
