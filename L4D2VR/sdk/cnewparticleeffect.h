#pragma once

#include <math.h>
#include "game.h"
#include "offsets.h"

class Game;

// Only its address is kept, as a field of C_Portal_Player.
class CNewParticleEffect;

class CPortal_Base2D
{
public:
	inline VMatrix MatrixThisToLinked() {
		return *(VMatrix*)((uintptr_t)this + 0x4C4);
	};
};
