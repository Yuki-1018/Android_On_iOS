/*
* Copyright (C) 2011 The Android Open Source Project
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
* http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*/
#include "RenderThread.h"

#include "EGLDispatch.h"
#include "FrameBuffer.h"
#include "GLESv2Dispatch.h"
#include "GLESv1Dispatch.h"
#include "ReadBuffer.h"
#include "RenderControl.h"
#include "RenderThreadInfo.h"
#include "TimeUtils.h"

#define STREAM_BUFFER_SIZE 4*1024*1024

RenderThread::RenderThread(IOStream *stream, emugl::Mutex *lock) :
        emugl::Thread(),
        m_lock(lock),
        m_stream(stream) {}

RenderThread::~RenderThread() {
    delete m_stream;
}

// static
RenderThread* RenderThread::create(IOStream *stream, emugl::Mutex *lock) {
    return new RenderThread(stream, lock);
}

void RenderThread::forceStop() {
    m_stream->forceStop();
}

intptr_t RenderThread::main() {
    // ANGLE/Mesa dispatch is thread-local. FrameBuffer protects shared handles
    // internally. Never hold a global lock across blocking response writes:
    // a slow guest process must not stall SurfaceFlinger or another application.
    RenderThreadInfo info;
    info.m_glDec.initGL(gles1_dispatch_get_proc_func, NULL);
    info.m_gl2Dec.initGL(gles2_dispatch_get_proc_func, NULL);
    initRenderControlContext(&info.m_rcDec);
    ReadBuffer buffer(m_stream, 64 * 1024);
    bool running = true;
    while (running && buffer.getData() > 0) {
        while (buffer.validData() >= 8) {
            uint32_t size;
            memcpy(&size, buffer.buf() + 4, 4);
            if (size < 8 || size > 64u * 1024 * 1024) { running = false; break; }
            if (buffer.validData() < size) break;
            size_t used = info.m_glDec.decode(buffer.buf(), size, m_stream);
            if (!used) used = info.m_gl2Dec.decode(buffer.buf(), size, m_stream);
            if (!used) used = info.m_rcDec.decode(buffer.buf(), size, m_stream);
            if (used != size) { fprintf(stderr, "GPU rejected packet: size=%u consumed=%zu\n", size, used); running = false; break; }
            buffer.consume(used);
        }
    }
    m_stream->forceStop();
    FrameBuffer::getFB()->bindContext(0, 0, 0);
    FrameBuffer::getFB()->drainWindowSurface();
    FrameBuffer::getFB()->drainRenderContext();
    return 0;
}
