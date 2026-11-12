"""Pin the canonical sRGB register approximation and vendor saturation +1 matrix."""

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
int main(){const uint8_t matrix[]={29,96,3,13,132,145,138,118,20,1,152};assert(!memcmp(matrix,core::cine::matrix,11));assert(core::cine::aeLevel==-1);
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
