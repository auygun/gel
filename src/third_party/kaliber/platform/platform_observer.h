#ifndef ENGINE_PLATFORM_PLATFORM_OBSERVER_H
#define ENGINE_PLATFORM_PLATFORM_OBSERVER_H

namespace eng {

class InputEvent;

class PlatformObserver {
 public:
  virtual ~PlatformObserver() = default;

  virtual void OnWindowCreated() = 0;
  virtual void OnWindowDestroyed() = 0;
  virtual void OnFramebufferResized(int width, int height) = 0;
  virtual void LostFocus() = 0;
  virtual void GainedFocus() = 0;
};

}  // namespace eng

#endif  // ENGINE_PLATFORM_PLATFORM_OBSERVER_H
