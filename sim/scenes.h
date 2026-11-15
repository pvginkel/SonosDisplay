#pragma once

// The scenes the render knows: a name and a function that builds a view from fixtures. One view
// has several scenes where its states differ.

#include <vector>

#include "Shell.h"
#include "SimHost.h"

struct Scene {
    const char* name;
    void (*build)(SimHost& host, Shell& shell);
};

const std::vector<Scene>& get_scenes();
