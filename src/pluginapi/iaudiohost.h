#ifndef IAUDIOHOST_H
#define IAUDIOHOST_H

#include <span>

// The playing song's decoded audio, for a plugin that shows it (a
// visualization). Each plugin gets its own. Called on the main thread.
class IAudioHost {
public:
  virtual ~IAudioHost() = default;

  // Whether the plugin wants audio, for example while its panel is shown. The
  // host copies decoded audio only while some plugin wants it, and forgets it
  // when none does.
  virtual void setWanted(bool wanted) = 0;
  // Fills out with the mono samples, in [-1, 1], that end at the estimated
  // play position: the audio being heard now. Zeros where there is none.
  // Returns their sample rate, or 0 when no audio overlaps them. The host keeps
  // the last 2 s of audio, and none while no plugin wants it.
  virtual int read(std::span<float> out) const = 0;
};

#endif // IAUDIOHOST_H
