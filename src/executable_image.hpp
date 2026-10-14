#pragma once

struct ExecutableImage {
    int64_t size = 0;
    // probably some data about version of code and arch
    char* data; // what to use in C++ instead of raw pointers? 
};



