/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef WEBGL_SAMPLER_H_
#define WEBGL_SAMPLER_H_

#include "mozilla/LinkedList.h"
#include "nsWrapperCache.h"
#include "WebGLObjectModel.h"

namespace mozilla {

class WebGLSampler final
    : public nsWrapperCache
    , public WebGLRefCountedObject<WebGLSampler>
    , public LinkedListElement<WebGLSampler>
    , public WebGLContextBoundObject
{
    friend class WebGLContext2;
    friend class WebGLTexture;

public:
    explicit WebGLSampler(WebGLContext* webgl, GLuint sampler);

    const GLuint mGLName;

    void Delete();
    WebGLContext* GetParentObject() const;

    virtual JSObject* WrapObject(JSContext* cx, JS::Handle<JSObject*> givenProto) override;

    void SamplerParameter1i(GLenum pname, GLint param);
    void SamplerParameter1f(GLenum pname, GLfloat param);

private:

    NS_INLINE_DECL_CYCLE_COLLECTING_NATIVE_REFCOUNTING(WebGLSampler)
    NS_DECL_CYCLE_COLLECTION_SCRIPT_HOLDER_NATIVE_CLASS(WebGLSampler)

    TexMinFilter mMinFilter;
    TexMagFilter mMagFilter;
    TexWrap mWrapS;
    TexWrap mWrapT;
    TexWrap mWrapR;
    GLint mMinLod;
    GLint mMaxLod;
    TexCompareMode mCompareMode;
    TexCompareFunc mCompareFunc;

private:
    ~WebGLSampler();
};

} // namespace mozilla

#endif // WEBGL_SAMPLER_H_
