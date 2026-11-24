#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
namespace gimmd::secondary_physics {
struct Plan { bool valid=true,reset=false;unsigned steps=0; };
// Drive simulation from the motion clock, never from render callback count.
// Seeking, looping and long stalls discard stale velocity instead of catching up.
struct Clock {
    static constexpr double Step=1.0/60;
    double previous=0,remainder=0;uint64_t epoch=0;bool initialized=false,playing=false;
    Plan sample(double seconds,uint64_t generation,bool running) {
        if(!std::isfinite(seconds)||seconds<0)return {false,false,0};
        const double delta=seconds-previous;
        const bool jump=!initialized||generation!=epoch||delta<-.000001||delta>.1||(!playing&&!running&&delta>.000001);
        Plan result{};result.reset=jump;
        if(jump)remainder=0;
        else if(playing){
            remainder+=delta;
            result.steps=unsigned(std::floor((remainder+1e-9)/Step));
            if(result.steps>4){result.steps=0;result.reset=true;remainder=0;}
            else remainder=std::max(0.0,remainder-result.steps*Step);
        }
        previous=seconds;epoch=generation;initialized=true;playing=running;
        return result;
    }
};
}
