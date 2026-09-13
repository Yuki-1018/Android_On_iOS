"""Pinned ANGLE Metal additions for Goldfish's GL texture EGLImages.

Keep selected mip storage shared with the source and retain it when orphaned.
Only advertise the texture-2D source target implemented here.
"""
BASE = 'Source/ThirdParty/ANGLE/src/libANGLE/renderer/metal/'
REPLACEMENTS = [
    (BASE + 'DisplayMtl.mm',
     '    outExtensions->imageBase = true;',
     '    outExtensions->imageBase = true;\n    outExtensions->glTexture2DImage = true;'),
    (BASE + 'TextureMtl.h',
     '    const mtl::TextureRef &getNativeTexture() const { return mNativeTexture; }',
     '''    const mtl::TextureRef &getNativeTexture() const { return mNativeTexture; }

    // Share the selected mip's native storage, not a snapshot of its pixels.
    angle::Result exportEGLImage(const gl::Context *context,
                                 const gl::ImageIndex &index,
                                 mtl::TextureRef *out)
    {
        if (ensureTextureCreated(context) != angle::Result::Continue ||
            ensureImageCreated(context, index) != angle::Result::Continue)
            return angle::Result::Stop;
        *out = getImage(index);
        return *out ? angle::Result::Continue : angle::Result::Stop;
    }

    void orphanEGLImage(const gl::ImageIndex &index)
    {
        // Keep unrelated mip levels, but detach this source image so a same-size
        // glTexImage cannot overwrite storage still owned by EGLImage siblings.
        releaseTexture(false, true);
        getImage(index).reset();
    }'''),
    (BASE + 'ImageMtl.h',
     '    gl::TextureType mImageTextureType;',
     '''    // Borrowed only between creation and synchronous initialize().
    const gl::Context *mSourceContext;
    gl::TextureType mImageTextureType;'''),
    (BASE + 'ImageMtl.mm',
     'ImageMtl::ImageMtl(const egl::ImageState &state, const gl::Context *context) : ImageImpl(state) {}',
     '''ImageMtl::ImageMtl(const egl::ImageState &state, const gl::Context *context)
    : ImageImpl(state), mSourceContext(context) {}'''),
    (BASE + 'ImageMtl.mm',
     '''    if (mState.target == EGL_METAL_TEXTURE_ANGLE)
    {''',
     '''    if (mState.target == EGL_GL_TEXTURE_2D_KHR)
    {
        TextureMtl *texture = GetImplAs<TextureMtl>(GetAs<gl::Texture>(mState.source));
        if (!mSourceContext ||
            texture->exportEGLImage(mSourceContext, mState.imageIndex, &mNativeTexture) !=
                angle::Result::Continue)
        {
            mSourceContext = nullptr;
            return egl::EglBadAlloc();
        }
        mSourceContext = nullptr;
        // exportEGLImage returns a single-mip view, so the imported level is 0.
        mImageTextureType = gl::TextureType::_2D;
        mImageLevel = 0;
        mImageLayer = 0;
    }
    else if (mState.target == EGL_METAL_TEXTURE_ANGLE)
    {'''),
    (BASE + 'ImageMtl.mm',
     '''    if (sibling == mState.source)
    {
        mNativeTexture = nullptr;
    }''',
     '''    // EGLImage siblings must keep the old storage when the source texture
    // is deleted or respecified. The shared TextureRef owns that storage until
    // this image and its targets release it; onDestroy handles final release.
    (void)context;
    if (sibling == mState.source && mState.target == EGL_GL_TEXTURE_2D_KHR)
    {
        GetImplAs<TextureMtl>(GetAs<gl::Texture>(sibling))->orphanEGLImage(mState.imageIndex);
    }'''),
]
