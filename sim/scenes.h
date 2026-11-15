#pragma once

// The scenes the render knows: a name and a function that builds a view from fixtures. One view
// has several scenes where its states differ. A scene that touches the screen says with `check`
// what the touch must have done by the time the scene is drawn.

#include <vector>

#include "Shell.h"
#include "SimHost.h"

struct Scene {
    const char* name;
    void (*build)(SimHost& host, Shell& shell);

    // Returns what is wrong once the scene has run, or nullptr. Optional.
    const char* (*check)(const SimHost& host);
};

const std::vector<Scene>& get_scenes();
