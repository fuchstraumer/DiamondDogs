#pragma once
#ifndef CNOISE_H
#define CNOISE_H

// Include all modules
#include "modules\Modules.h"

// Include models, which help construct noise data (especially in 3D)
#include "models\Models.h"

// Simple macro to shortcut away the namespaces.
#ifdef USING_CNOISE_NAMESPACES
using namespace cnoise::combiners;
using namespace cnoise::generators;
using namespace cnoise::modifiers;
using namespace cnoise::utility;
using namespace cnoise::models;
#endif // USING_CNOISE_NAMESPACES

#endif // !CNOISE_H
