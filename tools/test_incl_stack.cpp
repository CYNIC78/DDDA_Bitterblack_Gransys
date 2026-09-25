// Test production one-line inclination snapshot; shim elides unrelated runtime.
#include "tcomp/guard_t.cpp"
#include <cassert>
#include <fstream>
#include <string>
std::ofstream logFile;
namespace Runtime {
const char* PartyCombatSlotName(int slot) {
    return slot == PARTY_MAIN ? "MainPawn" :
           slot == PARTY_HIRED1 ? "Hired1" : "Hired2";
}
}
int main(int argc, char** argv) {
    assert(argc == 2);
    logFile.open(argv[1]);
    PawnAI::PawnView view[2] = {};
    view[0].slot = Runtime::PARTY_MAIN;
    view[0].body = 0x1111;
    view[0].vocation = VOC_STRIDER;
    view[0].incl[I_NEXUS] = 936.f;
    view[0].incl[I_SCATHER] = 400.f;
    view[0].incl[I_GUARDIAN] = 350.f;
    PawnAI::Persona::RanksOf(view[0].incl, view[0].rank);
    // First snapshot and unchanged tick.
    PawnAI::LogPawnStackIfChanged(view, 1);
    PawnAI::LogPawnStackIfChanged(view, 1);
    // User edits a DIFFERENT inclination with the same pawn/body.
    view[0].incl[I_SCATHER] = 420.f;
    PawnAI::Persona::RanksOf(view[0].incl, view[0].rank);
    PawnAI::LogPawnStackIfChanged(view, 1);
    view[1] = view[0];
    view[1].slot = Runtime::PARTY_HIRED1;
    view[1].body = 0x2222;
    view[1].vocation = VOC_FIGHTER;
    view[1].incl[I_GUARDIAN] = 993.f;
    PawnAI::Persona::RanksOf(view[1].incl, view[1].rank);
    PawnAI::LogPawnStackIfChanged(view, 2);
    logFile.flush();
    std::ifstream f(argv[1]);
    std::string line;
    int lines = 0, anchors = 0, hired = 0;
    while (std::getline(f, line)) {
        ++lines;
        if (line.find("source=anchor") != std::string::npos) ++anchors;
        if (line.find("source=hired-live") != std::string::npos) ++hired;
        assert(line.find("Nexus=") != std::string::npos);
        assert(line.find("Guardian=") != std::string::npos);
    }
    assert(lines == 3 && anchors == 2 && hired == 1);
}
