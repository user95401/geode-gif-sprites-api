# GIF Sprites

Adds support for animated **GIF** files in `CCSprite::create()` for Geometry Dash using **giflib** v5.0.0!

## Usage

If the filename ends with `.gif` or header is `GIF87a` or `GIF89a` (you can use gif file named as `anim.png`), a `CCGIFAnimatedSprite` will be returned instead of a `CCSprite`.
The animation plays automatically and uses embedded frame delays.

```cpp
auto gif = CCSprite::create("animated.gif"); //returns CCGIFAnimatedSprite*
gif->setPosition(this->getContentSize() / 2);
this->addChild(gif, 10);
```

Or if you included `<user95401.gif-sprites/include/CCGIFAnimatedSprite.hpp>`:

```cpp
#include <user95401.gif-sprites/include/CCGIFAnimatedSprite.hpp>

auto gif = CCGIFAnimatedSprite::create("animated.gif");
gif->setPosition(this->getContentSize() / 2);
this->addChild(gif, 10);

//and you able to getElapsed and stuff
float gifTimeElapsed = gif->getElapsed();
```

Using texture pack (or any other resource modding ways) you can replace some files like `GJ_gradientBG.png`, just rename your `epic-anime-wallpaper.gif` exactly to `GJ_gradientBG.png`, mod detect it as long as this file is GIF87a or GIF89a.

## Features

- Hooks `CCSprite::create(const char*)` to support creating animated sprites from GIF file.
- Frame decoding with correct delays
- Automatic animation loop
- Shared caching on repeated loads
- Lightweight and early-load safe

## Integration

Add to your `mod.json` dependencies:

```json
"dependencies": {
	"user95401.gif-sprites": ">=v1.0.0"
}
```

And include headers (optional):

```cpp
#include <user95401.gif-sprites/include/CCGIFAnimatedSprite.hpp>
```