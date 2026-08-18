#pragma once
/**
 * Runtime::Mem — фундамент рантайма: безопасное чтение/запись чужой памяти,
 * границы секций образа игры и резолв имени живого объекта через DTI.
 *
 * СЛОЙ: продукт. Работает ВСЕГДА, независимо от [devtools] enabled.
 * От него зависят и продуктовые модули (WorldScan, PartyRecon,
 * PriorityPlatform), и исследовательский DevTools.
 *
 * Правило слоя: здесь нет ни игровой логики, ни UI, ни файлов — только
 * доступ к памяти. Всё, что знает про пешек, врагов или приоритеты,
 * этому модулю не принадлежит.
 */

#include <windows.h>
#include <stdint.h>

namespace Runtime {
namespace Mem {

struct SecRange { uintptr_t lo, hi; };

extern uintptr_t g_base;
extern uint32_t g_imageSize;
extern SecRange g_exec[8];
extern int g_nExec;
extern SecRange g_rdata[8];
extern int g_nRdata;

bool Rd(const void* p, void* out, size_t n);

bool RdPtr(const void* p, uintptr_t* out);

bool WrSafe(void* p, const void* value, size_t n);

uintptr_t ImageEnd();

bool InImage(uintptr_t a);

bool LooksHeap(uintptr_t a);

// Save-layer check. Same idea as CombatIntel::IsInActiveGameplay.
// HUNT must not run on the title screen or mid-load.
bool InWorld();

void InitSections();

bool InExec(uintptr_t a);

bool InRdata(uintptr_t a);

// Vtable lives in rdata; first slots are methods in .text.
// InImage alone is too weak: 0x400000 (MZ) and patterned dwords leaked into census.
bool LooksLikeVtable(uintptr_t vt);

// Zip 34 — resolve a live object's class name without the atlas.
// MT Framework: every MtObject's vtable has GetDTI() early; the DTI card in
// .data holds a char* name at +4 (Zip 14 note). So: object -> vtable -> scan
// the first slots for a function that returns a .data pointer whose +4 is an
// ASCII class name. Cheaper and exact vs. extrapolating the 0x48 lattice.
bool ReadCStr(uintptr_t va, char* out, int cap);

// A DTI card: [0] = MtDTI vtable (in .rdata), [4] = char* name (in image).
bool NameFromDti(uintptr_t dti, char* out, int cap);

// Find the DTI for a live object by scanning its vtable for `mov eax, imm32;
// ret` (B8 imm32 C3) — that is GetDTI in this build.
uintptr_t DtiOfObject(uintptr_t obj);

bool NameOfLiveObject(uintptr_t obj, char* out, int cap);

const char* NameOfLiveObjectSafe(const void* obj, char* out, int cap);

// --- Статистика кэша имён (диагностика производительности) ---------------
// Связка vtable -> имя класса неизменна на весь процесс, поэтому резолв
// через DTI выполняется один раз на vtable, а не на каждый объект каждый тик.
struct NameCacheStats { uint32_t hits, misses, entries; };
NameCacheStats NameCacheGetStats();
void           NameCacheReset();

// Инициализация базы образа. Вызывается из продуктового старта
// (Runtime::Init), а НЕ из DevTools — фундамент нужен всегда.
void Init();

} // namespace Mem
} // namespace Runtime
