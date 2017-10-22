#include <string.h>
#include <stdlib.h>

#include "fpe_shader.h"
#include "string_utils.h"
#include "../glx/hardext.h"

const char* dummy_vertex = \
"varying vec4 Color; \n"
"void main() {\n"
"Color = gl_Color;\n"
"gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;\n"
"}";

const char* dummy_frag = \
"varying vec4 Color; \n"
"void main() {\n"
"gl_FragColor = Color;\n"
"}\n";

static char* shad = NULL;
static int shad_cap = 0;

const int comments = 1;

#define ShadAppend(S) shad = Append(shad, &shad_cap, S)

const char* texvecsize[] = {"vec2", "vec3", "vec2"};
const char* texxyzsize[] = {"xy", "xyz", "xy"};
const char* texsampler[] = {"texture2D", "textureCube", "textureStream"};

const char* fpe_texenvSrc(int src, int tmu, int twosided) {
    static char buff[200];
    switch(src) {
        case FPE_SRC_TEXTURE:
            sprintf(buff, "texColor%d", tmu);
            break;
        case FPE_SRC_TEXTURE0:
        case FPE_SRC_TEXTURE1:
        case FPE_SRC_TEXTURE2:
        case FPE_SRC_TEXTURE3:
        case FPE_SRC_TEXTURE4:
        case FPE_SRC_TEXTURE5:
        case FPE_SRC_TEXTURE6:
        case FPE_SRC_TEXTURE7:
            sprintf(buff, "texColor%d", src-FPE_SRC_TEXTURE0);  // should check if texture is enabled
            break;
        case FPE_SRC_CONSTANT:
            sprintf(buff, "_gl4es_TextureEnvColor_%d", tmu);
            break;
        case FPE_SRC_PRIMARY_COLOR:
            sprintf(buff, "%s", twosided?"((gl_FrontFacing)?Color:BackColor)":"Color");
            break;
        case FPE_SRC_PREVIOUS:
            sprintf(buff, "fColor");
            break;
    }
    return buff;
}

char* fpe_packed(int x, int s, int k) {
    static char buff[8][30];
    static int idx = 0;

    idx&=7;
    int mask = (1<<k)-1;

    const char *hex = "0123456789ABCDEF";

    buff[idx][s] = '\0';
    for (int i; i<s; i++) {
        buff[idx][(s-1)-i] = hex[(x&mask)];
        x>>=k;
    }
    return buff[idx++];
}
char* fpe_binary(int x, int s) {
    return fpe_packed(x, s, 1);
}
    

