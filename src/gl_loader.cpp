#include "gl_loader.h"
#include "common.h"

namespace m1 {
namespace gl {

PFNGLCREATESHADERPROC                  CreateShader = nullptr;
PFNGLSHADERSOURCEPROC                  ShaderSource = nullptr;
PFNGLCOMPILESHADERPROC                 CompileShader = nullptr;
PFNGLGETSHADERIVPROC                   GetShaderiv = nullptr;
PFNGLGETSHADERINFOLOGPROC              GetShaderInfoLog = nullptr;
PFNGLDELETESHADERPROC                  DeleteShader = nullptr;
PFNGLCREATEPROGRAMPROC                 CreateProgram = nullptr;
PFNGLATTACHSHADERPROC                  AttachShader = nullptr;
PFNGLLINKPROGRAMPROC                   LinkProgram = nullptr;
PFNGLGETPROGRAMIVPROC                  GetProgramiv = nullptr;
PFNGLGETPROGRAMINFOLOGPROC             GetProgramInfoLog = nullptr;
PFNGLUSEPROGRAMPROC                    UseProgram = nullptr;
PFNGLDELETEPROGRAMPROC                 DeleteProgram = nullptr;
PFNGLGETUNIFORMLOCATIONPROC            GetUniformLocation = nullptr;
PFNGLUNIFORM1IPROC                     Uniform1i = nullptr;
PFNGLUNIFORM1FPROC                     Uniform1f = nullptr;
PFNGLUNIFORM2FPROC                     Uniform2f = nullptr;
PFNGLUNIFORM3FPROC                     Uniform3f = nullptr;
PFNGLUNIFORM4FPROC                     Uniform4f = nullptr;
PFNGLGENVERTEXARRAYSPROC               GenVertexArrays = nullptr;
PFNGLBINDVERTEXARRAYPROC               BindVertexArray = nullptr;
PFNGLDELETEVERTEXARRAYSPROC            DeleteVertexArrays = nullptr;
PFNGLGENBUFFERSPROC                    GenBuffers = nullptr;
PFNGLBINDBUFFERPROC                    BindBuffer = nullptr;
PFNGLBUFFERDATAPROC                    BufferData = nullptr;
PFNGLBUFFERSUBDATAPROC                 BufferSubData = nullptr;
PFNGLDELETEBUFFERSPROC                 DeleteBuffers = nullptr;
PFNGLENABLEVERTEXATTRIBARRAYPROC       EnableVertexAttribArray = nullptr;
PFNGLVERTEXATTRIBPOINTERPROC           VertexAttribPointer = nullptr;
PFNGLACTIVETEXTUREPROC                 ActiveTexture = nullptr;
PFNGLGENERATEMIPMAPPROC                GenerateMipmap = nullptr;
PFNGLGENFRAMEBUFFERSPROC               GenFramebuffers = nullptr;
PFNGLBINDFRAMEBUFFERPROC               BindFramebuffer = nullptr;
PFNGLDELETEFRAMEBUFFERSPROC            DeleteFramebuffers = nullptr;
PFNGLFRAMEBUFFERRENDERBUFFERPROC       FramebufferRenderbuffer = nullptr;
PFNGLCHECKFRAMEBUFFERSTATUSPROC        CheckFramebufferStatus = nullptr;
PFNGLBLITFRAMEBUFFERPROC               BlitFramebuffer = nullptr;
PFNGLGENRENDERBUFFERSPROC              GenRenderbuffers = nullptr;
PFNGLBINDRENDERBUFFERPROC              BindRenderbuffer = nullptr;
PFNGLDELETERENDERBUFFERSPROC           DeleteRenderbuffers = nullptr;
PFNGLRENDERBUFFERSTORAGEMULTISAMPLEPROC RenderbufferStorageMultisample = nullptr;

template <typename T>
static bool loadOne(T& fn, const char* name) {
    fn = reinterpret_cast<T>(SDL_GL_GetProcAddress(name));
    if (!fn) {
        LogPrintf("[GL] Missing proc: %s\n", name);
        return false;
    }
    return true;
}

bool LoadFunctions() {
    bool ok = true;
    ok &= loadOne(CreateShader, "glCreateShader");
    ok &= loadOne(ShaderSource, "glShaderSource");
    ok &= loadOne(CompileShader, "glCompileShader");
    ok &= loadOne(GetShaderiv, "glGetShaderiv");
    ok &= loadOne(GetShaderInfoLog, "glGetShaderInfoLog");
    ok &= loadOne(DeleteShader, "glDeleteShader");
    ok &= loadOne(CreateProgram, "glCreateProgram");
    ok &= loadOne(AttachShader, "glAttachShader");
    ok &= loadOne(LinkProgram, "glLinkProgram");
    ok &= loadOne(GetProgramiv, "glGetProgramiv");
    ok &= loadOne(GetProgramInfoLog, "glGetProgramInfoLog");
    ok &= loadOne(UseProgram, "glUseProgram");
    ok &= loadOne(DeleteProgram, "glDeleteProgram");
    ok &= loadOne(GetUniformLocation, "glGetUniformLocation");
    ok &= loadOne(Uniform1i, "glUniform1i");
    ok &= loadOne(Uniform1f, "glUniform1f");
    ok &= loadOne(Uniform2f, "glUniform2f");
    ok &= loadOne(Uniform3f, "glUniform3f");
    ok &= loadOne(Uniform4f, "glUniform4f");
    ok &= loadOne(GenVertexArrays, "glGenVertexArrays");
    ok &= loadOne(BindVertexArray, "glBindVertexArray");
    ok &= loadOne(DeleteVertexArrays, "glDeleteVertexArrays");
    ok &= loadOne(GenBuffers, "glGenBuffers");
    ok &= loadOne(BindBuffer, "glBindBuffer");
    ok &= loadOne(BufferData, "glBufferData");
    ok &= loadOne(BufferSubData, "glBufferSubData");
    ok &= loadOne(DeleteBuffers, "glDeleteBuffers");
    ok &= loadOne(EnableVertexAttribArray, "glEnableVertexAttribArray");
    ok &= loadOne(VertexAttribPointer, "glVertexAttribPointer");
    ok &= loadOne(ActiveTexture, "glActiveTexture");
    ok &= loadOne(GenerateMipmap, "glGenerateMipmap");
    ok &= loadOne(GenFramebuffers, "glGenFramebuffers");
    ok &= loadOne(BindFramebuffer, "glBindFramebuffer");
    ok &= loadOne(DeleteFramebuffers, "glDeleteFramebuffers");
    ok &= loadOne(FramebufferRenderbuffer, "glFramebufferRenderbuffer");
    ok &= loadOne(CheckFramebufferStatus, "glCheckFramebufferStatus");
    ok &= loadOne(BlitFramebuffer, "glBlitFramebuffer");
    ok &= loadOne(GenRenderbuffers, "glGenRenderbuffers");
    ok &= loadOne(BindRenderbuffer, "glBindRenderbuffer");
    ok &= loadOne(DeleteRenderbuffers, "glDeleteRenderbuffers");
    ok &= loadOne(RenderbufferStorageMultisample, "glRenderbufferStorageMultisample");

    GLint maxSamples = 0;
    glGetIntegerv(GL_MAX_SAMPLES, &maxSamples);
    if (maxSamples > 8) maxSamples = 8;
    g_stats.maxMsaa = maxSamples;

    GLfloat maxAniso = 1.0f;
    glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &maxAniso);
    if (glGetError() != GL_NO_ERROR || maxAniso < 1.0f) maxAniso = 1.0f;
    g_stats.maxAniso = static_cast<int>(maxAniso);

