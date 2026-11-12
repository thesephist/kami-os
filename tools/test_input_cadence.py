"""Validate successful-sample gap diagnostics, I2C failures and clock rollover."""
from pathlib import Path
import subprocess, tempfile
ROOT=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='input-cadence-') as td:
 p=Path(td)
 (p/'test.cpp').write_text('''#include "core/input_cadence.h"
#include <cassert>
int main(){core::input::Cadence c;c.sample(1,true);assert(!c.snapshot().samples);
 c.start();c.sample(0,true);c.sample(10000,true);c.sample(30000,false);c.sample(70000,true);
 auto s=c.snapshot();assert(s.samples==3&&s.failures==1&&s.maxGapUs==60000&&s.spanUs==70000);
 assert(s.blind20Us==40000&&s.blind50Us==10000&&!s.blind100Us);
 c.stop();c.sample(1000000,true);assert(c.snapshot().samples==3);
 c.start();c.sample(0xfffff000u,true);c.sample(uint32_t(0xfffff000u+150000),true);
 s=c.snapshot();assert(s.maxGapUs==150000&&s.blind20Us==130000&&s.blind50Us==100000&&s.blind100Us==50000);
 c.start();assert(!c.snapshot().samples&&!c.snapshot().failures&&!c.snapshot().spanUs);
}''')
 subprocess.run(['c++','-std=c++11','-fsanitize=address,undefined','-I'+str(ROOT/'include'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
print('PASS input cadence: disabled/reset/stop, successful snapshots, failed I2C gaps, pulse-window bounds and timer rollover')
