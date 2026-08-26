// 84.25: полное тело vs деталь/база. Держать в синхроне с
// RuntimeInternal.h KindIsLiveEnemyBody (test_build004 сравнивает тела).
#include <assert.h>
#include <stdio.h>
#include <string.h>

inline bool KindIsLiveEnemyBody(const char* n)
{
    if (!n || !n[0]) return false;
    if (!strcmp(n, "uHumanEnemy")) return true;
    if (n[0] != 'u' || n[1] != 'E' || n[2] != 'm') return false;
    if (n[3] < '0' || n[3] > '9') return false;
    const char* p = n + 3;
    while (*p >= '0' && *p <= '9') ++p;
    if (*p == 0) return true;
    if (*p == '_' && p[1] >= '0' && p[1] <= '9' && p[2] >= '0' && p[2] <= '9')
        return true;
    return false;
}

int main()
{
    assert(KindIsLiveEnemyBody("uEm8100"));
    assert(KindIsLiveEnemyBody("uEm0100"));
    assert(KindIsLiveEnemyBody("uEm0101"));
    assert(KindIsLiveEnemyBody("uEm5000"));
    assert(KindIsLiveEnemyBody("uEm5800"));
    assert(KindIsLiveEnemyBody("uEm8000"));
    assert(KindIsLiveEnemyBody("uEm5200_00"));
    assert(KindIsLiveEnemyBody("uHumanEnemy"));
    assert(!KindIsLiveEnemyBody("uEmDragonBase"));
    assert(!KindIsLiveEnemyBody("uEmDragonBase::DragonAttackRange"));
    assert(!KindIsLiveEnemyBody("uEmZakoDragonBase"));
    assert(!KindIsLiveEnemyBody("uEm0100_3"));
    assert(!KindIsLiveEnemyBody("uEm0100_0"));
    assert(!KindIsLiveEnemyBody("uEm5000_1"));
    assert(!KindIsLiveEnemyBody("uPlayer"));
    assert(!KindIsLiveEnemyBody("uCmc"));
    assert(!KindIsLiveEnemyBody("uNpc"));
    assert(!KindIsLiveEnemyBody(""));
    assert(!KindIsLiveEnemyBody(0));
    fprintf(stderr, "KindIsLiveEnemyBody 84.25 filter: PASS\n");
    return 0;
}
