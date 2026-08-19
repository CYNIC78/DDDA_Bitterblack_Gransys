/**
 * CameraPlus.cpp — Tactical Camera + Pause + Disable Auto-Correction (v2.2)
 *
 * СТАБИЛЬНОСТЬ:
 * - FlyThread пишет в память камеры только под SEH
 * - Шатдаун через событие (не через WaitForSingleObject в DllMain)
 * - Все хоткеи проверяют активный геймплей
 */

#include "stdafx.h"
#include "runtime/Runtime.h"
#include "CameraPlus.h"
#include <windows.h>

static BYTE* g_camPos    = nullptr;
static BYTE* g_camOrient = nullptr;
static bool  g_freeCam  = false;
static bool  g_freeFly  = false;
static float g_flySpd   = 2.0f;
static float g_flySpdZ  = 2.0f;
static bool  g_paused   = false;
static float g_pauseSpd = 0.0001f;
static BYTE* g_pSpeedObj = nullptr;
static bool  g_noAutoCorrect = false;

// Build 68 — Pawn Cam: камера следует за пешкой (позиция пешки, взгляд игрока).
static bool  g_pawnCam   = false;
// Build 69.3: РЕЖИМ включён — это ещё не значит, что мы ведём камеру.
//
// Хук HDetach глушит покадровое обновление камеры движком. Раньше он смотрел
// на g_pawnCam, то есть на сам факт включённого режима. Но сразу после
// загрузки карты тел ещё нет: движок уже отключён, а мы ещё не пишем —
// камера ничья и висит в пространстве. Теперь хук смотрит на этот флаг,
// который поднимается только тогда, когда позиции резолвнуты и мы реально
// пишем координаты каждый кадр.
static bool  g_pawnCamDriving = false;
// Диагностика режима: что видит камера прямо сейчас. Показывается в панели,
// чтобы «камера болтается» превращалось в конкретные факты, а не в гипотезы.
static bool  g_dbgHavePawn = false;
static bool  g_dbgHaveArisen = false;
static float g_pawnCamHeight = 150.0f;  // см, подъём камеры над головой пешки
// Доля оставшегося расстояния за итерацию камеры (~5 мс). Подробности —
// у места применения: это вес в перетягивании камеры с движком, а не просто
// сглаживание. Выше 0.05 начинается гостинг, 1.0 = «сюрреализм».
static float g_pawnCamFollow = 0.01f;
static float g_pawnCamBias   = 1.0f;    // 0=у Аризена, 0.5=между, 1=у пешки (Party Cam)
static bool  g_pawnCamInit = false;     // первая постановка — без lerp (мгновенно)
// Build 69.1: сглаживание САМОГО bias, а не позиции.
//
// ЗАЧЕМ. Вход в режим делал снап (g_pawnCamInit=false -> cX=tX), а выход
// отдавал камеру движку, который возвращает её плавно. Отсюда асимметрия:
// «к пешке — мгновенно, к ГГ — плавно». Позиционный лерп трогать нельзя,
// L=0.01 подобран по отсутствию гостинга. Поэтому едет точка блендинга:
// на входе bias стартует с 0 (камера уже у Аризена, снап невидим) и плавно
// доезжает до заданного. Обе стороны теперь ведут себя одинаково.
static float g_pawnCamBiasCur  = 0.0f;  // фактический bias, догоняет заданный
static float g_pawnCamBiasEase = 0.20f; // 0..1 за итерацию (~5 мс)
                                        // 0.20 — примерно как возврат камеры движком
static bool  g_pawnCamAutoCorrectOff = false; // мы отключили автокоррекцию ради pawn cam
static DWORD g_camDumpLast = 0;   // rate-limit дампа объекта камеры (Build 68.3)
static int   g_camDumpCount = 0;
static HANDLE g_flyThread = nullptr;
static volatile bool g_flyThreadStop = false;
static HANDLE g_flyEvent = nullptr;  // для пробуждения без Sleep-ожидания

// Кастомные хоткеи (из .ini)
static int g_hkFreeCam = VK_MBUTTON; // был F4, теперь СКМ (средняя кнопка)
static int g_hkPause   = VK_NUMPAD0;
static int g_hkSpeedUp = VK_ADD;
static int g_hkSpeedDn = VK_SUBTRACT;
static int g_hkPawnCam = VK_NUMPAD1; // Pawn Cam toggle

