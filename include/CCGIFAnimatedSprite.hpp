#pragma once

#include <vector>
#include <cocos2d.h>
#include <Geode/utils/casts.hpp>

//its only member reference and cast helper)
class CCGIFAnimatedSprite : public cocos2d::CCSprite {
public:

    static CCGIFAnimatedSprite* create(std::string file) {
        return geode::cast::typeinfo_cast<CCGIFAnimatedSprite*>(
            cocos2d::CCSprite::create(file.c_str())
        );
    }

    const std::vector<cocos2d::CCTexture2D*>& getFrames() const { return m_frames; }
    const std::vector<float>& getDelays() const { return m_delays; }
    size_t getCurrentIndex() const { return m_index; }
    float getElapsed() const { return m_elapsed; }

    std::vector<cocos2d::CCTexture2D*> m_frames;
    std::vector<float> m_delays;
    size_t m_index;
    float m_elapsed;
};
