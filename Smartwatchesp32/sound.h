#ifndef _sound_h
#define _sound_h

// Sound is disabled — no speaker on this build.
// All call sites are kept so the rest of the code compiles unchanged.

inline void sound_init()  {}
inline void playSound(const uint8_t* /*snd*/, uint8_t /*len*/) {}

// Dummy sound data (zero-length) so the linker is happy with any extern refs
constexpr uint8_t shoot_snd[]    = {};
constexpr uint8_t SHOOT_SND_LEN  = 0;
constexpr uint8_t get_key_snd[]  = {};
constexpr uint8_t GET_KEY_SND_LEN = 0;
constexpr uint8_t hit_wall_snd[] = {};
constexpr uint8_t HIT_WALL_SND_LEN = 0;
constexpr uint8_t walk1_snd[]    = {};
constexpr uint8_t WALK1_SND_LEN  = 0;
constexpr uint8_t walk2_snd[]    = {};
constexpr uint8_t WALK2_SND_LEN  = 0;
constexpr uint8_t medkit_snd[]   = {};
constexpr uint8_t MEDKIT_SND_LEN = 0;

// The original sound.h used a global 'sound' bool to gate walk sounds.
// Provide it as always-false so the walk-sound branches compile but never fire.
static bool sound = false;

#endif // _sound_h
