#pragma once
// Заглушка ImGui для проверок под g++.
//
// ЗАЧЕМ. Файлы с панелями (CombatIntel.cpp, PawnAI.cpp) не проходят проверку
// целиком, потому что настоящий ImGui тянет DirectX. Из-за этого их не видит
// ни один шаг syntax_check.sh — а именно там живёт логика урона и тика.
// Здесь ровно те вызовы, что встречаются в панелях: имена и формы совпадают,
// поведение не нужно (проверка только разбирает и связывает имена).
//
// ВАЖНО: это не ImGui. Ничего не рисует и не проверяет. Если код начнёт
// полагаться на ВОЗВРАТ этих функций, заглушка ничего не заметит.

struct ImVec2 {
    float x, y;
    ImVec2(float a = 0, float b = 0) : x(a), y(b) {}
};

struct ImVec4 {
    float x, y, z, w;
    ImVec4(float a = 0, float b = 0, float c = 0, float d = 0) : x(a), y(b), z(c), w(d) {}
};

namespace ImGui {

template <class... A> inline bool CollapsingHeader(const char*, A...) { return false; }
template <class... A> inline bool Checkbox(const char*, A...) { return false; }
template <class... A> inline bool TreeNode(const char*, A...) { return false; }
template <class... A> inline void Text(const char*, A...) {}
template <class... A> inline void TextColored(A...) {}
template <class... A> inline void TextDisabled(const char*, A...) {}
template <class... A> inline void TextWrapped(const char*, A...) {}
template <class... A> inline void BulletText(const char*, A...) {}
template <class... A> inline void SameLine(A...) {}
template <class... A> inline void Separator() {}
template <class... A> inline void ProgressBar(A...) {}
template <class... A> inline void PushID(A...) {}
template <class... A> inline void PopID() {}
template <class... A> inline void TreePop() {}

} // namespace ImGui
