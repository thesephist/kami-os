"""Pin the canonical sRGB register approximation and six Color profiles and wrapping order."""

from pathlib import Path
import tempfile, subprocess

R = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as d:
    p = Path(d)
    (p / "test.cpp").write_text(
        r"""#include "core/cine_isp.h"
#include <math.h>
#include <cassert>
#include <cstring>
#include <climits>
int main(){using namespace core::cine;
const char* names[]={"Sunny","Gold","Portra","Mono","Cool","Vivid"};
for(unsigned i=0;i<colorCount;++i){auto mode=ColorMode(i);assert(!strcmp(colorProfile(mode).name,names[i]));assert(unsigned(advanceColor(mode,1))==(i+1)%colorCount);assert(unsigned(advanceColor(mode,-1))==(i+colorCount-1)%colorCount);assert(advanceColor(mode,6)==mode);assert(validColor(unsigned(advanceColor(mode,INT_MIN)))&&validColor(unsigned(advanceColor(mode,INT_MAX))));const auto& p=colorProfile(mode);for(unsigned k=1;k<15;++k)assert(p.gamma[k]>=p.gamma[k-1]);}
assert(!validColor(6)&&!validColor(255));assert(!strcmp(colorProfile(ColorMode(255)).name,"Sunny"));
for(unsigned i=3;i<11;++i)assert(monoMatrix[i]==0);
const uint8_t matrix[]={29,96,3,13,132,145,138,118,20,1,152};assert(!memcmp(matrix,core::cine::matrix,11));assert(core::cine::aeLevel==-1);
const unsigned refs[]={4,8,16,32,40,48,56,64,72,80,96,112,144,176,208};unsigned last=0;for(unsigned i=0;i<15;++i){float x=refs[i]/256.f;last=unsigned((x<=.0031308f?12.92f*x:1.055f*powf(x,1.f/2.4f)-.055f)*255.f+.5f);assert(core::cine::gamma[i]==last);if(i)assert(core::cine::gamma[i]>=core::cine::gamma[i-1]);}assert(core::cine::gamma[15]==unsigned((256.f-last)*40.f/30.f+.5f));}
"""
    )
    subprocess.run(
        [
            "c++",
            "-std=c++11",
            "-I" + str(R / "include"),
            str(p / "test.cpp"),
            "-o",
            str(p / "test"),
        ],
        check=True,
    )
    subprocess.run([str(p / "test")], check=True)
print(
    "PASS canonical ISP bytes exactly preserve the previous sRGB formula, vendor +1 matrix and AE target"
)