// ═══ HУК 1: ЗАХВАТ КАМЕРЫ ═══

static LPBYTE pHkCam1, oHkCam1;

void __declspec(naked) HCam1()
{
    __asm
    {
        mov [g_camPos], esi
        movss xmm5, [esi + 0x14]
        jmp oHkCam1
    }
}

static LPBYTE pHkCam2, oHkCam2;

void __declspec(naked) HCam2()
{
    __asm
    {
        mov [g_camOrient], esi
        movss xmm1, [esi + 0x114]
        jmp oHkCam2
    }
}

// ═══ HУК 3: DETACH ═══

static LPBYTE pHkDetach, oHkDetach;

void __declspec(naked) HDetach()
{
    __asm
    {
        cmp byte ptr [g_freeCam], 1
        je skipUpdate
        cmp byte ptr [g_pawnCamDriving], 1
        je skipUpdate
        movss xmm0, [ecx + 0x20]
        addss xmm0, [ecx + 0x10]
        movss dword ptr [ecx + 0x10], xmm0
        movss xmm0, [ecx + 0x24]
        addss xmm0, [ecx + 0x14]
        movss dword ptr [ecx + 0x14], xmm0
        movss xmm0, [ecx + 0x28]
        addss xmm0, [ecx + 0x18]
        movss dword ptr [ecx + 0x18], xmm0
    skipUpdate:
        ret 4
    }
}

// ═══ HУК 4: ПАУЗА ═══

static LPBYTE pHkSpeed, oHkSpeed;

void __declspec(naked) HSpeed()
{
    __asm
    {
        mov [g_pSpeedObj], edx
        cmp byte ptr [g_paused], 1
        je doPause
        movss xmm1, [edx + 0x24]
        jmp oHkSpeed
    doPause:
        movss xmm1, [g_pauseSpd]
        movss dword ptr [edx + 0x24], xmm1
        jmp oHkSpeed
    }
}

// ═══ DISABLE AUTO-CORRECTION ═══

static LPBYTE pAutoV, pAutoH;
static BYTE  origAutoV[7], origAutoH[7];

void ApplyAutoCorrect(bool disable)
{
    DWORD old;
    if (pAutoV) {
        VirtualProtect(pAutoV, 7, PAGE_EXECUTE_READWRITE, &old);
        if (disable) {
            pAutoV[0]=0x83; pAutoV[1]=0xFE; pAutoV[2]=0x00;
            pAutoV[3]=0x90; pAutoV[4]=0x90; pAutoV[5]=0x90; pAutoV[6]=0x90;
        } else memcpy(pAutoV, origAutoV, 7);
        VirtualProtect(pAutoV, 7, old, &old);
    }
    if (pAutoH) {
        VirtualProtect(pAutoH, 7, PAGE_EXECUTE_READWRITE, &old);
        if (disable) {
            pAutoH[0]=0x83; pAutoH[1]=0xFA; pAutoH[2]=0x00;
            pAutoH[3]=0x90; pAutoH[4]=0x90; pAutoH[5]=0x90; pAutoH[6]=0x90;
        } else memcpy(pAutoH, origAutoH, 7);
        VirtualProtect(pAutoH, 7, old, &old);
    }
}

// ═══ ПОТОК ═══

