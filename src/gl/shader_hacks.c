#include <string.h>
#include <stdlib.h>
#include "string_utils.h"


static const char* gl4es_hacks[] = {
// this is for Psychonauts (using LIBGL_GL=21)
"#version 120\n"
"vec4 ps_r0;\n"
"vec4 ps_t0 = gl_TexCoord[0];\n",

"#version 120\n"
"vec4 ps_r0;\n"
"#define ps_t0 gl_TexCoord[0]\n",

// this is for Psychonauts (using LIBGL_GL=20)
"#version 110\n"
"vec4 ps_r0;\n"
"vec4 ps_t0 = gl_TexCoord[0];\n",

"#version 110\n"
"vec4 ps_r0;\n"
"#define ps_t0 gl_TexCoord[0]\n",

// this is for Guacamelee (yep, there is a lot of hacks, only int -> float conversions)
"float edgeGlow = step ( 0.2 , pow ( clamp ( ( dot ( vec2 ( 1 * sign ( v_texcoord3 . z ) , 1 ) , normalize ( quadCoord . xy - 0.5 ) ) - 0.4 + depth * 2.0 ) , 0.0  , 1.0  ) , 25 ) ) ;",
"float edgeGlow = step ( 0.2 , pow ( clamp ( ( dot ( vec2 ( 1.0 * sign ( v_texcoord3 . z ) , 1.0 ) , normalize ( quadCoord . xy - 0.5 ) ) - 0.4 + depth * 2.0 ) , 0.0  , 1.0  ) , 25.0 ) ) ;",

"float litfire = max ( dot ( normalize ( drops1 . rgb ) , normalize ( vec3 ( - 1 , 0 , pow ( max ( 1.0 - ocoord . x , 0.0 ) , 9 ) ) ) ) , 0 ) ;",
"float litfire = max ( dot ( normalize ( drops1 . rgb ) , normalize ( vec3 ( - 1.0 , 0.0 , pow ( max ( 1.0 - ocoord . x , 0.0 ) , 9.0 ) ) ) ) , 0.0 ) ;",

"if ( ( normalizedDepth ) < 0.0  ) discard ; ;\nif ( depth < 0 )",
"if ( ( normalizedDepth ) < 0.0  ) discard ; ;\nif ( depth < 0.0 )",

"gl_FragColor . rgba += glowHit ;\nif ( depth < 0 )",
"gl_FragColor . rgba += glowHit ;\nif ( depth < 0.0 )",

"gl_FragColor . a *= pow ( clamp ( ( depth + 1 ) , 0.0  , 1.0  ) , 70 ) ;",
"gl_FragColor . a *= pow ( clamp ( ( depth + 1.0 ) , 0.0  , 1.0  ) , 70.0 ) ;",

"if ( floor ( in_texcoord0 . y ) != 0 )",
"if ( floor ( in_texcoord0 . y ) != 0.0 )",

"if ( in_position0 . y < 0 )",
"if ( in_position0 . y < 0.0 )",

"if ( in_position0 . x < 0 )",
"if ( in_position0 . x < 0.0 )",

"branchB . y = 0 ;",
"branchB . y = 0.0 ;",

"branchB . x = 0 ;",
"branchB . x = 0.0 ;",

// this is for Battle Block Theater
"   if(texColor.w == 0)\n       gl_FragColor = texColor;",
"   if(texColor.w == 0.0)\n       gl_FragColor = texColor;",

"if(dist1 > 0)       {           float lightVal = (1-dist1) * light1Luminosity;",
"if(dist1 > 0.0)       {           float lightVal = (1.0-dist1) * light1Luminosity;",

"float lightVal = 0;",
"float lightVal = 0.0;",

"       if(dist1 > 0)\n"
"       {\n"
"			if(dist1 > 1)\n"
"				dist1 = 1;\n",
"       if(dist1 > 0.0)\n"
"       {\n"
"			if(dist1 > 1.0)\n"
"				dist1 = 1.0;\n",


"lightVal += (1-dist1) * light1Luminosity;",
"lightVal += (1.0-dist1) * light1Luminosity;",

"lightVal += (1-dist1) * light2Luminosity;",
"lightVal += (1.0-dist1) * light2Luminosity;",

"lightVal += (1-dist1) * light3Luminosity;",
"lightVal += (1.0-dist1) * light3Luminosity;",

"if(lightVal > 1)\n"
"			lightVal = 1;",
"if(lightVal > 1.0)\n"
"			lightVal = 1.0;",

"if(lightVal > 1)\n"
"           lightVal = 1;", // space and tabs make a difference...
"if(lightVal > 1.0)\n"
"           lightVal = 1.0;",

// For Night of the Zombie / Irrlicht 1.9.0
"gl_FragColor = (sample*(1-grayScaleFactor)) + (gray*grayScaleFactor);",
"gl_FragColor = (sample*(1.0-grayScaleFactor)) + (gray*grayScaleFactor);",

// For Knytt Underground
"vec2 val = texture_coordinate1+coeff*2*(i/float(iterations-1.0) - 0.5);",
"vec2 val = texture_coordinate1+coeff*2.0*(float(i)/float(iterations-1) - 0.5);",

"    b /= iterations;",
"    b /= float(iterations);",

// For Antichamber
"attribute vec4 _Un_AttrPosition0;\n"
"vec4 Un_AttrPosition0 = _Un_AttrPosition0;\n",
"attribute vec4 _Un_AttrPosition0;\n"
"#define Un_AttrPosition0 _Un_AttrPosition0\n",

"attribute vec4 _Un_AttrColor0;\n"
"vec4 Un_AttrColor0 = _Un_AttrColor0;\n",
"attribute vec4 _Un_AttrColor0;\n"
"#define Un_AttrColor0 _Un_AttrColor0\n",

"attribute vec4 _Un_AttrColor1;\n"
"vec4 Un_AttrColor1 = _Un_AttrColor1;\n",
"attribute vec4 _Un_AttrColor1;\n"
"#define Un_AttrColor1 _Un_AttrColor1\n",

"attribute vec4 _Un_AttrTangent0;\n"
"vec4 Un_AttrTangent0 = _Un_AttrTangent0;\n",
"attribute vec4 _Un_AttrTangent0;\n"
"#define Un_AttrTangent0 _Un_AttrTangent0\n",

"attribute vec4 _Un_AttrNormal0;\n"
"vec4 Un_AttrNormal0 = _Un_AttrNormal0;\n",
"attribute vec4 _Un_AttrNormal0;\n"
"#define Un_AttrNormal0 _Un_AttrNormal0\n",

"attribute vec4 _Un_AttrBlendIndices0;\n"
"vec4 Un_AttrBlendIndices0 = _Un_AttrBlendIndices0;\n",
"attribute vec4 _Un_AttrBlendIndices0;\n"
"#define Un_AttrBlendIndices0 _Un_AttrBlendIndices0\n",

"attribute vec4 _Un_AttrBlendWeight0;\n"
"vec4 Un_AttrBlendWeight0 = _Un_AttrBlendWeight0;\n",
"attribute vec4 _Un_AttrBlendWeight0;\n"
"#define Un_AttrBlendWeight0 _Un_AttrBlendWeight0\n",

"attribute vec4 _Un_AttrBinormal0;\n"
"vec4 Un_AttrBinormal0 = _Un_AttrBinormal0;\n",
"attribute vec4 _Un_AttrBinormal0;\n"
"#define Un_AttrBinormal0 _Un_AttrBinormal0\n",

"attribute vec4 _Un_AttrTexCoord0;\n"
"vec4 Un_AttrTexCoord0 = _Un_AttrTexCoord0;\n",
"attribute vec4 _Un_AttrTexCoord0;\n"
"#define Un_AttrTexCoord0 _Un_AttrTexCoord0\n",

"attribute vec4 _Un_AttrTexCoord1;\n"
"vec4 Un_AttrTexCoord1 = _Un_AttrTexCoord1;\n",
"attribute vec4 _Un_AttrTexCoord1;\n"
"#define Un_AttrTexCoord1 _Un_AttrTexCoord1\n",

"attribute vec4 _Un_AttrTexCoord2;\n"
"vec4 Un_AttrTexCoord2 = _Un_AttrTexCoord2;\n",
"attribute vec4 _Un_AttrTexCoord2;\n"
"#define Un_AttrTexCoord2 _Un_AttrTexCoord2\n",

"attribute vec4 _Un_AttrTexCoord3;\n"
"vec4 Un_AttrTexCoord3 = _Un_AttrTexCoord3;\n",
"attribute vec4 _Un_AttrTexCoord3;\n"
"#define Un_AttrTexCoord3 _Un_AttrTexCoord3\n",

"attribute vec4 _Un_AttrTexCoord4;\n"
"vec4 Un_AttrTexCoord4 = _Un_AttrTexCoord4;\n",
"attribute vec4 _Un_AttrTexCoord4;\n"
"#define Un_AttrTexCoord4 _Un_AttrTexCoord4\n",

"attribute vec4 _Un_AttrTexCoord5;\n"
"vec4 Un_AttrTexCoord5 = _Un_AttrTexCoord5;\n",
"attribute vec4 _Un_AttrTexCoord5;\n"
"#define Un_AttrTexCoord5 _Un_AttrTexCoord5\n",

"attribute vec4 _Un_AttrTexCoord6;\n"
"vec4 Un_AttrTexCoord6 = _Un_AttrTexCoord6;\n",
"attribute vec4 _Un_AttrTexCoord6;\n"
"#define Un_AttrTexCoord6 _Un_AttrTexCoord6\n",

"attribute vec4 _Un_AttrTexCoord7;\n"
"vec4 Un_AttrTexCoord7 = _Un_AttrTexCoord7;\n",
"attribute vec4 _Un_AttrTexCoord7;\n"
"#define Un_AttrTexCoord7 _Un_AttrTexCoord7\n",

};

