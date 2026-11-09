from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='cine-tone-') as directory:
 p=Path(directory);(p/'test.cpp').write_text(r'''
#include "core/cine_tone.h"
#include <cassert>
#include <vector>
using namespace core::cine;
int main(){
 ToneMap t;Parameters p;p.black=4;for(auto& g:p.gains)g=1024;
 for(unsigned look=0;look<3;++look)for(int ev=-6;ev<=6;++ev){
  p.look=look;p.exposureThirds=ev;t.configure(p);int last=-1;
  for(unsigned i=0;i<256;++i){uint8_t r,g,b;t.pixel(i,i,i,r,g,b);assert(r==g&&g==b);assert(r>=last);last=r;if(i<=4)assert(r==0);}
 }
 p.exposureThirds=0;p.look=1;t.configure(p);uint8_t r,g,b;
 t.pixel(255,255,255,r,g,b);assert(r>=238&&r<=242);
 t.pixel(49,49,49,r,g,b);assert(r>=115&&r<=120);
 uint8_t raw[]={16,64,64,128},out[3];assert(t.renderBgr(raw,4,2,2,out,3));assert(out[0]<out[1]&&out[1]<out[2]);
 assert(!t.renderBgr(raw,3,2,2,out,3));assert(!t.renderBgr(raw,4,1,4,out,3));assert(!t.renderBgr(raw,4,2,2,out,2));
 uint16_t preview[35];assert(t.preview565(raw,4,2,2,preview,7,5));for(auto color:preview)assert(color==preview[0]);assert(!t.preview565(raw,4,2,2,preview,0,5));
 t.configure(p,true);t.pixel(255,255,255,r,g,b);assert(r==255&&g==255&&b==255);
 p.black=255;t.configure(p);t.pixel(255,255,255,r,g,b);
 // Fractional area uses all photosites before tone mapping and preserves flat fields.
 std::vector<uint8_t> native(2048*1536,49),vga(640*480*3);Parameters areaParams;for(auto& gain:areaParams.gains)gain=1024;t.configure(areaParams);
 assert(t.renderBgrArea(native.data(),native.size(),2048,1536,vga.data(),vga.size(),640,480));
 t.pixel(49,49,49,r,g,b);for(unsigned i=0;i<vga.size();i+=3)assert(vga[i]==b&&vga[i+1]==g&&vga[i+2]==r);
 assert(!t.renderBgrArea(native.data(),native.size()-1,2048,1536,vga.data(),vga.size(),640,480));
 assert(!t.renderBgrArea(native.data(),native.size(),2048,1536,vga.data(),vga.size()-1,640,480));
 assert(!t.renderBgrArea(native.data(),native.size(),2048,1536,vga.data(),vga.size(),0,480));
 // Impulses at every Bayer phase must influence the result; black-border and mirror bounds.
 for(unsigned phase=0;phase<4;++phase){uint8_t impulse[16]={4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4},tiny[3];impulse[(phase/2)*4+phase%2]=240;
 assert(t.renderBgrArea(impulse,16,4,4,tiny,3,1,1));assert(tiny[0]||tiny[1]||tiny[2]);}
 // Metering uses source-linear brightness, rejects black noise, and ignores tiny speculars.
 std::vector<uint8_t> scene(1024*768,49);auto met=meterBayer(scene.data(),scene.size(),1024,768,4);
 assert(met.valid&&met.samples==3072&&met.exposureThirds==0&&!met.highlightLimited);
 scene.assign(scene.size(),16);met=meterBayer(scene.data(),scene.size(),1024,768,4);assert(met.valid&&met.exposureThirds==6);
 scene.assign(scene.size(),200);met=meterBayer(scene.data(),scene.size(),1024,768,4);assert(met.valid&&met.exposureThirds==-6);
 scene.assign(scene.size(),4);met=meterBayer(scene.data(),scene.size(),1024,768,4);assert(!met.valid&&met.exposureThirds==0);
 scene.assign(scene.size(),6);met=meterBayer(scene.data(),scene.size(),1024,768,4);assert(!met.valid&&met.exposureThirds==0);
 scene.assign(scene.size(),16);for(unsigned y=0;y<16;++y)for(unsigned x=0;x<512;++x)scene[y*1024+x]=240;
 met=meterBayer(scene.data(),scene.size(),1024,768,4);assert(met.valid&&met.exposureThirds==6&&!met.highlightLimited);
 scene.assign(scene.size(),16);for(unsigned y=0;y<64;++y)for(unsigned x=0;x<1024;++x)scene[y*1024+x]=240;
 met=meterBayer(scene.data(),scene.size(),1024,768,4);assert(met.valid&&met.highlightLimited&&met.exposureThirds==0&&met.arithmeticMean>met.geometricMean*2);
 scene.assign(scene.size(),16);for(unsigned y=1;y<64;y+=2)for(unsigned x=1;x<1024;x+=2)scene[y*1024+x]=255;
 met=meterBayer(scene.data(),scene.size(),1024,768,4);assert(met.valid&&met.highlightLimited&&met.exposureThirds==0);
 assert(!meterBayer(scene.data(),scene.size()-1,1024,768,4).valid);assert(!meterBayer(nullptr,0,0,0,4).valid);
 uint8_t grayCell[]={49,49,49,49};assert(meterBayer(grayCell,4,2,2,4).exposureThirds==0);
 met=meterBayer(scene.data(),scene.size(),1024,768,255);assert(met.exposureThirds>=-6&&met.exposureThirds<=6);
 // Large odd-stride regions exercise Bayer indexing and boundary selection.
 std::vector<uint8_t> frame(1024*768,128),image(512*384*3);std::vector<uint16_t> view(224*168);
 assert(t.renderBgr(frame.data(),frame.size(),1024,768,image.data(),image.size()));
 assert(t.preview565(frame.data(),frame.size(),1024,768,view.data(),224,168));
}
''')
 subprocess.run(['c++','-std=c++17','-O2','-fsanitize=address,undefined','-I'+str(root/'include'),str(p/'test.cpp'),str(root/'src/core/cine_tone.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
print('PASS actual Cine tone core: monotonic 3 looks x 13 exposure settings, neutral-gray hue, black/white/midtone behavior, BGGR channel order, exact frame bounds, full-frame rendering/preview and startup metering (gray, dark, white, noise, tiny/specular and colored highlights) under ASan/UBSan.')