static DWORD WINAPI FlyThread(LPVOID)
{
    bool f4w=false, n0w=false, plw=false, mnw=false, pcw=false;

    while (!g_flyThreadStop) {
        // Ждём с таймаутом 5 мс — но при шатдауне пробуждаемся мгновенно через событие
        WaitForSingleObject(g_flyEvent, 5);

        __try {
            bool f4 = (GetAsyncKeyState(g_hkFreeCam)&0x8000) && !(GetAsyncKeyState(VK_MENU)&0x8000);
            if (f4 && !f4w) { g_freeCam=!g_freeCam; g_freeFly=g_freeCam; }
            f4w=f4;
            bool n0 = GetAsyncKeyState(g_hkPause)&0x8000;
            if (n0 && !n0w) { g_paused=!g_paused; if(!g_paused&&g_pSpeedObj)*(float*)(g_pSpeedObj+0x24)=1.0f; }
            n0w=n0;
            bool pl = GetAsyncKeyState(g_hkSpeedUp)&0x8000;
            if (pl&&!plw) { float s=g_pauseSpd*2.0f; if(s>1.0f)s=1.0f; g_pauseSpd=s; }
            plw=pl;
            bool mn = GetAsyncKeyState(g_hkSpeedDn)&0x8000;
            if (mn&&!mnw) { float s=g_pauseSpd/2.0f; if(s<0.00001f)s=0.00001f; g_pauseSpd=s; }
            mnw=mn;

            // Build 68: Pawn Cam — отдельный режим, независимый от freeFly.
            bool pc = (GetAsyncKeyState(g_hkPawnCam)&0x8000);
            if (pc && !pcw) {
                g_pawnCam = !g_pawnCam;
                g_pawnCamInit = false;
                g_pawnCamBiasCur = 0.0f;  // входим от Аризена и плавно едем к пешке
                if (!g_pawnCam) g_pawnCamDriving = false;  // камеру сразу движку
                g_camDumpCount = 0;   // сброс дампа при переключении
                g_camDumpLast = 0;
                // Отключить авто-возврат камеры к игроку, иначе игра каждый кадр
                // тянет камеру к ГГ, а мы — к пешке (борьба двух писателей =
                // метание и «госты»). Включаем только если ini-флаг noAutoCorrect
                // сам этого не сделал.
                // Build 69.2: автокоррекцию НЕ трогаем прямо здесь.
                //
                // Раньше на включении мы сразу патчили движок (камера больше
                // не тянется к ГГ), но если тела ещё не найдены — сами тоже
                // ничего не пишем. Камера оставалась ничьей и висела в
                // пространстве после загрузки карты. Теперь движок отдаёт
                // камеру только тогда, когда нам есть куда её вести
                // (см. ниже, по факту резолва позиций).
                if (!g_pawnCam && g_pawnCamAutoCorrectOff) {
                    ApplyAutoCorrect(false);
                    g_pawnCamAutoCorrectOff = false;
                }
            }
            pcw = pc;

            if (!g_camPos) continue;

            // Страховка от рассинхрона: режим мог выключиться любым путём
            // (хоткей, чекбокс, перезагрузка конфига). Ведение снимаем всегда.
            if (!g_pawnCam && g_pawnCamDriving) g_pawnCamDriving = false;

            // --- Pawn Cam / Party Cam: камера на blend(Аризен, пешка, bias) ---
            if (g_pawnCam) {
                float px=0, py=0, pz=0, ax=0, ay=0, az=0;
                bool havePawn   = Runtime::GetMainPawnWorldPos(&px, &py, &pz);
                bool haveArisen = Runtime::GetArisenWorldPos(&ax, &ay, &az);
                g_dbgHavePawn = havePawn; g_dbgHaveArisen = haveArisen;

                // Build 69.4: ведём камеру только если есть ТЕ тела, которые
                // нужны текущему bias. Раньше при потерянной пешке мы всё
                // равно забирали камеру и сажали её на Аризена — со стороны
                // это выглядит как «камера прилипла к игроку со смещением»,
                // то есть как сломанная фича.
                const float wantB = (g_pawnCamBias < 0) ? 0 : (g_pawnCamBias > 1 ? 1 : g_pawnCamBias);
                const bool needPawn   = wantB > 0.01f;
                const bool needArisen = wantB < 0.99f;
                if ((needPawn && !havePawn) || (needArisen && !haveArisen)) {
                    havePawn = haveArisen = false;   // ниже отдадим камеру движку
                }

                if (!havePawn && !haveArisen) {
                    // Тел ещё нет (только загрузились) или мир выгружен.
                    // Полностью возвращаем камеру движку: снимаем и ведение
                    // (иначе хук HDetach глушит его покадровое обновление и
                    // камера висит в пустоте), и патч автокоррекции.
                    // Как только тела найдутся, режим включится сам.
                    g_pawnCamDriving = false;
                    if (g_pawnCamAutoCorrectOff) {
                        ApplyAutoCorrect(false);
                        g_pawnCamAutoCorrectOff = false;
                    }
                    g_pawnCamInit = false;      // войдём заново, уже по месту
                    g_pawnCamBiasCur = 0.0f;
                    continue;
                }

                // Позиция есть — теперь можно забирать камеру у движка.
                // Флаг поднимаем ДО первой записи, иначе движок успеет
                // применить свою дельту поверх нашей и получится дрожь.
                g_pawnCamDriving = true;
                if (!g_pawnCamAutoCorrectOff && !g_noAutoCorrect) {
                    ApplyAutoCorrect(true);
                    g_pawnCamAutoCorrectOff = true;
                }
                {
                    // если одной точки нет — берём другую целиком
                    float srcX, srcY, srcZ;
                    if (havePawn && haveArisen) {
                        float want = g_pawnCamBias;
                        if (want < 0) want = 0; if (want > 1) want = 1;
                        // Build 69.1: едет bias, а не камера. Так вход в режим
                        // и выход из него выглядят одинаково плавно.
                        float e = g_pawnCamBiasEase;
                        if (e < 0.001f) e = 0.001f; if (e > 1.0f) e = 1.0f;
                        g_pawnCamBiasCur += (want - g_pawnCamBiasCur) * e;
                        if (want - g_pawnCamBiasCur < 0.001f &&
                            g_pawnCamBiasCur - want < 0.001f) g_pawnCamBiasCur = want;
                        const float b = g_pawnCamBiasCur;
                        srcX = ax + (px - ax) * b;
                        srcY = az + (pz - az) * b;   // камера: Y = world Z
                        srcZ = ay + (py - ay) * b;   // камера: Z(высота) = world Y
                    } else if (havePawn) {
                        // Аризена не видно: работаем от пешки. Bias подтягиваем к 1,
                        // иначе при появлении Аризена камера прыгнет.
                        g_pawnCamBiasCur = 1.0f;
                        srcX = px; srcY = pz; srcZ = py;
                    } else {
                        g_pawnCamBiasCur = 0.0f;
                        srcX = ax; srcY = az; srcZ = ay;
                    }
                    // Build 68.5: откат на запись в CURRENT (как в 68.3 — работает).
                    // Target-подход (68.4) дал статичную камеру: current не ехал к
                    // найденному target (видимо, интерполяцию ведёт другой потребитель).
                    // Smoothness ЗАХАРДКОЖЕН на 0.01 — только там нет гостинга.
                    float& cX = *(float*)(g_camPos+0x10);
                    float& cZ = *(float*)(g_camPos+0x14); // высота
                    float& cY = *(float*)(g_camPos+0x18);
                    float tX = srcX;
                    float tY = srcY;
                    float tZ = srcZ + g_pawnCamHeight;
                    if (!g_pawnCamInit) {
                        // Телепорт только если камера реально далеко (вышли из
                        // free-fly, сменилась локация). При обычном входе из
                        // третьего лица она в паре метров — тогда доезжаем
                        // плавно, иначе получался тот самый рывок «на голову».
                        const float dx = tX - cX, dy = tY - cY, dz = tZ - cZ;
                        const float d2 = dx*dx + dy*dy + dz*dz;
                        const float kNear = 1500.0f;             // 15 м в см
                        if (d2 > kNear * kNear) { cX = tX; cY = tY; cZ = tZ; }
                        g_pawnCamInit = true;
                    } else {
                        // L — доля оставшегося расстояния за итерацию (~5 мс).
                        // Это НЕ просто плавность: движок продолжает писать
                        // камеру сам, и L задаёт наш вес в этом перетягивании.
                        // При L=1 мы каждый кадр телепортируем камеру в цель,
                        // движок тянет обратно — отсюда «сюрреализм» и гостинг.
                        // Малое L даёт устойчивое равновесие двух писателей.
                        float L = g_pawnCamFollow;
                        if (L < 0.002f) L = 0.002f; if (L > 0.5f) L = 0.5f;
                        cX += (tX - cX) * L;
                        cY += (tY - cY) * L;
                        cZ += (tZ - cZ) * L;
                    }
                }
                continue;
            }

            if (!g_freeFly || !g_freeCam) continue;
            if (!g_camOrient) continue;

            // SEH-защита записи в память камеры
            float& X=*(float*)(g_camPos+0x10);
            float& Z=*(float*)(g_camPos+0x14);
            float& Y=*(float*)(g_camPos+0x18);
            float s=*(float*)(g_camOrient+0xB0);
            float c=*(float*)(g_camOrient+0x110);
            if (GetAsyncKeyState(VK_UP)   &0x8000) { X+=s*g_flySpd; Y-=c*g_flySpd; }
            if (GetAsyncKeyState(VK_DOWN) &0x8000) { X-=s*g_flySpd; Y+=c*g_flySpd; }
            if (GetAsyncKeyState(VK_LEFT) &0x8000) { X-=c*g_flySpd; Y-=s*g_flySpd; }
            if (GetAsyncKeyState(VK_RIGHT)&0x8000) { X+=c*g_flySpd; Y+=s*g_flySpd; }
            if (GetAsyncKeyState(VK_PRIOR)&0x8000) Z+=g_flySpdZ;
            if (GetAsyncKeyState(VK_NEXT) &0x8000) Z-=g_flySpdZ;
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            // Камера пересоздалась — ничего не пишем, следующий тик восстановит
        }
    }
    return 0;
}