// For Stellaris
static const char* gl4es_sign_1[] = {
"if (Data.Type == 1)",
"if (Data.BlendMode == 0)",
};
static const char* gl4es_hacks_1[] = {
"if (Data.Type == 1)",
"if (Data.Type == 1.0)",

"if (Data.Type == 2)",
"if (Data.Type == 2.0)",

"if (Data.Type == 3)",
"if (Data.Type == 3.0)",

"if (Data.BlendMode == 0)",
"if (Data.BlendMode == 0.0)",

"if (Data.BlendMode == 1)",
"if (Data.BlendMode == 1.0)",

"if (Data.BlendMode == 2)",
"if (Data.BlendMode == 2.0)",

"Out.vMaskingTexCoord = saturate(v.vTexCoord * 1000);",
"Out.vMaskingTexCoord = saturate(v.vTexCoord * 1000.0);",

"float vTime = 0.9 - saturate( (Time - AnimationTime) * 4 );",
"float vTime = 0.9 - saturate( (Time - AnimationTime) * 4.0 );",

"float vTime = 0.9 - saturate( (Time - AnimationTime) * 16 );",
"float vTime = 0.9 - saturate( (Time - AnimationTime) * 16.0 );",
};

static char* ShaderHacks_1(char* shader, char* Tmp, int* tmpsize)
{
    // check for all signature first
    for (int i=0; i<sizeof(gl4es_sign_1)/sizeof(gl4es_sign_1[0]); i++)
        if(!strstr(Tmp, gl4es_sign_1[i]))
            return Tmp;
    // Do the replace
    for (int i=0; i<sizeof(gl4es_hacks_1)/sizeof(gl4es_hacks_1[0]); i+=2)
        if(strstr(Tmp, gl4es_hacks_1[i])) {
            if(Tmp==shader) {Tmp = malloc(*tmpsize); strcpy(Tmp, shader);}   // hacking!
            Tmp = InplaceReplaceSimple(Tmp, tmpsize, gl4es_hacks_1[i], gl4es_hacks_1[i+1]);
        }
    return Tmp;
}

char* ShaderHacks(char* shader)
{
    char* Tmp = shader;
    int tmpsize = strlen(Tmp)+10;
    // specific hacks
    Tmp = ShaderHacks_1(shader, Tmp, &tmpsize);
    // generic
    for (int i=0; i<sizeof(gl4es_hacks)/sizeof(gl4es_hacks[0]); i+=2)
        if(strstr(Tmp, gl4es_hacks[i])) {
            if(Tmp==shader) {Tmp = malloc(tmpsize); strcpy(Tmp, shader);}   // hacking!
            Tmp = InplaceReplaceSimple(Tmp, &tmpsize, gl4es_hacks[i], gl4es_hacks[i+1]);
        }
    return Tmp;
}