const char* const* fpe_VertexShader(fpe_state_t *state) {
    // vertex is first called, so 1st time init is only here
    if(!shad_cap) shad_cap = 1024;
    if(!shad) shad = (char*)malloc(shad_cap);
    int lighting = state->lighting;
    int twosided = state->twosided && lighting;
    int light_separate = state->light_separate;
    int secondary = state->colorsum && !(lighting && light_separate);
    int fog = state->fog;
    int fogmode = state->fogmode;
    int fogsource = state->fogsource;
    int color_material = state->color_material;
    int headers = 0;
    int planes = state->plane;
    char buff[1024];

    shad[0] = '\0';

    if(comments) {
        sprintf(buff, "// ** Vertex Shader **\n// ligthting=%d (twosided=%d, separate=%d, color_material=%d)\n// secondary=%d, fog=%d(mode=%d source=%d), planes=%s\n// texture=%s\n",
            lighting, twosided, light_separate, color_material, secondary, fog, fogmode, fogsource, fpe_binary(planes, 6), fpe_packed(state->texture, 16, 2));
        ShadAppend(buff);
        headers+=CountLine(buff);
    }
    ShadAppend("varying vec4 Color;\n");  // might be unused...
    headers++;
    if(twosided) {
        ShadAppend("varying vec4 BackColor;\n");
        headers++;
    }
    if(light_separate || secondary) {
        ShadAppend("varying vec4 SecColor;\n");
        headers++;
        if(twosided) {
            ShadAppend("varying vec4 SecBackColor;\n");
            headers++;
        }
    }
    if(fog) {
        ShadAppend("varying lowp vec4 FogColor;\n");
        headers++;
        ShadAppend("varying float FogF;\n");
        headers++;
    }
    if(planes) {
        ShadAppend("varying vec4 vertex;\n");
        headers++;
    }
    // textures coordinates
    for (int i=0; i<hardext.maxtex; i++) {
        int t = (state->texture>>(i*2))&0x3;
        if(t) {
            sprintf(buff, "varying %s _gl4es_TexCoord_%d;\n", texvecsize[t-1], i);
            ShadAppend(buff);
            headers++;
            if(state->textmat&(1<<i)) {
                sprintf(buff, "uniform mat4 _gl4es_TextureMatrix_%d;\n", i);
                ShadAppend(buff);
                headers++;
            }
        }
    }
    // let's start
    ShadAppend("\nvoid main() {\n");
    // initial Color / lighting calculation
    ShadAppend("gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;\n");
    // lighting and some fog use this
    if(lighting || (fog && fogsource==FPE_FOG_SRC_DEPTH) || planes) {
        if(planes==0)
            ShadAppend("vec4 ");
        ShadAppend("vertex = gl_ModelViewMatrix * gl_Vertex;\n");
    }
    if(!lighting) {
        ShadAppend("Color = gl_Color;\n");
        if(secondary) {
            ShadAppend("SecColor = gl_SecondaryColor;\n");
        }
    } else {
        if(comments) {
            sprintf(buff, "// ColorMaterial On/Off=%d Front = %d Back = %d\n", color_material, state->cm_front_mode, state->cm_back_mode);
            ShadAppend(buff);
        }
        // material emission
        char fm_emission[60], fm_ambient[60], fm_diffuse[60], fm_specular[60];
        char bm_emission[60], bm_ambient[60], bm_diffuse[60], bm_specular[60];
        sprintf(fm_emission, "%s", (color_material && state->cm_front_mode==FPE_CM_EMISSION)?"gl_Color":"gl_FrontMaterial.emission");
        sprintf(fm_ambient, "%s", (color_material && (state->cm_front_mode==FPE_CM_AMBIENT || state->cm_front_mode==FPE_CM_AMBIENTDIFFUSE))?"gl_Color":"gl_FrontMaterial.ambient");
        sprintf(fm_diffuse, "%s", (color_material && (state->cm_front_mode==FPE_CM_DIFFUSE || state->cm_front_mode==FPE_CM_AMBIENTDIFFUSE))?"gl_Color":"gl_FrontMaterial.diffuse");
        sprintf(fm_specular, "%s", (color_material && state->cm_front_mode==FPE_CM_SPECULAR)?"gl_Color":"gl_FrontMaterial.specular");
        if(twosided) {
            sprintf(bm_emission, "%s", (color_material && state->cm_back_mode==FPE_CM_EMISSION)?"gl_Color":"gl_BackMaterial.emission");
            sprintf(bm_ambient, "%s", (color_material && (state->cm_back_mode==FPE_CM_AMBIENT || state->cm_back_mode==FPE_CM_AMBIENTDIFFUSE))?"gl_Color":"gl_BackMaterial.ambient");
            sprintf(bm_diffuse, "%s", (color_material && (state->cm_back_mode==FPE_CM_DIFFUSE || state->cm_back_mode==FPE_CM_AMBIENTDIFFUSE))?"gl_Color":"gl_BackMaterial.diffuse");
            sprintf(bm_specular, "%s", (color_material && state->cm_back_mode==FPE_CM_SPECULAR)?"gl_Color":"gl_BackMaterial.specular");
        }

        sprintf(buff, "Color = %s;\n", fm_emission);
        ShadAppend(buff);
        if(twosided) {
            sprintf(buff, "vec4 BackColor = %s;\n", bm_emission);
            ShadAppend(buff);
        }
        sprintf(buff, "Color += %s*gl_FrontLightModelProduct.sceneColor;\n", fm_ambient);
        ShadAppend(buff);
        if(twosided) {
            sprintf(buff, "Color += %s*gl_BackLightModelProduct.sceneColor;\n", bm_ambient);
            ShadAppend(buff);
        }
        if(light_separate) {
            ShadAppend("SecColor=vec4(0.);\n");
            if(twosided)
                ShadAppend("SecBackColor=vec4(0.);\n");
        }
        ShadAppend("float att;\n");
        ShadAppend("float spot;\n");
        ShadAppend("vec4 VP;\n");
        ShadAppend("float lVP;\n");
        ShadAppend("float nVP;\n");
        ShadAppend("float fi;\n");
        ShadAppend("vec4 aa,dd,ss;\n");
        ShadAppend("vec4 hi;\n");
        if(twosided)
            ShadAppend("vec4 back_aa,back_dd,back_ss;\n");
        ShadAppend("vec3 normal = gl_NormalMatrix * gl_Normal;\n");
        for(int i=0; i<hardext.maxlights; i++) {
            if(state->light&(1<<i)) {
                if(comments) {
                    sprintf(buff, "// light %d on, light_direction=%d, light_cutoff180=%d\n", i, (state->light_direction>>i&1), (state->light_cutoff180>>i&1));
                    ShadAppend(buff);
                }
                // enabled light i
                sprintf(buff, "VP = gl_LightSource[%d].position - vertex;\n", i);
                ShadAppend(buff);
                // att depend on light position w
                if((state->light_direction>>i&1)==0) {
                    ShadAppend("att = 1.0;\n");
                } else {
                    ShadAppend("lVP = length(VP);\n");
                    sprintf(buff, "att = 1.0/(gl_LightSource[%d].constantAttenuation + gl_LightSource[%d].linearAttenuation * lVP + gl_LightSource[%d].quadraticAttenuation * lVP*lVP);\n", i, i, i);
                    ShadAppend(buff);
                }
                ShadAppend("VP = normalize(VP);\n");
                // spot depend on spotlight cutoff angle
                if((state->light_cutoff180>>i&1)==0) {
                    //ShadAppend("spot = 1.0;\n");
                } else {
                    printf(buff, "spot = max(dot(VP.xyz, gl_LightSource[%d].spotDirection), 0.);\n", i);
                    ShadAppend(buff);
                    sprintf(buff, "if(spot<gl_LightSource[%d].spotCosCutoff) spot=0.0; else spot=pow(spot, gl_LightSource[%d].spotExponent);", i, i);
                    ShadAppend(buff);
                    ShadAppend("att *= spot;\n");
                }
                sprintf(buff, "nVP = max(dot(normal, VP.xyz), 0.);fi=(nVP!=0.)?1.:0.;\n");
                ShadAppend(buff);
                sprintf(buff, "aa = %s * gl_LightSource[%d].ambient;\n", fm_ambient, i);
                ShadAppend(buff);
                if(twosided) {
                    sprintf(buff, "back_aa = %s * gl_LightSource[%d].ambient;\n", bm_ambient, i);
                    ShadAppend(buff);
                }
                sprintf(buff, "dd = nVP * %s * gl_LightSource[%d].diffuse;\n", fm_diffuse, i);
                ShadAppend(buff);
                if(twosided) {
                    sprintf(buff, "back_dd = nVP * %s * gl_LightSource[%d].diffuse;\n", bm_diffuse, i);
                    ShadAppend(buff);
                }
                if(state->light_localviewer) {
                    ShadAppend("hi = VP + normalize(-V);\n");
                } else {
                    ShadAppend("hi = VP;\n");
                }
                sprintf(buff, "ss = fi*pow(max(dot(hi.xyz, normal),0.), gl_FrontMaterial.shininess)*%s*gl_LightSource[%d].specular;\n", fm_specular, i);
                ShadAppend(buff);
                if(twosided) {
                    sprintf(buff, "ss = fi*pow(max(dot(hi.xyz, normal),0.), gl_BackMaterial.shininess)*%s*gl_LightSource[%d].specular;\n", bm_specular, i);
                    ShadAppend(buff);
                }
                if(state->light_separate) {
                    ShadAppend("Color += att*(aa+dd);\n");
                    ShadAppend("SecColor += att*(ss);\n");
                    if(twosided) {
                        ShadAppend("BackColor += att*(back_aa+back_dd);\n");
                        ShadAppend("SecBackColor += att*(back_ss);\n");
                    }
                } else {
                    ShadAppend("Color += att*(aa+dd+ss);\n");
                    if(twosided)
                        ShadAppend("BackColor += att*(back_aa+back_dd+back_ss);\n");
                }
                if(comments) {
                    sprintf(buff, "// end of light %d\n", i);
                    ShadAppend(buff);
                }
            }
        }
        sprintf(buff, "Color.a = %s.a;\n", fm_diffuse);
        ShadAppend(buff);
        if(twosided) {
            sprintf(buff, "BackColor.a = %s.a;\n", bm_diffuse);
            ShadAppend(buff);
        }
    }
    // calculate texture coordinates
    if(comments)
        ShadAppend("// texturing\n");
    for (int i=0; i<hardext.maxtex; i++) {
        int t = (state->texture>>(i*2))&0x3;
        int mat = state->textmat&(1<<i)?1:0;
        if(t) {
            if(comments) {
                sprintf(buff, "// texture %d active: %X %s\n", i, t, mat?"with matrix":"");
                ShadAppend(buff);
            }
            if(mat)
                sprintf(buff, "_gl4es_TexCoord_%d = (gl_MultiTexCoord%d * _gl4es_TextureMatrix_%d).%s;\n", i, i, i, texxyzsize[t-1]);
            else
                sprintf(buff, "_gl4es_TexCoord_%d = gl_MultiTexCoord%d.%s;\n", i, i, texxyzsize[t-1]);
            ShadAppend(buff);
        }
    }
    // Fog color
    if(comments)
        ShadAppend("// Fog\n");
    if(fog) {
        if(comments) {
            sprintf(buff, "// Fog On: mode=%X, source=%X\n", fogmode, fogsource);
            ShadAppend(buff);
        }
        sprintf(buff, "float fog_c = %s;\n", fogsource==FPE_FOG_SRC_DEPTH?"abs(vertex.z)":"gl_FogCoord"); // either vertex.z of length(vertex), let's choose the faster here
        ShadAppend(buff);
        switch(fogmode) {
            case FPE_FOG_EXP:
                ShadAppend("FogF = clamp(exp(-gl_Fog.density * fog_c), 0., 1.);\n");
                break;
            case FPE_FOG_EXP2:
                ShadAppend("FogF = clamp(exp(-(gl_Fog.density * fog_c)*(gl_Fog.density * fog_c)), 0., 1.);\n");
                break;
            case FPE_FOG_LINEAR:
                ShadAppend("FogF = clamp((gl_Fog.end - fog_c) * gl_Fog.scale, 0., 1.);\n");
                break;
        }
        ShadAppend("FogColor = gl_Fog.color;\n");
    }

    ShadAppend("}\n");

    return (const char* const*)&shad;
}

