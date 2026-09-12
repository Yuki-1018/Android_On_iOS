#include "Bridge.h"
#include "gles2_opcodes.h"
#undef OP_last
#include "renderControl_opcodes.h"
#include <vector>
#include <stdexcept>
#include <cstring>
#include <cstdio>
#include <chrono>
#include <thread>
#include <algorithm>
static void check(bool ok, const char *message) { if (!ok) throw std::runtime_error(message); }
struct Client {
    void *stream = ae_gpu_open();
    Client() { check(stream, "open GPU stream"); uint32_t flags=0; send(&flags,4); }
    ~Client() { ae_gpu_close(stream); }
    void send(const void *data, size_t size) {
        auto p=static_cast<const uint8_t *>(data);
        auto end=std::chrono::steady_clock::now()+std::chrono::seconds(10);
        while(size) {
            // Deliberately split opcode, size, arguments and payload at odd boundaries.
            int n=ae_gpu_send(stream,p,std::min(size,size_t(7)));
            if(n==-2) { check(std::chrono::steady_clock::now()<end,"write timeout"); std::this_thread::yield(); continue; }
            check(n>0,"stream write"); p+=n; size-=n;
        }
    }
    std::vector<uint32_t> receive(size_t words) {
        std::vector<uint32_t> out(words); auto p=reinterpret_cast<uint8_t *>(out.data()); size_t left=words*4;
        auto end=std::chrono::steady_clock::now()+std::chrono::seconds(10);
        while(left) {
            int n=ae_gpu_receive(stream,p,std::min(left,size_t(8192)));
            if(n==-2) { check(std::chrono::steady_clock::now()<end,"read timeout"); std::this_thread::sleep_for(std::chrono::milliseconds(1)); continue; }
            check(n>0,"stream read"); p+=n; left-=n;
        }
        return out;
    }
    std::vector<uint32_t> call(uint32_t opcode, std::vector<uint32_t> args={}, size_t out=0) {
        std::vector<uint32_t> packet={opcode,uint32_t(8+args.size()*4)};
        packet.insert(packet.end(),args.begin(),args.end()); send(packet.data(),packet.size()*4); return receive(out);
    }
    uint32_t shader(uint32_t type,const char *source) {
        auto id=call(OP_glCreateShader,{type},1)[0]; check(id,"create shader");
        size_t len=strlen(source)+1;
        std::vector<uint8_t> packet(20+len);
        uint32_t head[]={OP_glShaderString,uint32_t(packet.size()),id,uint32_t(len)};
        memcpy(packet.data(),head,16); memcpy(packet.data()+16,source,len);
        uint32_t count=uint32_t(len); memcpy(packet.data()+16+len,&count,4);
        send(packet.data(),packet.size()); call(OP_glCompileShader,{id});
        check(call(OP_glGetShaderiv,{id,0x8B81,4},1)[0]==1,"shader compile"); return id;
    }
};
int main() {
 try {
    check(ae_gpu_init(64,64)==1,"EGL/GLES1/GLES2/EGLImage initialization");
    Client first, second;
    {
        Client slow;
        slow.call(OP_rcGetConfigs,{1u<<20,1u<<20});
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        check(second.call(OP_rcGetRendererVersion,{},1)[0]==1,"slow client must not block another connection");
    }
    check(first.call(OP_rcGetRendererVersion,{},1)[0]==1,"renderer handshake");
    auto choice=first.call(OP_rcChooseConfig,{20,0x3040,4,0x3033,4,0x3038,20,4,1},2);
    check(choice[1]>0,"GLES2 EGL config"); uint32_t cfg=choice[0];
    auto ctx=first.call(OP_rcCreateContext,{cfg,0,2},1)[0]; check(ctx,"context");
    auto surface=first.call(OP_rcCreateWindowSurface,{cfg,64,64},1)[0]; check(surface,"surface");
    auto color=first.call(OP_rcCreateColorBuffer,{64,64,0x1908},1)[0]; check(color,"color buffer");
    first.call(OP_rcSetWindowColorBuffer,{surface,color});
    check(first.call(OP_rcMakeCurrent,{ctx,surface,surface},1)[0]==1,"make current");
    auto vs=first.shader(0x8B31,"attribute vec4 p; void main(){gl_Position=p;}");
    auto fs=first.shader(0x8B30,"precision mediump float; void main(){gl_FragColor=vec4(1.,0.,0.,1.);}");
    auto program=first.call(OP_glCreateProgram,{},1)[0];
    first.call(OP_glAttachShader,{program,vs}); first.call(OP_glAttachShader,{program,fs});
    first.call(OP_glBindAttribLocation,{program,0,4,0x70});
    first.call(OP_glLinkProgram,{program});
    check(first.call(OP_glGetProgramiv,{program,0x8B82,4},1)[0]==1,"program link");
    first.call(OP_glClearColor,{0,0,0,0x3f800000}); first.call(OP_glClear,{0x4000});
    auto vbo=first.call(OP_glGenBuffers,{1,4},1)[0]; check(vbo,"vertex buffer");
    first.call(OP_glBindBuffer,{0x8892,vbo});
    first.call(OP_glBufferData,{0x8892,24,24,0xbf800000,0xbf800000,0x40400000,0xbf800000,0xbf800000,0x40400000,0x88E4});
    uint8_t attrib[29]={}; uint32_t header[]={OP_glVertexAttribPointerOffset,29,0,2,0x1406};
    memcpy(attrib,header,20); uint32_t stride=8; memcpy(attrib+21,&stride,4);
    first.send(attrib,sizeof(attrib)); first.call(OP_glEnableVertexAttribArray,{0});
    first.call(OP_glViewport,{0,0,64,64}); first.call(OP_glUseProgram,{program});
    first.call(OP_glDrawArrays,{4,0,3});
    auto pixel=first.call(OP_glReadPixels,{0,0,1,1,0x1908,0x1401,4},1)[0];
    check(pixel==0xff0000ff,"GLES2 readback red");
    first.call(OP_glEnable,{0x0C11}); first.call(OP_glScissor,{0,0,64,32});
    first.call(OP_glClearColor,{0,0,0x3f800000,0x3f800000}); first.call(OP_glClear,{0x4000}); first.call(OP_glDisable,{0x0C11});
    check(first.call(OP_rcFlushWindowColorBuffer,{surface},1)[0]==0,"gralloc flush");
    first.call(OP_rcFBPost,{color}); first.call(OP_glFinishRoundTrip,{},1);
    std::vector<uint8_t> frame(64*64*4);
    check(ae_gpu_frame(frame.data(),frame.size())==1,"headless post callback");
    check(frame[0]==0 && frame[1]==0 && frame[2]==255,"BGRA conversion");
    check(frame[(64*63)*4]==255 && frame[(64*63)*4+2]==0,"frame orientation bottom blue");
    first.call(OP_rcFBPost,{color}); first.call(OP_glFinishRoundTrip,{},1);
    check(ae_gpu_frame(frame.data(),frame.size())==0,"unchanged frame skips CPU/Metal transfer");
    first.call(OP_glEnable,{0x0C11}); first.call(OP_glScissor,{0,63,1,1});
    first.call(OP_glClearColor,{0,0x3f800000,0,0x3f800000}); first.call(OP_glClear,{0x4000}); first.call(OP_glDisable,{0x0C11});
    check(first.call(OP_rcFlushWindowColorBuffer,{surface},1)[0]==0,"single pixel flush");
    first.call(OP_rcFBPost,{color}); first.call(OP_glFinishRoundTrip,{},1);
    unsigned firstRow=99, rows=99;
    check(ae_gpu_frame_region(frame.data(),frame.size(),&firstRow,&rows)==1,"dirty transfer");
    check(firstRow==0 && rows==1 && frame[1]==255 && frame[2]==0,"only changed row copied");
    check(frame[(64*63)*4]==255,"unchanged rows preserved");
    auto shared=second.call(OP_rcCreateContext,{cfg,ctx,2},1)[0]; check(shared,"shared context");
    auto other=second.call(OP_rcCreateWindowSurface,{cfg,64,64},1)[0]; check(other,"second surface");
    check(second.call(OP_rcMakeCurrent,{shared,other,other},1)[0]==1,"second current");
    check(second.call(OP_glGetProgramiv,{program,0x8B82,4},1)[0]==1,"shared program visibility");
    second.call(OP_rcMakeCurrent,{0,0,0},1); second.call(OP_rcDestroyWindowSurface,{other}); second.call(OP_rcDestroyContext,{shared});
    first.call(OP_glDeleteBuffers,{1,4,vbo}); first.call(OP_glDeleteProgram,{program}); first.call(OP_glDeleteShader,{vs}); first.call(OP_glDeleteShader,{fs});
    first.call(OP_rcMakeCurrent,{0,0,0},1); first.call(OP_rcDestroyWindowSurface,{surface}); first.call(OP_rcDestroyContext,{ctx}); first.call(OP_rcCloseColorBuffer,{color});
    first.call(OP_rcGetRendererVersion,{},1); second.call(OP_rcGetRendererVersion,{},1);
    puts("GPU wire protocol, GLES2 shader draw/readback, gralloc post, dirty rows and shared contexts passed"); return 0;
 } catch(const std::exception &e) { fprintf(stderr,"GPU test: %s\n",e.what()); return 1; }
}
