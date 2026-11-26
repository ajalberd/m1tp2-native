#pragma once

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#include <cstdint>

#ifndef GL_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_TEXTURE_MAX_ANISOTROPY_EXT     0x84FE
#endif
#ifndef GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT 0x84FF
#endif

namespace m1 {
namespace gl {

extern PFNGLCREATESHADERPROC                  CreateShader;
extern PFNGLSHADERSOURCEPROC                  ShaderSource;
extern PFNGLCOMPILESHADERPROC                 CompileShader;
extern PFNGLGETSHADERIVPROC                   GetShaderiv;
extern PFNGLGETSHADERINFOLOGPROC              GetShaderInfoLog;
extern PFNGLDELETESHADERPROC                  DeleteShader;
extern PFNGLCREATEPROGRAMPROC                 CreateProgram;
extern PFNGLATTACHSHADERPROC                  AttachShader;
extern PFNGLLINKPROGRAMPROC                   LinkProgram;
extern PFNGLGETPROGRAMIVPROC                  GetProgramiv;
extern PFNGLGETPROGRAMINFOLOGPROC             GetProgramInfoLog;
extern PFNGLUSEPROGRAMPROC                    UseProgram;
extern PFNGLDELETEPROGRAMPROC                 DeleteProgram;
extern PFNGLGETUNIFORMLOCATIONPROC            GetUniformLocation;
extern PFNGLUNIFORM1IPROC                     Uniform1i;
extern PFNGLUNIFORM1FPROC                     Uniform1f;
extern PFNGLUNIFORM2FPROC                     Uniform2f;
extern PFNGLUNIFORM3FPROC                     Uniform3f;
extern PFNGLUNIFORM4FPROC                     Uniform4f;
extern PFNGLGENVERTEXARRAYSPROC               GenVertexArrays;
extern PFNGLBINDVERTEXARRAYPROC               BindVertexArray;
extern PFNGLDELETEVERTEXARRAYSPROC            DeleteVertexArrays;
extern PFNGLGENBUFFERSPROC                    GenBuffers;
extern PFNGLBINDBUFFERPROC                    BindBuffer;
extern PFNGLBUFFERDATAPROC                    BufferData;
extern PFNGLBUFFERSUBDATAPROC                 BufferSubData;
extern PFNGLDELETEBUFFERSPROC                 DeleteBuffers;
extern PFNGLENABLEVERTEXATTRIBARRAYPROC       EnableVertexAttribArray;
extern PFNGLVERTEXATTRIBPOINTERPROC           VertexAttribPointer;
extern PFNGLACTIVETEXTUREPROC                 ActiveTexture;
extern PFNGLGENERATEMIPMAPPROC                GenerateMipmap;
extern PFNGLGENFRAMEBUFFERSPROC               GenFramebuffers;
extern PFNGLBINDFRAMEBUFFERPROC               BindFramebuffer;
extern PFNGLDELETEFRAMEBUFFERSPROC            DeleteFramebuffers;
extern PFNGLFRAMEBUFFERRENDERBUFFERPROC       FramebufferRenderbuffer;
extern PFNGLCHECKFRAMEBUFFERSTATUSPROC        CheckFramebufferStatus;
extern PFNGLBLITFRAMEBUFFERPROC               BlitFramebuffer;
extern PFNGLGENRENDERBUFFERSPROC              GenRenderbuffers;
extern PFNGLBINDRENDERBUFFERPROC              BindRenderbuffer;
extern PFNGLDELETERENDERBUFFERSPROC           DeleteRenderbuffers;
extern PFNGLRENDERBUFFERSTORAGEMULTISAMPLEPROC RenderbufferStorageMultisample;

bool LoadFunctions();
GLuint BuildProgram(const char* vsSrc, const char* fsSrc, const char* label);

} // namespace gl
} // namespace m1