// ═══ UI ═══

void RenderCameraUI()
{
    if (!ImGui::CollapsingHeader("Camera Plus")) return;
    ImGui::PushID("CamPlus");

    if (ImGui::Checkbox("Tactical Camera (MMB)", &g_freeCam))
        config.setBool("camera","freeCam",g_freeCam);
    ImGui::TextWrapped("Freezes in place, tracks player. Arrows to reposition.");

    if (ImGui::Checkbox("Manual Fly (arrows/PgUp/PgDn)", &g_freeFly))
        config.setBool("camera","freeFly",g_freeFly);

    ImGui::Separator();
    // Build 68: Party Cam (Pawn Cam v2)
    if (ImGui::Checkbox("Party Cam (pawn/pc camera)", &g_pawnCam)) {
        config.setBool("camera", "pawnCam", g_pawnCam);
        g_pawnCamInit = false;    // постановка в точку Аризена (камера уже там)
        g_pawnCamBiasCur = 0.0f;  // и плавный отъезд к пешке
        if (!g_pawnCam) g_pawnCamDriving = false;
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Camera positions between Arisen and pawn (your view direction). Hotkey: NumPad 1.");
    if (g_pawnCam) {
        if (ImGui::SliderFloat("Bias (0=Arisen, 0.5=mid, 1=pawn)", &g_pawnCamBias, 0.0f, 1.0f, "%.2f"))
            config.setFloat("camera", "pawnCamBias", g_pawnCamBias);
        if (ImGui::SliderFloat("Camera height", &g_pawnCamHeight, 0.0f, 400.0f, "%.0f cm"))
            config.setFloat("camera", "pawnCamHeight", g_pawnCamHeight);
        if (ImGui::SliderFloat("Bias ease (transition speed)", &g_pawnCamBiasEase,
                               0.002f, 0.20f, "%.3f"))
            config.setFloat("camera", "pawnCamBiasEase", g_pawnCamBiasEase);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("How fast the camera travels between Arisen and pawn. "
                              "0.002 = slow drift, 0.20 = about as fast as the engine's "
                              "own return to the player. Applies both ways.");
        ImGui::Text("bias now %.2f -> %.2f", g_pawnCamBiasCur, g_pawnCamBias);
        ImGui::TextColored(g_pawnCamDriving ? ImVec4(0.3f,1,0.3f,1) : ImVec4(1,0.6f,0.3f,1),
            "state: %s | pawn %s | arisen %s | autocorrect %s",
            g_pawnCamDriving ? "DRIVING" : "engine owns camera",
            g_dbgHavePawn ? "ok" : "--",
            g_dbgHaveArisen ? "ok" : "--",
            g_pawnCamAutoCorrectOff ? "patched off" : "vanilla");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("If the camera hangs and this says 'engine owns camera', "
                              "something other than our code holds it. If it says "
                              "'DRIVING' with no bodies, that is our bug.");
        if (ImGui::SliderFloat("Follow weight (vs engine)", &g_pawnCamFollow,
                               0.002f, 0.05f, "%.3f"))
            config.setFloat("camera", "pawnCamFollow", g_pawnCamFollow);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Our share of the camera each frame. The engine keeps "
                              "writing it too, so this is a tug-of-war weight, not "
                              "plain smoothing. Above ~0.05 it starts ghosting.");
    }

    ImGui::PushItemWidth(150);
    ImGui::SliderFloat("Speed XY", &g_flySpd, 0.5f, 30.0f, "%.1f");
    ImGui::SliderFloat("Speed Z", &g_flySpdZ, 0.5f, 30.0f, "%.1f");
    ImGui::PopItemWidth();

    if (g_camPos) ImGui::Text("X=%.0f Z=%.0f Y=%.0f",
        *(float*)(g_camPos+0x10),*(float*)(g_camPos+0x14),*(float*)(g_camPos+0x18));

    ImGui::Separator();

    ImGui::TextColored(ImVec4(0.6f,0.9f,1,1),"Camera Behavior");
    bool wasAuto = g_noAutoCorrect;
    if (ImGui::Checkbox("Disable Auto-Correction", &g_noAutoCorrect)) {
        config.setBool("camera","noAutoCorrect",g_noAutoCorrect);
        if (wasAuto != g_noAutoCorrect) ApplyAutoCorrect(g_noAutoCorrect);
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Camera won't auto-return behind player.\\nIn F4 mode creates 'tracking' effect.");
    ImGui::SameLine();
    ImGui::TextDisabled("(dinp8)");

    ImGui::Separator();

    if (ImGui::Checkbox("Pause (Num0)", &g_paused)) {
        config.setBool("camera","pause",g_paused);
        if (!g_paused && g_pSpeedObj) *(float*)(g_pSpeedObj+0x24)=1.0f;
    }
    ImGui::PushItemWidth(200);
    ImGui::SliderFloat("Speed", &g_pauseSpd, 0.00001f, 1.0f, "%.5f", 3.0f);
    ImGui::PopItemWidth();
    if (g_paused) ImGui::TextColored(ImVec4(1,0.5f,0.3f,1),"PAUSED");
    ImGui::PopID();
}

// ═══ INIT ═══

void Hooks::CameraPlus()
{
    g_freeCam  = config.getBool("camera","freeCam",false);
    g_freeFly  = config.getBool("camera","freeFly",false);
    g_flySpd   = config.getFloat("camera","flySpeed",2.0f);
    g_flySpdZ  = config.getFloat("camera","flySpeedZ",2.0f);
    g_paused   = config.getBool("camera","pause",false);
    g_pauseSpd = config.getFloat("camera","pauseSpeed",0.0001f);
    g_noAutoCorrect = config.getBool("camera","noAutoCorrect",false);
    g_pawnCam       = config.getBool("camera","pawnCam",false);
    g_pawnCamHeight = config.getFloat("camera","pawnCamHeight",150.0f);
    g_pawnCamFollow = config.getFloat("camera","pawnCamFollow",0.01f);
    g_pawnCamBias   = config.getFloat("camera","pawnCamBias",1.0f);
    g_pawnCamBiasEase = config.getFloat("camera","pawnCamBiasEase",0.20f);

    // Кастомные хоткеи из .ini (camera_keys)
    g_hkFreeCam = config.getUInt("camera_keys","freeCam", VK_MBUTTON) & 0xFF;
    g_hkPause   = config.getUInt("camera_keys","pause",   VK_NUMPAD0) & 0xFF;
    g_hkSpeedUp = config.getUInt("camera_keys","speedUp", VK_ADD) & 0xFF;
    g_hkSpeedDn = config.getUInt("camera_keys","speedDn", VK_SUBTRACT) & 0xFF;
    g_hkPawnCam = config.getUInt("camera_keys","pawnCam", VK_NUMPAD1) & 0xFF;

    BYTE s1[]={0xF3,0x0F,0x10,0x6E,0x14,0x0F,0xC6,0xED,0x00,0xF3,0x0F,0x11,0x84,0x24,0xCC,0x00,0x00,0x00};
    if (Hooks::FindSignature("Cam1",s1,&pHkCam1)) {
        Hooks::CreateHook("Cam1",pHkCam1,HCam1,(LPVOID*)&oHkCam1,true); oHkCam1=pHkCam1+5;
    }

    BYTE s2[]={0xF3,0x0F,0x10,0x8E,0x14,0x01,0x00,0x00,0xF3,0x0F,0x10,0x86,0x10,0x01,0x00,0x00,0xF3,0x0F,0x10,0xA6,0x50,0x01,0x00,0x00};
    if (Hooks::FindSignature("Cam2",s2,&pHkCam2)) {
        Hooks::CreateHook("Cam2",pHkCam2,HCam2,(LPVOID*)&oHkCam2,true); oHkCam2=pHkCam2+8;
    }

    BYTE sd[]={0xF3,0x0F,0x10,0x41,0x20,0xF3,0x0F,0x58,0x41,0x10,0xF3,0x0F,0x11,0x41,0x10,0xF3,0x0F,0x10,0x41,0x24,0xF3,0x0F,0x58,0x41,0x14,0xF3,0x0F,0x11,0x41,0x14,0xF3,0x0F,0x10,0x41,0x28,0xF3,0x0F,0x58,0x41,0x18,0xF3,0x0F,0x11,0x41,0x18,0xC2,0x04,0x00};
    if (Hooks::FindSignature("Detach",sd,&pHkDetach))
        Hooks::CreateHook("Detach",pHkDetach,HDetach,(LPVOID*)&oHkDetach,true);

    BYTE ss[]={0xF3,0x0F,0x10,0x4A,0x24,0xF3,0x0F,0x59,0x49,0x70};
    if (Hooks::FindSignature("Speed",ss,&pHkSpeed)) {
        Hooks::CreateHook("Speed",pHkSpeed,HSpeed,(LPVOID*)&oHkSpeed,true); oHkSpeed=pHkSpeed+5;
    }

    BYTE sigV[]={0x80,0xBE,0xF0,0x02,0x00,0x00,0x00,0x0F,0x85,0xCC,0xCC,0x00,0x00};
    if (Hooks::FindSignature("AutoV",sigV,&pAutoV)) memcpy(origAutoV,pAutoV,7);

    BYTE sigH[]={0x80,0xBA,0xF1,0x02,0x00,0x00,0x00,0x0F,0x85,0xCC,0xCC,0x00,0x00};
    if (Hooks::FindSignature("AutoH",sigH,&pAutoH)) memcpy(origAutoH,pAutoH,7);

    if (g_noAutoCorrect) ApplyAutoCorrect(true);

    g_flyThreadStop=false;
    g_flyEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    g_flyThread=CreateThread(0,0,FlyThread,0,0,0);
    InGameUIAdd(RenderCameraUI);

    logFile << "CameraPlus v2.2: detach=" << (pHkDetach?1:0)
            << " speed=" << (pHkSpeed?1:0)
            << " autoV=" << (pAutoV?1:0) << " autoH=" << (pAutoH?1:0)
            << " freeCam=" << g_freeCam << std::endl;
}

void Hooks::CameraPlusShutdown()
{
    g_flyThreadStop=true;
    // Пробуждаем поток через событие — не ждём 5 мс
    if (g_flyEvent) SetEvent(g_flyEvent);
    // НЕ WaitForSingleObject — это может быть вызвано из DllMain!
    // Поток завершится сам (цикл проверяет g_flyThreadStop)
    if (g_flyEvent) { CloseHandle(g_flyEvent); g_flyEvent = nullptr; }
    if (g_flyThread) { CloseHandle(g_flyThread); g_flyThread = nullptr; }
    if (g_paused&&g_pSpeedObj)*(float*)(g_pSpeedObj+0x24)=1.0f;
    if (g_noAutoCorrect) ApplyAutoCorrect(false);
    g_pawnCamDriving = false;
    if (g_pawnCamAutoCorrectOff) { ApplyAutoCorrect(false); g_pawnCamAutoCorrectOff = false; }
}