const char* const* fpe_FragmentShader(fpe_state_t *state) {
    int headers = 0;
    int lighting = state->lighting;
    int twosided = state->twosided && lighting;
    int light_separate = state->light_separate && lighting;
    int secondary = state->colorsum && !(lighting && light_separate);
    int alpha_test = state->alphatest;
    int alpha_func = state->alphafunc;
    int fog = state->fog;  
    int planes = state->plane;  
    int texenv_combine = 0;
    char buff[1024];

    shad[0] = '\0';

    if(comments) {
        sprintf(buff, "// ** Fragment Shader **\n// lighting=%d, alpha=%d, secondary=%d, planes=%s, texture=%s, texformat=%s\n", lighting, alpha_test, secondary, fpe_binary(planes, 6), fpe_packed(state->texture, 16, 2), fpe_packed(state->texformat, 24, 3));
        ShadAppend(buff);
        headers+=CountLine(buff);
    }
    ShadAppend("varying vec4 Color;\n");
    headers++;
    if(twosided) {
        ShadAppend("varying vec4 BackColor;\n");
        headers++;
    }
    if(light_separate || secondary) {
        ShadAppend("varying vec4 SecColor;\n");
        headers++;
        if(twosided) {
            ShadAppend("varying vec4 SecBackColor;\n");
            headers++;
        }
    }
    if(fog) {
        ShadAppend("varying lowp vec4 FogColor;\n");
        headers++;
        ShadAppend("varying float FogF;\n");
        headers++;
    }
    if(planes) {
        ShadAppend("varying vec4 vertex;\n");
        headers++;
    }
    // textures coordinates
    for (int i=0; i<hardext.maxtex; i++) {
        int t = (state->texture>>(i*2))&0x3;
        if(t) {
            sprintf(buff, "varying %s _gl4es_TexCoord_%d;\n", texvecsize[t-1], i);
            ShadAppend(buff);
            sprintf(buff, "uniform sampler2D _gl4es_TexSampler_%d;\n", i);
            ShadAppend(buff);
            headers++;

            int texenv = (state->texenv>>(i*3))&0x07;
            if (texenv==FPE_COMBINE) {
                texenv_combine = 1;
                if((state->texrgbscale>>i)&1) {
                    sprintf(buff, "uniform float _gl4es_TexEnvRGBScale_%d;\n", i);
                    ShadAppend(buff);
                    headers++;
                }
                if((state->texalphascale>>i)&1) {
                    sprintf(buff, "uniform float _gl4es_TexEnvAlphaScale_%d;\n", i);
                    ShadAppend(buff);
                    headers++;
                }
            }
        }
    }
    if(alpha_test && alpha_func>FPE_NEVER) {
        ShadAppend("uniform float _gl4es_AlphaRef;\n");
        headers++;
    } 

    ShadAppend("void main() {\n");

    //*** Clip Planes (it's probably not the best idea to do that here...)
    if(planes) {
        for (int i=0; i<hardext.maxplanes; i++) {
            if(planes>>i) {
                sprintf(buff, "if(dot(vertex, gl_clipPlane[%d])<0) discard;\n");
                ShadAppend(buff);
            }
        }
    }

    //*** initial color
    sprintf(buff, "vec4 fColor = %s;\n", twosided?"(gl_FrontFacing)?Color:BackColor":"Color");
    ShadAppend(buff);

    //*** apply textures
    if(state->texture) {
        // fetch textures first
        for (int i=0; i<hardext.maxtex; i++) {
            int t = (state->texture>>(i*2))&0x3;
            if(t) {
                sprintf(buff, "vec4 texColor%d = %s(_gl4es_TexSampler_%d, _gl4es_TexCoord_%d);\n", i, texsampler[t-1], i, i);
                ShadAppend(buff);
            }
        }

        // TexEnv stuff
        if(texenv_combine)
            ShadAppend("vec4 Arg0, Arg1, Arg2;\n");
        // fetch textures first
        for (int i=0; i<hardext.maxtex; i++) {
            int t = (state->texture>>(i*2))&0x3;
            if(t) {
                int texenv = (state->texenv>>(i*3))&0x07;
                int texformat = (state->texformat>>(i*3))&0x07;
                if(comments) {
                    sprintf(buff, "// Texture %d active: %X, texenv=%X, format=%X\n", i, t, texenv, texformat);
                    ShadAppend(buff);
                }
                int needclamp = 1;
                switch (texenv) {
                    case FPE_MODULATE:
                        sprintf(buff, "fColor *= texColor%d;\n", i);
                        ShadAppend(buff);
                        needclamp = 0;
                        break;
                    case FPE_ADD:
                        if(texformat!=FPE_TEX_ALPHA) {
                            sprintf(buff, "fColor.rgb += texColor%d.rgb;\n", i);
                            ShadAppend(buff);
                        }
                        if(texformat==FPE_TEX_INTENSITY)
                            sprintf(buff, "fColor.a += texColor%d.a;\n", i);
                        else
                            sprintf(buff, "fColor.a *= texColor%d.a;\n", i);
                        ShadAppend(buff);
                        break;
                    case FPE_DECAL:
                        sprintf(buff, "fColor.rgb = mix(fColor.rgb, texColor%d.rgb, texColor%d.a);\n", i, i);
                        ShadAppend(buff);
                        needclamp = 0;
                        break;
                    case FPE_BLEND:
                        // create the Uniform for TexEnv Constant color
                        sprintf(buff, "uniform lowp vec4 _gl4es_TextureEnvColor_%d;\n", i);
                        shad = ResizeIfNeeded(shad, &shad_cap, strlen(buff));
                        InplaceInsert(GetLine(buff, headers), buff);
                        headers+=CountLine(buff);
                        needclamp=0;
                        if(texformat!=FPE_TEX_ALPHA) {
                            sprintf(buff, "fColor.rgb = mix(fColor.rgb, _gl4es_TextureEnvColor_%d.rgb, texColor%d.rgb);\n", i, i, i);
                            ShadAppend(buff);
                        }
                        switch(texformat) {
                            case FPE_TEX_LUM:
                            case FPE_TEX_RGB:
                                // no change in alpha channel
                                break;
                            case FPE_TEX_INTENSITY:
                                sprintf(buff, "fColor.a = mix(fColor.a, _gl4es_TextureEnvColor_%d.a, texColor%d.a);\n", i, i, i);
                                ShadAppend(buff);
                                break;
                            default:
                                sprintf(buff, "fColor.a *= texColor%d.a;\n", i);
                                ShadAppend(buff);
                        }
                        ShadAppend(buff);
                        break;
                    case FPE_REPLACE:
                        if(texformat==FPE_TEX_RGB || texformat==FPE_TEX_LUM) {
                            sprintf(buff, "fColor.rgb = texColor%d.rgb;\n", i);
                            ShadAppend(buff);
                        } else if(texformat==FPE_TEX_ALPHA) {
                            sprintf(buff, "fColor.a = texColor%d.a;\n", i);
                            ShadAppend(buff);
                        } else {
                            sprintf(buff, "fColor = texColor%d;\n", i);
                            ShadAppend(buff);
                        }
                        break;
                    case FPE_COMBINE:
                        {
                            int constant = 0;
                            // parse the combine state
                            int combine_rgb = state->texcombine[i]&0xf;
                            int combine_alpha = (state->texcombine[i]>>4)&0xf;
                            int src_r[3], op_r[3];
                            int src_a[3], op_a[3];
                            for (int j=0; j<3; j++) {
                                src_a[j] = (state->texsrcalpha[j]>>(i*4))&0xf;
                                op_a[j] = (state->texopalpha[j]>>i)&1;
                                src_r[j] = (state->texsrcrgb[j]>>(i*4))&0xf;
                                op_r[j] = (state->texoprgb[j]>>(i*2))&3;
                            }
                            if(combine_rgb==FPE_CR_DOT3_RGBA) {
                                    src_a[0] = src_a[1] = src_a[2] = -1;
                                    op_a[0] = op_a[1] = op_a[2] = -1;
                                    src_r[2] = op_r[2] = -1;
                            } else {
                                if(combine_alpha==FPE_CR_REPLACE) {
                                    src_a[1] = src_a[2] = -1;
                                    op_a[1] = op_a[2] = -1;
                                } else if (combine_alpha!=FPE_CR_INTERPOLATE) {
                                    src_a[2] = op_a[2] = -1;
                                }
                                if(combine_rgb==FPE_CR_REPLACE) {
                                    src_r[1] = src_r[2] = -1;
                                    op_r[1] = op_r[2] = -1;
                                } else if (combine_rgb!=FPE_CR_INTERPOLATE) {
                                    src_r[2] = op_r[2] = -1;
                                }
                            }
                            // is texture constants needed ?
                            for (int j=0; j<3; j++) {
                                if (src_a[j]==FPE_SRC_CONSTANT)
                                    constant=1;
                            }
                            if(comments) {
                                sprintf(buff, " //  Combine RGB: fct=%d, Src/Op: 0=%d/%d 1=%d/%d 2=%d/%d\n", combine_rgb, src_r[0], op_r[0], src_r[1], op_r[1], src_r[2], op_r[2]);
                                ShadAppend(buff);
                                sprintf(buff, " //  Combine Alpha: fct=%d, Src/Op: 0=%d/%d 1=%d/%d 2=%d/%d\n", combine_alpha, src_a[0], op_a[0], src_a[1], op_a[1], src_a[2], op_a[2]);
                                ShadAppend(buff);
                            }
                            if(constant) {
                                // yep, create the Uniform
                                sprintf(buff, "uniform lowp vec4 _gl4es_TextureEnvColor_%d;\n", i);
                                shad = ResizeIfNeeded(shad, &shad_cap, strlen(buff));
                                InplaceInsert(GetLine(buff, headers), buff);
                                headers+=CountLine(buff);                            
                            }
                            for (int j=0; j<3; j++) {
                                if(op_r[j]!=-1)
                                switch(op_r[j]) {
                                    case FPE_OP_SRCCOLOR:
                                        sprintf(buff, "Arg%d.rgb = %s.rgb;\n", j, fpe_texenvSrc(src_r[0], i, twosided));
                                        break;
                                    case FPE_OP_MINUSCOLOR:
                                        sprintf(buff, "Arg%d.rgb = vec3(1.) - %s.rgb;\n", j, fpe_texenvSrc(src_r[0], i, twosided));
                                        break;
                                    case FPE_OP_ALPHA:
                                        sprintf(buff, "Arg%d.rgb = vec3(%s.a);\n", j, fpe_texenvSrc(src_r[0], i, twosided));
                                        break;
                                    case FPE_OP_MINUSALPHA:
                                        sprintf(buff, "Arg%d.rgb = vec3(1. - %s.a);\n", j, fpe_texenvSrc(src_r[0], i, twosided));
                                        break;
                                }
                                ShadAppend(buff);
                                if(op_a[j]!=-1)
                                switch(op_a[j]) {
                                    case FPE_OP_ALPHA:
                                        sprintf(buff, "Arg%d.a = %s.a;\n", j, fpe_texenvSrc(src_r[0], i, twosided));
                                        break;
                                    case FPE_OP_MINUSALPHA:
                                        sprintf(buff, "Arg%d.a = 1. - %s.a;\n", j, fpe_texenvSrc(src_r[0], i, twosided));
                                        break;
                                }
                                ShadAppend(buff);
                            }
                                
                            switch(combine_rgb) {
                                case FPE_CR_REPLACE:
                                    ShadAppend("fColor.rgb = Arg0.rgb;\n");
                                    break;
                                case FPE_CR_MODULATE:
                                    ShadAppend("fColor.rgb = Arg0.rgb * Arg1.rgb;\n");
                                    break;
                                case FPE_CR_ADD:
                                    ShadAppend("fColor.rgb = Arg0.rgb + Arg1.rgb;\n");
                                    break;
                                case FPE_CR_ADD_SIGNED:
                                    ShadAppend("fColor.rgb = Arg0.rgb + Arg1.rgb - vec3(0.5);\n");
                                    break;
                                case FPE_CR_INTERPOLATE:
                                    ShadAppend("fColor.rgb = Arg0.rgb*Arg2.rgb + Arg1.rgb*(vec3(1.)-Arg2.rgb);\n");
                                    break;
                                case FPE_CR_SUBTRACT:
                                    ShadAppend("fColor.rgb = Arg0.rgb - Arg1.rgb;\n");
                                    break;
                                case FPE_CR_DOT3_RGB:
                                    ShadAppend("fColor.rgb = vec3(4*((Arg0.r-0.5)*(Arg1.r-0.5)+(Arg0.g-0.5)*(Arg1.g-0.5)+(Arg0.b-0.5)*(Arg1.b-0.5)));\n");
                                    break;
                                case FPE_CR_DOT3_RGBA:
                                    ShadAppend("fColor = vec4(4*((Arg0.r-0.5)*(Arg1.r-0.5)+(Arg0.g-0.5)*(Arg1.g-0.5)+(Arg0.b-0.5)*(Arg1.b-0.5)));\n");
                                    break;
                            }
                            if(combine_rgb!=FPE_CR_DOT3_RGBA) 
                            switch(combine_alpha) {
                                case FPE_CR_REPLACE:
                                    ShadAppend("fColor.a = Arg0.a;\n");
                                    break;
                                case FPE_CR_MODULATE:
                                    ShadAppend("fColor.a = Arg0.a * Arg1.a;\n");
                                    break;
                                case FPE_CR_ADD:
                                    ShadAppend("fColor.a = Arg0.a + Arg1.a;\n");
                                    break;
                                case FPE_CR_ADD_SIGNED:
                                    ShadAppend("fColor.a = Arg0.a + Arg1.a - 0.5;\n");
                                    break;
                                case FPE_CR_INTERPOLATE:
                                    ShadAppend("fColor.a = Arg0.a*Arg2.a + Arg1.a*(1.-Arg2.a);\n");
                                    break;
                                case FPE_CR_SUBTRACT:
                                    ShadAppend("fColor.a = Arg0.a - Arg1.a;\n");
                                    break;
                            }
                            if((state->texrgbscale>>i)&1) {
                                sprintf(buff, "fColor.rgb *= _gl4es_TexEnvRGBScale_%d;\n", i);
                                ShadAppend(buff);
                            }
                            if((state->texalphascale>>i)&1) {
                                sprintf(buff, "fColor.a *= _gl4es_TexEnvAlphaScale_%d;\n", i);
                                ShadAppend(buff);
                            }
                        }
                        break;
                }
                if(needclamp)
                    ShadAppend("fColor = clamp(fColor, 0., 1.);\n");
            }
        }
    }
    //*** Alpha Test
    if(alpha_test) {
        if(comments) {
            sprintf(buff, "// Alpha Test, fct=%X\n", alpha_func);
            ShadAppend(buff);
        }
        if(alpha_func==GL_ALWAYS) {
            // nothing here...
        } else if (alpha_func==GL_NEVER) {
            ShadAppend("discard;\n"); // Never pass...
        } else {
            // FPE_LESS FPE_EQUAL FPE_LEQUAL FPE_GREATER FPE_NOTEQUAL FPE_GEQUAL
            // but need to negate the operator
            const char* alpha_test_op[] = {">=","!=",">","<=","==","<"}; 
            sprintf(buff, "if (int(fColor.a*255.) %s int(_gl4es_AlphaRef*255.)) discard;\n", alpha_test_op[alpha_func-FPE_LESS]);
            ShadAppend(buff);
        }
    }

    //*** Add secondary color
    if(light_separate || secondary) {
        if(comments) {
            sprintf(buff, "// Add Secondary color (%s %s)\n", light_separate?"light":"", secondary?"secondary":"");
            ShadAppend(buff);
        }
        sprintf(buff, "fColor += vec4((%s).rgb, 0.);\n", twosided?"(gl_FrontFacing)?SecColor:BackSecColor":"SecColor");
        ShadAppend(buff);
    }

    //*** Fog
    if(fog) {
        if(comments) {
            ShadAppend("// Fog ebabled\n");
        }
        ShadAppend("fColor.rgb = mix(FogColor.rgb, fColor.rgb, FogF);\n");
    }

    //done
    ShadAppend("gl_FragColor = fColor;\n");
    ShadAppend("}");

    return (const char* const*)&shad;
}