    LogPrintf("[GL] Capabilities: maxMSAA=%dx maxAnisotropy=%dx\n", g_stats.maxMsaa, g_stats.maxAniso);
    return ok;
}

static GLuint compileOne(GLenum type, const char* src, const char* label) {
    GLuint s = CreateShader(type);
    ShaderSource(s, 1, &src, nullptr);
    CompileShader(s);
    GLint ok = 0;
    GetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        GetShaderInfoLog(s, sizeof(log), nullptr, log);
        LogPrintf("[GL] Shader compile failed (%s): %s\n", label, log);
        DeleteShader(s);
        return 0;
    }
    return s;
}

GLuint BuildProgram(const char* vsSrc, const char* fsSrc, const char* label) {
    GLuint vs = compileOne(GL_VERTEX_SHADER, vsSrc, label);
    GLuint fs = compileOne(GL_FRAGMENT_SHADER, fsSrc, label);
    if (!vs || !fs) {
        if (vs) DeleteShader(vs);
        if (fs) DeleteShader(fs);
        return 0;
    }
    GLuint p = CreateProgram();
    AttachShader(p, vs);
    AttachShader(p, fs);
    LinkProgram(p);
    DeleteShader(vs);
    DeleteShader(fs);
    GLint ok = 0;
    GetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        GetProgramInfoLog(p, sizeof(log), nullptr, log);
        LogPrintf("[GL] Program link failed (%s): %s\n", label, log);
        DeleteProgram(p);
        return 0;
    }
    return p;
}

} // namespace gl
} // namespace m1
