# Правила работы с ImGui в этом проекте

## Версия зафиксирована: 1.48 WIP

```
ImGui/imgui.h:19   #define IMGUI_VERSION "1.48 WIP"
```

Это **старая** версия (2015 год). Почти все примеры кода в интернете и в
памяти моделей написаны для 1.6x–1.9x, и их синтаксис здесь не работает.

## Ошибка, которая уже случилась

```
error C2065: ImGuiTreeNodeFlags_DefaultOpen: необъявленный идентификатор
```

Причина: перечисления `ImGuiTreeNodeFlags_*` появились только в 1.51.
В 1.48 их нет вообще.

## Как правильно в 1.48

### Заголовок, открытый по умолчанию

```cpp
// НЕЛЬЗЯ (синтаксис новых версий):
ImGui::CollapsingHeader("Label", ImGuiTreeNodeFlags_DefaultOpen)

// НУЖНО — 4-й аргумент:
ImGui::CollapsingHeader(label, str_id, display_frame, default_open)
ImGui::CollapsingHeader("Entity mutations", "entmut", true, true)
```

Полная сигнатура из `imgui.h:250`:

```cpp
bool CollapsingHeader(const char* label, const char* str_id = NULL,
                      bool display_frame = true, bool default_open = false);
```

### Дерево

```cpp
if (ImGui::TreeNode("Recon tools")) {
    ...
    ImGui::TreePop();      // обязательно, ровно один на каждый TreeNode
}
```

В 1.48 нет `TreeNodeEx` с флагами — только простая форма.

## Второе правило: только ASCII в строках UI

Встроенный шрифт ImGui (ProggyClean) содержит **латиницу и базовую
пунктуацию**. Всё остальное рисуется как `?`.

Отсюда и `?????? ???? ?????` в панели: это были русские строки.

Под запрет попадают не только кириллица, но и «типографские» символы,
которые легко занести копипастом:

| символ | что вместо |
|---|---|
| `—` `–` (тире) | `-` |
| `«` `»` | `"` |
| `…` | `...` |
| любая кириллица | английский текст |

**Правило: весь текст, попадающий в ImGui, пишем по-английски и только
ASCII.** Комментарии в коде и записи в лог — можно по-русски: лог пишется
в UTF-8 файл и читается нормально.

Важно помнить, что в UI попадает не только явный `ImGui::Text(...)`, но и
строки статуса: `EnemyTuner::StatusLine()` рисуется в панели, поэтому все
`lstrcpynA(s_status, ...)` и `sprintf_s` для него — тоже английские.

## Как проверить перед сборкой

Быстрая проверка не-ASCII во всех UI-вызовах:

```bash
python3 - <<'PY'
import re, glob
pat = re.compile(r'ImGui::(?:Text|Button|TextColored|TextWrapped|TextDisabled'
                 r'|CollapsingHeader|TreeNode|Checkbox|SliderFloat|RadioButton'
                 r'|Selectable|MenuItem|BeginMenu|LabelText|BulletText)')
for p in glob.glob('src/**/*.cpp', recursive=True):
    for i, l in enumerate(open(p, encoding='utf-8'), 1):
        code = l.split('//')[0]
        if pat.search(code) and any(ord(c) > 127 for c in code):
            print(p, i, code.strip()[:80])
PY
```

Проверка сигнатур ImGui без Visual Studio (`/home/user/tcomp/imgui/t.cpp`):

```bash
g++ -std=c++11 -fsyntax-only -I<репо>/ImGui t.cpp
```

Компилируется настоящий `imgui.h` из проекта, поэтому несуществующие
константы и неверные сигнатуры вылезают сразу — не дожидаясь MSVC.

Обычный харнесс (`/home/user/tcomp/check.sh`) `DevTools.cpp` не покрывает:
там ImGui и DirectX. Для UI-кода используем проверку выше.
