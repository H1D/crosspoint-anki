#pragma once

#include <BoardConfig.h>

// BoardConfig helpers from Free-Ink/freeink-sdk#218 that the pinned SDK does not
// have yet. Delete this file once the SDK pin includes them: the build then
// fails here with a redefinition.
namespace BoardConfig {

inline bool hasHorizontalKeys() { return ACTIVE.input.left != PIN_UNASSIGNED || ACTIVE.input.right != PIN_UNASSIGNED; }

}  // namespace BoardConfig
