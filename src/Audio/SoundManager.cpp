
#include "SoundManager.hpp"
#include <vector>
#include <filesystem>

using namespace geode::prelude;

std::unordered_map<std::string, FMOD::Sound*> SoundManager::s_soundCache;
static std::unordered_map<std::string, std::vector<FMOD::Channel*>> s_activeChannels;
FMOD::ChannelGroup* SoundManager::s_sfxGroup = nullptr;

bool SoundManager::ensureChannelGroup(FMOD::System* sys) {
    if (!sys) return false;
    if (s_sfxGroup) return true;

    // Create a dedicated sound effects channel group for the mod.
    FMOD_RESULT res = sys->createChannelGroup("ModFrameSFXGroup", &s_sfxGroup);
    return (res == FMOD_OK && s_sfxGroup != nullptr);
}

void SoundManager::playSound(const std::string& path) {
    if (path.empty()) return;

    auto audioEngine = FMODAudioEngine::sharedEngine();
    if (!audioEngine || !audioEngine->m_system) return;

    // Skip playback when game sound effects are muted.
    if (audioEngine->m_sfxVolume <= 0.0f) return;

    FMOD::System* sys = audioEngine->m_system;
    if (!ensureChannelGroup(sys)) return;

    // Synchronize with the game's SFX volume.
    s_sfxGroup->setVolume(audioEngine->m_sfxVolume);

    // Resolve the audio path.
    std::filesystem::path audioPath(path);

    if (!audioPath.is_absolute()) {
        audioPath = std::filesystem::path(
            Mod::get()->getConfigDir().string()
        ) / audioPath;
    }

    audioPath = audioPath.lexically_normal();
    std::string fullPathStr = audioPath.string();

    FMOD::Sound* sound = nullptr;
    auto it = s_soundCache.find(fullPathStr);

    if (it != s_soundCache.end()) {
        sound = it->second;
    }
    else {
        // Only load sounds that exist.
        if (!std::filesystem::exists(audioPath)) return;

        FMOD_RESULT res = sys->createSound(
            fullPathStr.c_str(),
            FMOD_DEFAULT | FMOD_LOOP_OFF | FMOD_CREATESAMPLE,
            nullptr,
            &sound
        );

        if (res == FMOD_OK && sound) {
            sound->setMode(FMOD_LOOP_OFF);
            s_soundCache[fullPathStr] = sound;
        }
        else {
            return;
        }
    }

    if (sound) {
        sound->setMode(FMOD_LOOP_OFF);

        // Clean up channels that have finished playing.
        auto& channels = s_activeChannels[fullPathStr];

        for (auto chIt = channels.begin(); chIt != channels.end();) {
            bool isPlaying = false;

            if (*chIt &&
                (*chIt)->isPlaying(&isPlaying) == FMOD_OK &&
                isPlaying) {
                ++chIt;
            }
            else {
                chIt = channels.erase(chIt);
            }
        }

        // Stop the oldest channel if the voice limit is reached.
        while (channels.size() >= MAX_ACTIVE_VOICES) {
            if (channels.front()) {
                channels.front()->stop();
            }

            channels.erase(channels.begin());
        }

        // Play the new sound.
        FMOD::Channel* channel = nullptr;

        FMOD_RESULT playRes = sys->playSound(
            sound,
            s_sfxGroup,
            false,
            &channel
        );

        if (playRes == FMOD_OK && channel) {
            channel->setMode(FMOD_LOOP_OFF);
            channel->setLoopCount(0);
            channel->setPriority(0);
            channel->setVolumeRamp(false);

            channels.push_back(channel);
        }
    }
}

void SoundManager::stopAll() {
    if (s_sfxGroup) {
        s_sfxGroup->stop();
    }

    s_activeChannels.clear();
}

void SoundManager::clearCache() {
    stopAll();

    for (auto& [path, sound] : s_soundCache) {
        if (sound) {
            sound->release();
        }
    }

    s_soundCache.clear();
    s_activeChannels.clear();

    if (s_sfxGroup) {
        s_sfxGroup->release();
        s_sfxGroup = nullptr;
    }
}
