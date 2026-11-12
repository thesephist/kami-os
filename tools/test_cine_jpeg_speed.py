"""Compare the pinned codec's exact output with and without its O3 build overlay."""

from pathlib import Path
import importlib.util
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
LIB = ROOT / ".pio/libdeps/esp32-s3-menu-n16r8-production/JPEGDEC/src"
spec = importlib.util.spec_from_file_location(
    "jpeg_speed", ROOT / "scripts/cine_jpeg_speed.py"
)
speed = importlib.util.module_from_spec(spec)
spec.loader.exec_module(speed)
with tempfile.TemporaryDirectory(prefix="cine-jpeg-speed-") as directory:
    root = Path(directory)
    original = (LIB / "JPEGDEC.cpp").read_bytes()
    generated = speed.speed_source(LIB / "JPEGDEC.cpp")
    assert generated == b'#pragma GCC optimize ("O3")\n' + original
    patched = root / "JPEGDEC.cpp"
    patched.write_bytes(generated)
    for name in speed.PINNED:
        (root / name).write_bytes((LIB / name).read_bytes())
    assert speed.speed_source(patched) == generated
    (root / "jpeg.inl").write_bytes(b"changed upstream revision")
    try:
        speed.speed_source(patched)
        raise AssertionError("Unexpected codec revision was accepted")
    except RuntimeError:
        pass
    # The compiler needs the real inlined implementation, not the corrupted pin fixture.
    (root / "jpeg.inl").unlink()
    patched.write_bytes(generated)
    (root / "main.cpp").write_text(
        r"""
#include <JPEGDEC.h>
#include <cassert>
#include <fstream>
#include <vector>
#include <cstdlib>
std::vector<uint16_t> output;int width,height;
int draw(JPEGDRAW* b){assert(b&&b->pPixels&&b->x>=0&&b->y>=0&&b->iWidthUsed>0&&b->iWidthUsed<=b->iWidth&&b->x+b->iWidthUsed<=width&&b->y+b->iHeight<=height);for(int y=0;y<b->iHeight;++y)for(int x=0;x<b->iWidthUsed;++x)output[(b->y+y)*width+b->x+x]=b->pPixels[y*b->iWidth+x];return 1;}
int main(int argc,char** argv){assert(argc==4);std::ifstream file(argv[1],std::ios::binary);std::vector<uint8_t> jpeg((std::istreambuf_iterator<char>(file)),{});JPEGDEC codec;assert(codec.openRAM(jpeg.data(),jpeg.size(),draw));int scale=atoi(argv[3]);width=(codec.getWidth()+scale-1)/scale;height=(codec.getHeight()+scale-1)/scale;output.resize(width*height);codec.setPixelType(RGB565_LITTLE_ENDIAN);int flags=scale==8?JPEG_SCALE_EIGHTH:scale==4?JPEG_SCALE_QUARTER:scale==2?JPEG_SCALE_HALF:0;assert(codec.decode(0,0,flags));codec.close();std::ofstream out(argv[2],std::ios::binary);out.write(reinterpret_cast<char*>(output.data()),output.size()*2);}
"""
    )
    binaries = []
    for name, source in [("size", LIB / "JPEGDEC.cpp"), ("speed", patched)]:
        binary = root / name
        subprocess.run(
            [
                "c++",
                "-std=c++11",
                "-Os",
                "-fsanitize=address",
                "-D__LINUX__",
                "-I" + str(LIB),
                str(source),
                str(root / "main.cpp"),
                "-o",
                str(binary),
            ],
            check=True,
        )
        binaries.append(binary)
    generator = r"""
from PIL import Image
import sys
for w,h,sub in [(2048,1536,0),(2048,1536,1),(640,480,2)]:
    row=bytes(v for x in range(w) for v in ((x*37)%256,(x*13)%256,(x*71)%256))
    im=Image.frombytes('RGB',(w,h),b''.join(bytes((v+y*17)%256 for v in row)for y in range(h)))
    im.save(sys.argv[1]+'/'+str(w)+'-'+str(sub)+'.jpg',quality=92,subsampling=sub)
"""
    subprocess.run([sys.executable, "-c", generator, str(root)], check=True)
    cases = 0
    for image in root.glob("*.jpg"):
        for scale in [1, 2, 4, 8]:
            outputs = []
            for binary in binaries:
                dest = root / (binary.name + ".rgb565")
                subprocess.run(
                    [str(binary), str(image), str(dest), str(scale)], check=True
                )
                outputs.append(dest.read_bytes())
            assert outputs[0] == outputs[1], (image.name, scale)
            cases += 1
    print(
        f"PASS pinned JPEGDEC O3 overlay: exact source scope, changed-revision rejection, {cases} identical 3MP/VGA RGB565 decodes at full/half/quarter/eighth scale; ASan, host CPU"
    )
