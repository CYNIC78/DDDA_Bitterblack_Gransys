// Production PresetManager.cpp с in-memory INI: Init не имеет права писать.
#include <map>
#include <string>
#include <cassert>
#include <cmath>
struct iniConfig {
    std::map<std::string, float> floats;
    int writes = 0;
    float getFloat(const char* section, const char* key, float fallback) {
        std::string k = std::string(section) + "/" + key;
        return floats.count(k) ? floats[k] : fallback;
    }
    int getInt(const char*, const char*, int fallback) { return fallback; }
    void setFloat(const char* section, const char* key, float value) {
        floats[std::string(section) + "/" + key] = value; ++writes;
    }
    void setInt(const char*, const char*, int) { ++writes; }
};
iniConfig config;
#include "../src/pawnai/PresetManager.cpp"
int main() {
    config.floats["customAnchor/nexus"] = 846.154053f;
    config.floats["customAnchor/guardian"] = 450.0f;
    PawnAI::PresetManager p;
    p.Init();
    assert(config.writes == 0);
    assert(std::fabs(p.anchor[I_NEXUS] - 846.154053f) < .01f);
    assert(std::fabs(config.floats["customAnchor/nexus"] - 846.154053f) < .01f);
    p.LoadPreset(5); // явный выбор Balanced, запись ожидается
    assert(config.writes > 0);
    assert(p.anchor[I_NEXUS] == 450.f);
    assert(config.floats["customAnchor/nexus"] == 450.f);
    config.floats["customAnchor/nexus"] = 900.f;
    config.writes = 0;
    p.Init(); // повторная инициализация после перезагрузки сейва
    assert(config.writes == 0 && p.anchor[I_NEXUS] == 900.f);
}
