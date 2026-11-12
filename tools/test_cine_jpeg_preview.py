from pathlib import Path
import tempfile,subprocess
R=Path(__file__).resolve().parents[1];s=(R/'src/services/cine_camera_resize.cpp').read_text();s=s[s.index('struct Decode {'):s.index('struct Encode')]
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp);src=p/'test.cpp';src.write_text('''#include <stdint.h>
#include <limits.h>
#define MAX_BUFFERED_PIXELS 2048
#include <stddef.h>
#include <string.h>
#include <vector>
#include <cassert>
namespace core{namespace watchdog{void feed(){}}}
struct Session{static constexpr unsigned previewWidth=224,previewHeight=168;};
struct JPEGDRAW{void* pUser;int x,y,iWidthUsed,iHeight,iWidth;uint16_t* pPixels;};
'''+s+'''
int main(){
 for(unsigned w:{256u,320u}){unsigned h=w==256?192:240;std::vector<uint16_t> dst(224*168+2,0xbeef);Decode c{nullptr,0,dst.data()+1,nullptr,w,h,false};assert(writeJpeg(&c,0,0,w,h,nullptr));
 for(unsigned y=0;y<h;y+=8)for(unsigned x=0;x<w;x+=8){std::vector<uint8_t> tile(8*8*3);for(unsigned j=0;j<8;++j)for(unsigned i=0;i<8;++i){auto* p=tile.data()+(j*8+i)*3;p[0]=(x+i)%256;p[1]=(y+j)%256;p[2]=0;}assert(writeJpeg(&c,x,y,8,8,tile.data()));}
 assert(dst.front()==0xbeef&&dst.back()==0xbeef);for(unsigned y=0;y<168;++y)for(unsigned x=0;x<224;++x){unsigned r=(x*w/224)%256,g=(y*h/168)%256;assert(dst[1+y*224+x]==((r>>3)<<11|(g>>2)<<5));}
 std::fill(dst.begin(),dst.end(),0xbeef);
 for(unsigned y=0;y<h;y+=8)for(unsigned x=0;x<w;x+=8){std::vector<uint16_t> tile(8*10,0xffff);for(unsigned j=0;j<8;++j)for(unsigned i=0;i<8;++i){unsigned r=(x+i)%256,g=(y+j)%256;tile[j*10+i]=((r>>3)<<11|(g>>2)<<5);}JPEGDRAW b{&c,int(x),int(y),8,8,10,tile.data()};assert(writeBlock565(&b));}
 assert(dst.front()==0xbeef&&dst.back()==0xbeef);for(unsigned y=0;y<168;++y)for(unsigned x=0;x<224;++x){unsigned r=(x*w/224)%256,g=(y*h/168)%256;assert(dst[1+y*224+x]==((r>>3)<<11|(g>>2)<<5));}
 {bool stop=true;c.shouldAbort=[](void* p){return *static_cast<bool*>(p);};c.context=&stop;JPEGDRAW b{&c,0,0,8,8,8,dst.data()};assert(!writeBlock565(&b)&&c.interrupted);assert(!writeJpeg(&c,0,0,8,8,nullptr));c.shouldAbort=nullptr;c.context=nullptr;c.interrupted=false;}
 assert(!writeBlock565(nullptr));JPEGDRAW bad{&c,INT_MAX,INT_MAX,INT_MAX,INT_MAX,INT_MAX,dst.data()};assert(!writeBlock565(&bad));bad={&c,0,0,8,8,INT_MAX,dst.data()};assert(!writeBlock565(&bad));bad={&c,0,0,256,192,256,dst.data()};assert(!writeBlock565(&bad));assert(!writeJpeg(nullptr,0,0,8,8,nullptr));assert(!readJpeg(nullptr,0,nullptr,4));
 JPEGDRAW invalid{&c,-1,0,8,8,10,nullptr};assert(!writeBlock565(&invalid));
 uint8_t bytes[3]={};assert(!writeJpeg(&c,w,0,1,1,bytes));
 }
}
''');subprocess.run(['c++','-std=c++11','-fsanitize=address,undefined','-O1',str(src),'-o',str(p/'test')],check=True);subprocess.run([str(p/'test')],check=True)
print('PASS actual scaled JPEG preview callback: native/VGA tile coordinates, full viewport coverage, RGB channel order, output guards, malformed block rejection; ASan/UBSan')
