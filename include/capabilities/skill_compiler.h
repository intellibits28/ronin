#pragma once

#include "long_term_memory.h"
#include <string>

namespace Ronin::Kernel::Reasoning {

class SkillCompiler {
public:
    static void compileAndPromoteSkills(Memory::LongTermMemory* ltm, int threshold = 100);
};

} // namespace Ronin::Kernel::Reasoning
