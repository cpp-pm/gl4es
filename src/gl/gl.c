#include "gl.h"
#include "debug.h"
#include "../glx/hardext.h"
/*
glstate_t state = {.color = {1.0f, 1.0f, 1.0f, 1.0f},
	.secondary = {0.0f, 0.0f, 0.0f, 0.0f},
	.render_mode = 0,
	.projection_matrix = NULL,
	.modelview_matrix = NULL,
	.texture_matrix = NULL,
	.namestack = {0, NULL},
	.enable.vertex_array = 0,
	.enable.color_array = 0,
	.enable.secondary_array = 0,
	.enable.normal_array = 0,
	.texture.client = 0,
	.texture.active = 0,
	.buffers = {NULL, NULL, NULL, NULL},
	.shim_error = 0,
	.last_error = GL_NO_ERROR,
    .gl_batch = 0
	};
*/
glstate_t *glstate = NULL;

glstate_t *default_glstate = NULL;

GLuint readhack = 0;
GLint readhack_x = 0;
GLint readhack_y = 0;
GLfloat readhack_depth = 0.0f;
GLuint readhack_seq = 0;
GLuint gl_batch = 0;
GLuint gl_mergelist = 1;
int blendhack = 0;
int export_blendcolor = 0;
char glshim_version[50];
int initialized = 0;
int glshim_noerror = 0;
int glshim_nobanner = 0;
int glshim_npot = 0;

void* NewGLState(void* shared_glstate) {
    if(shared_glstate) {
        glstate_t* glstate = (glstate_t*)shared_glstate;
        glstate->shared_cnt++;
        return (void*)glstate;
    }
    glstate_t *glstate = (glstate_t*)malloc(sizeof(glstate_t));
	GLfloat white[] = {1.0f, 1.0f, 1.0f, 1.0f};
	memset(glstate, 0, sizeof(glstate_t));
	memcpy(glstate->color, white, sizeof(GLfloat)*4);
	glstate->last_error = GL_NO_ERROR;
    glstate->normal[3] = 1.0f; // default normal is 0/0/1
    
    // add default VBO
    {
        khint_t k;
        int ret;
        khash_t(buff) *list = glstate->buffers = kh_init(buff);
        k = kh_put(buff, list, 0, &ret);
        glbuffer_t *buff = kh_value(list, k) = malloc(sizeof(glbuffer_t));
        buff->buffer = 0;
        buff->type = 0;
        buff->data = NULL;
        buff->usage = GL_STATIC_DRAW;
        buff->size = 0;
        buff->access = GL_READ_WRITE;
        buff->mapped = 0;
        glstate->defaultvbo = buff;
    }
    // add default VAO
    {
        khint_t k;
        int ret;
        khash_t(glvao) *list = glstate->vaos = kh_init(glvao);
        k = kh_put(glvao, list, 0, &ret);
        glvao_t *glvao = kh_value(list, k) = malloc(sizeof(glvao_t));
        // new vao is binded to default vbo
        memset(glvao, 0, sizeof(glvao_t));
        // just put is number
        glvao->array = 0;
        glstate->defaultvao = glvao;
    }
    // Bind defaults...
    glstate->vao = glstate->defaultvao;

    glstate->shared_cnt = 0;

    glstate->gl_batch = gl_batch;

    return (void*)glstate;
}


void DeleteGLState(void* oldstate) {
    glstate_t* state = (glstate_t*)state;
    if(!state) return;

    if(state->shared_cnt) {
        --state->shared_cnt;
        return;
    }

    if(glstate == state)
        glstate = NULL;


    #define free_hashmap(T, N, K)           \
    {                                       \
        T *m;                               \
        kh_foreach_value(state->N, m,       \
            free(m);                        \
        )                                   \
        kh_destroy(K, state->N);            \
    }
    free_hashmap(glvao_t, vaos, glvao);
    free_hashmap(glbuffer_t, buffers, buff);
    free_hashmap(glquery_t, queries, queries);
    free_hashmap(gltexture_t, texture.list, tex);
    #undef free_hashmap
    // free eval maps
    #define freemap(dims, name)                              \
    { map_statef_t *m = (map_statef_t *)state->map##dims.name; \
    if (m) {                                                \
        if (m->free)                                        \
            free((void *)m->points);                        \
        free(m);                                            \
    } }
    freemap(1, vertex3); freemap(1, vertex4); freemap(1, index); freemap(1, color4); freemap(1, normal); 
    freemap(1, texture1); freemap(1, texture2); freemap(1, texture3); freemap(1, texture4);   
    freemap(2, vertex3); freemap(2, vertex4); freemap(2, index); freemap(2, color4); freemap(2, normal); 
    freemap(2, texture1); freemap(2, texture2); freemap(2, texture3); freemap(2, texture4);   
    #undef freemap
    // free lists
    if(state->lists) {
        for (int i=0; i<state->list.count; i++)
            free_renderlist(state->lists[i]);
        free(state->lists);
    }
    if(state->list.active) free_renderlist(state->list.active);

    // free matrix stack
    #define free_matrix(A)                  \
        if (state->A) {                   \
    	    free(state->A->stack);    \
            free(state->A);               \
        }
	free_matrix(projection_matrix);
	free_matrix(modelview_matrix);
	for (int i=0; i<MAX_TEX; i++)
		free_matrix(texture_matrix[i]);
	free(glstate->texture_matrix);
    #undef free_matrix
    // probably missing some things to free here!

    // all done
    free(state);
    return;
}

void ActivateGLState(void* new_glstate) {
    if(glstate == (glstate_t*)new_glstate) return;  // same state, nothing to do
    if (glstate && glstate->gl_batch) flush();
    glstate = (new_glstate)?(glstate_t*)new_glstate:default_glstate;
    if (gl_batch && glstate->init_batch==0) init_batch();
}

__attribute__((constructor))
void initialize_glshim() {
    char *env_nobanner = getenv("LIBGL_NOBANNER");
    if (env_nobanner && strcmp(env_nobanner, "1") == 0)
        glshim_nobanner = 1;

	if(!glshim_nobanner) LOGD("LIBGL: Initialising glshim\n");
	
    // init read hack 
    char *env_readhack = getenv("LIBGL_READHACK");
    if (env_readhack && strcmp(env_readhack, "1") == 0) {
        readhack = 1;
        LOGD("LIBGL: glReadPixel Hack (for other-life, 1x1 reading multiple time)\n");
    }
    if (env_readhack && strcmp(env_readhack, "2") == 0) {
        readhack = 2;
        LOGD("LIBGL: glReadPixel Depth Hack (for games that read GLDepth always at the same place, same 1x1 size)\n");
    }
    char *env_batch = getenv("LIBGL_BATCH");
    if (env_batch && strcmp(env_batch, "1") == 0) {
        gl_batch = 1;
        LOGD("LIBGL: Batch mode enabled\n");
    }
    if (env_batch && strcmp(env_batch, "0") == 0) {
        gl_batch = 0;
        LOGD("LIBGL: Batch mode disabled\n");
    }
    if (env_batch && strcmp(env_batch, "2") == 0) {
        gl_batch = 0;
        gl_mergelist = 0;
        LOGD("LIBGL: Batch mode disabled, merging of list disabled too\n");
    }
    
    default_glstate = (glstate_t*)NewGLState(NULL);
    ActivateGLState(default_glstate);
    
    initialized = 1;
}

// config functions
const GLubyte *glshim_glGetString(GLenum name) {
//    LOAD_GLES(glGetString);
    const GLubyte *str;
    errorShim(GL_NO_ERROR);
	static GLubyte *extensions = NULL;
	if(!extensions) {
		extensions = (GLubyte*)malloc(5000);	// arbitrary size...
		strcpy(extensions,
				"GL_EXT_abgr "
                "GL_EXT_packed_pixels "
                "GL_EXT_compiled_vertex_array "
                "GL_ARB_vertex_buffer_object "
                "GL_ARB_vertex_array_object "
                "GL_ARB_vertex_buffer "
                "GL_EXT_vertex_array "
                "GL_EXT_secondary_color "
                "GL_EXT_texture_env_combine "
                "GL_ARB_multitexture "
                "GL_ARB_texture_env_add "
                "GL_ARB_texture_border_clamp "
                "GL_ARB_point_parameters "
                "GL_EXT_texture_env_add "
                "GL_ARB_texture_env_combine "
                "GL_ARB_texture_env_crossbar "
                "GL_ARB_texture_env_dot3 "
                "GL_ARB_texture_mirrored_repeat "
                "GL_SGIS_generate_mipmap "
                "GL_EXT_packed_depth_stencil "
                "GL_EXT_draw_range_elements "
                "GL_EXT_bgra "
                "GL_ARB_texture_compression "
                "GL_EXT_texture_compression_s3tc "
                "GL_OES_texture_compression_S3TC "
                "GL_EXT_texture_compression_dxt3 "
                "GL_EXT_texture_compression_dxt5 "
                "GL_EXT_texture_compression_dxt1 "
                "GL_ARB_point_parameters "
                "GL_EXT_point_parameters "
                "GL_EXT_stencil_wrap "
                "SGIS_texture_edge_clamp "
                "GL_EXT_texture_edge_clamp "
                "GL_EXT_direct_state_access "
                "GL_EXT_multi_draw_arrays "
                "GL_SUN_multi_draw_arrays "
//                "GL_EXT_blend_logic_op "
//                "GL_ARB_texture_cube_map "
				);
		if(glshim_npot>=1)
			strcat(extensions, "GL_APPLE_texture_2D_limited_npot ");
		if(glshim_npot>=2)
			strcat(extensions, "GL_ARB_texture_non_power_of_two ");
        if(hardext.blendcolor)
            strcat(extensions, "GL_EXT_blend_color");
        if(hardext.blendminmax)
            strcat(extensions, "GL_EXT_blend_minmax");
        if(hardext.blendeq)
            strcat(extensions, "GL_EXT_blend_equation_separate");
        if(hardext.blendfunc)
            strcat(extensions, "GL_EXT_blend_func_separate");
        if(hardext.blendsub)
            strcat(extensions, "GL_EXT_blend_subtract");
        if(hardext.fbo)
            strcat(extensions,                 
                "GL_ARB_framebuffer_object "
                "GL_EXT_framebuffer_object "
                "GL_EXT_packed_depth_stencil "
                "GL_ARB_draw_buffers ");
	}
    switch (name) {
        case GL_VERSION:
            return (GLubyte *)glshim_version;
        case GL_EXTENSIONS:
            return extensions;
		case GL_VENDOR:
			return (GLubyte *)"ptitSeb";
		case GL_RENDERER:
			return (GLubyte *)"GLES_CM wrapper";
		case GL_SHADING_LANGUAGE_VERSION:
			return (GLubyte *)"";
        default:
			errorShim(GL_INVALID_ENUM);
            return (str)?str:(GLubyte*)"";
    }
}
const GLubyte *glGetString(GLenum name) AliasExport("glshim_glGetString");

void transposeMatrix(float *matrix)
{
    float tmp[16];
    memcpy(tmp, matrix, sizeof(tmp));
    for (int i=0; i<4; i++)
        for (int j=0; j<4; j++)
            matrix[i*4+j]=tmp[j*4+i];
}

// glGet
extern float raster_zoomx, raster_zoomy;
extern GLfloat raster_scale[4];
extern GLfloat raster_bias[4];

void glshim_glGetIntegerv(GLenum pname, GLint *params) {
    if (params==NULL) {
        errorShim(GL_INVALID_OPERATION);
        return;
    }
    GLint dummy;
    LOAD_GLES(glGetIntegerv);
    if (glstate->list.active && (glstate->gl_batch && !glstate->list.compiling)) flush();
    noerrorShim();
    switch (pname) {
        case GL_MAX_ELEMENTS_INDICES:
            *params = 1024;
            break;
        case GL_MAX_ELEMENTS_VERTICES:
			*params = 4096;
			break;
        case GL_AUX_BUFFERS:
            *params = 0;
            break;
        case GL_MAX_DRAW_BUFFERS_ARB:   // fake...
            *params = 1;
            break;
        case GL_UNPACK_ROW_LENGTH:	
			*params = glstate->texture.unpack_row_length;
			break;
        case GL_UNPACK_SKIP_PIXELS:
			*params = glstate->texture.unpack_skip_pixels;
			break;
        case GL_UNPACK_SKIP_ROWS:
			*params = glstate->texture.unpack_skip_rows;
			break;
        case GL_UNPACK_LSB_FIRST:
			*params = glstate->texture.unpack_lsb_first;
			break;
        case GL_UNPACK_IMAGE_HEIGHT:
            *params = glstate->texture.unpack_image_height;
            break;
        case GL_PACK_ROW_LENGTH:	
			*params = glstate->texture.pack_row_length;
			break;
        case GL_PACK_SKIP_PIXELS:
			*params = glstate->texture.pack_skip_pixels;
			break;
        case GL_PACK_SKIP_ROWS:
			*params = glstate->texture.pack_skip_rows;
			break;
        case GL_PACK_LSB_FIRST:
			*params = glstate->texture.pack_lsb_first;
			break;
        case GL_PACK_IMAGE_HEIGHT:
            *params = glstate->texture.pack_image_height;
            break;
        case GL_UNPACK_SWAP_BYTES:
        case GL_PACK_SWAP_BYTES:
            //Fake, *TODO* ?
			*params = 0;
			break;
	case GL_POINT_SIZE_RANGE:
			gles_glGetIntegerv(GL_POINT_SIZE_MIN, params);
			gles_glGetIntegerv(GL_POINT_SIZE_MAX, params+1);
			break;
	case GL_RENDER_MODE:
			*params = (glstate->render_mode)?glstate->render_mode:GL_RENDER;
			break;
	case GL_NAME_STACK_DEPTH:
			*params = glstate->namestack.top;
			break;
	case GL_MAX_NAME_STACK_DEPTH:
			*params = 1024;
			break;
	case GL_MAX_TEXTURE_IMAGE_UNITS:
			/*gles_glGetIntegerv(GL_MAX_TEXTURE_UNITS, params);*/
			*params = 4;
			break;
	case GL_NUM_COMPRESSED_TEXTURE_FORMATS:
			gles_glGetIntegerv(GL_NUM_COMPRESSED_TEXTURE_FORMATS, params);
			(*params)+=4;	// adding fake DXTc
			break;
	case GL_COMPRESSED_TEXTURE_FORMATS:
			gles_glGetIntegerv(GL_NUM_COMPRESSED_TEXTURE_FORMATS, &dummy);
			// get standard ones
			gles_glGetIntegerv(GL_COMPRESSED_TEXTURE_FORMATS, params);
			// add fake DXTc
			params[dummy++]=GL_COMPRESSED_RGB_S3TC_DXT1_EXT;
			params[dummy++]=GL_COMPRESSED_RGBA_S3TC_DXT1_EXT;
			params[dummy++]=GL_COMPRESSED_RGBA_S3TC_DXT3_EXT;
			params[dummy++]=GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
			break;
	case GL_MAX_MODELVIEW_STACK_DEPTH:
			*params=MAX_STACK_MODELVIEW;
			break;
	case GL_MAX_PROJECTION_STACK_DEPTH:
			*params=MAX_STACK_PROJECTION;
			break;
	case GL_MAX_TEXTURE_STACK_DEPTH:
			*params=MAX_STACK_TEXTURE;
			break;
	case GL_MODELVIEW_STACK_DEPTH:
			*params=(glstate->modelview_matrix)?(glstate->modelview_matrix->top+1):1;
			break;
	case GL_PROJECTION_STACK_DEPTH:
			*params=(glstate->projection_matrix)?(glstate->projection_matrix->top+1):1;
			break;
	case GL_TEXTURE_STACK_DEPTH:
			*params=(glstate->texture_matrix)?(glstate->texture_matrix[glstate->texture.active]->top+1):1;
			break;
	case GL_MAX_LIST_NESTING:
			*params=64;	// fake, no limit in fact
			break;
	case  GL_ARRAY_BUFFER_BINDING:
			*params=(glstate->vao->vertex)?glstate->vao->vertex->buffer:0;
			break;
	case  GL_ELEMENT_ARRAY_BUFFER_BINDING:
			*params=(glstate->vao->elements)?glstate->vao->elements->buffer:0;
			break;
	case  GL_PIXEL_PACK_BUFFER_BINDING:
			*params=(glstate->vao->pack)?glstate->vao->pack->buffer:0;
			break;
	case  GL_PIXEL_UNPACK_BUFFER_BINDING:
			*params=(glstate->vao->unpack)?glstate->vao->unpack->buffer:0;
			break;
    default:
			errorGL();
            gles_glGetIntegerv(pname, params);
    }
}
void glGetIntegerv(GLenum pname, GLint *params) AliasExport("glshim_glGetIntegerv");

void glshim_glGetFloatv(GLenum pname, GLfloat *params) {
    LOAD_GLES(glGetFloatv);
    if (glstate->list.active && (glstate->gl_batch && !glstate->list.compiling)) flush();
    noerrorShim();
    switch (pname) {
        case GL_MAX_ELEMENTS_INDICES:
            *params = 1024;
            break;
        case GL_MAX_ELEMENTS_VERTICES:
	    *params = 4096;
	    break;
        case GL_AUX_BUFFERS:
            *params = 0;
            break;
        case GL_UNPACK_ROW_LENGTH:	
	    *params = glstate->texture.unpack_row_length;
	    break;
        case GL_UNPACK_SKIP_PIXELS:
	    *params = glstate->texture.unpack_skip_pixels;
	    break;
        case GL_UNPACK_SKIP_ROWS:
	    *params = glstate->texture.unpack_skip_rows;
	    break;
        case GL_UNPACK_LSB_FIRST:
	    *params = glstate->texture.unpack_lsb_first;
	    break;
        case GL_PACK_ROW_LENGTH:	
	    *params = glstate->texture.pack_row_length;
	    break;
        case GL_PACK_SKIP_PIXELS:
	    *params = glstate->texture.pack_skip_pixels;
	    break;
        case GL_PACK_SKIP_ROWS:
	    *params = glstate->texture.pack_skip_rows;
	    break;
        case GL_PACK_LSB_FIRST:
	    *params = glstate->texture.pack_lsb_first;
	    break;
        case GL_ZOOM_X:
	    *params = raster_zoomx;
	    break;
        case GL_ZOOM_Y:
	    *params = raster_zoomy;
	    break;
        case GL_RED_SCALE:
	    *params = raster_scale[0];
	    break;
        case GL_RED_BIAS:
	    *params = raster_bias[0];
	    break;
        case GL_GREEN_SCALE:
        case GL_BLUE_SCALE:
        case GL_ALPHA_SCALE:
	    *params = raster_scale[(pname-GL_GREEN_SCALE)/2+1];
	    break;
        case GL_GREEN_BIAS:
        case GL_BLUE_BIAS:
        case GL_ALPHA_BIAS:
	    *params = raster_bias[(pname-GL_GREEN_BIAS)/2+1];
	    break;
	case GL_POINT_SIZE_RANGE:
	    gles_glGetFloatv(GL_POINT_SIZE_MIN, params);
	    gles_glGetFloatv(GL_POINT_SIZE_MAX, params+1);
	    break;
	case GL_RENDER_MODE:
	    *params = (glstate->render_mode)?glstate->render_mode:GL_RENDER;
	    break;
	case GL_NAME_STACK_DEPTH:
	    *params = glstate->namestack.top;
	    break;
	case GL_MAX_NAME_STACK_DEPTH:
	    *params = 1024;
	    break;
	case GL_MAX_MODELVIEW_STACK_DEPTH:
	    *params=MAX_STACK_MODELVIEW;
	    break;
	case GL_MAX_PROJECTION_STACK_DEPTH:
	    *params=MAX_STACK_PROJECTION;
	    break;
	case GL_MAX_TEXTURE_STACK_DEPTH:
	    *params=MAX_STACK_TEXTURE;
	    break;
	case GL_MODELVIEW_STACK_DEPTH:
	    *params=(glstate->modelview_matrix)?(glstate->modelview_matrix->top+1):1;
	    break;
	case GL_PROJECTION_STACK_DEPTH:
	    *params=(glstate->projection_matrix)?(glstate->projection_matrix->top+1):1;
	    break;
	case GL_TEXTURE_STACK_DEPTH:
	    *params=(glstate->texture_matrix)?(glstate->texture_matrix[glstate->texture.active]->top+1):1;
	    break;
	case GL_MAX_LIST_NESTING:
	    *params=64;	// fake, no limit in fact
	    break;
	case  GL_ARRAY_BUFFER_BINDING:
		*params=(glstate->vao->vertex)?glstate->vao->vertex->buffer:0;
		break;
	case  GL_ELEMENT_ARRAY_BUFFER_BINDING:
		*params=(glstate->vao->elements)?glstate->vao->elements->buffer:0;
		break;
	case  GL_PIXEL_PACK_BUFFER_BINDING:
        *params=(glstate->vao->pack)?glstate->vao->pack->buffer:0;
		break;
	case  GL_PIXEL_UNPACK_BUFFER_BINDING:
		*params=(glstate->vao->unpack)?glstate->vao->unpack->buffer:0;
		break;
    case GL_TRANSPOSE_PROJECTION_MATRIX:
        gles_glGetFloatv(GL_PROJECTION_MATRIX, params);
        transposeMatrix(params);
        break;
    case GL_TRANSPOSE_MODELVIEW_MATRIX:
        gles_glGetFloatv(GL_MODELVIEW_MATRIX, params);
        transposeMatrix(params);
        break;
    case GL_TRANSPOSE_TEXTURE_MATRIX:
        gles_glGetFloatv(GL_TEXTURE_MATRIX, params);
        transposeMatrix(params);
        break;
    default:
		errorGL();
		gles_glGetFloatv(pname, params);
    }
}
void glGetFloatv(GLenum pname, GLfloat *params) AliasExport("glshim_glGetFloatv");

extern int alphahack;
extern int texstream;

#ifndef GL_TEXTURE_STREAM_IMG  
#define GL_TEXTURE_STREAM_IMG                                   0x8C0D     
#endif

static void proxy_glEnable(GLenum cap, bool enable, void (*next)(GLenum)) {
    #define proxy_enable(constant, name) \
        case constant: glstate->enable.name = enable; next(cap); break
    #define enable(constant, name) \
        case constant: glstate->enable.name = enable; break;
    #define proxy_clientenable(constant, name) \
        case constant: glstate->vao->name = enable; next(cap); break
    #define clientenable(constant, name) \
        case constant: glstate->vao->name = enable; break;

    // TODO: maybe could be weird behavior if someone tried to:
    // 1. enable GL_TEXTURE_1D
    // 2. enable GL_TEXTURE_2D
    // 3. disable GL_TEXTURE_1D
    // 4. render. GL_TEXTURE_2D would be disabled.
    // cap = map_tex_target(cap);
    
    // Alpha Hack
    if (alphahack && (cap==GL_ALPHA_TEST) && enable) {
        if (glstate->texture.bound[glstate->texture.active])
            if (!glstate->texture.bound[glstate->texture.active]->alpha)
                enable = false;
    }
	noerrorShim();
#ifdef TEXSTREAM
    if (cap==GL_TEXTURE_STREAM_IMG)
        glstate->enable.texture_2d[glstate->texture.active] = enable;
#endif
    switch (cap) {
        enable(GL_AUTO_NORMAL, auto_normal);
        proxy_enable(GL_BLEND, blend);
        proxy_enable(GL_TEXTURE_2D, texture_2d[glstate->texture.active]);
        enable(GL_TEXTURE_GEN_S, texgen_s[glstate->texture.active]);
        enable(GL_TEXTURE_GEN_T, texgen_t[glstate->texture.active]);
        enable(GL_TEXTURE_GEN_R, texgen_r[glstate->texture.active]);
        enable(GL_TEXTURE_GEN_Q, texgen_q[glstate->texture.active]);
        enable(GL_LINE_STIPPLE, line_stipple);
        
        // Secondary color
        enable(GL_COLOR_SUM, color_sum);
        clientenable(GL_SECONDARY_COLOR_ARRAY, secondary_array);
	
        // for glDrawArrays
        clientenable(GL_VERTEX_ARRAY, vertex_array);
        clientenable(GL_NORMAL_ARRAY, normal_array);
        clientenable(GL_COLOR_ARRAY, color_array);
        clientenable(GL_TEXTURE_COORD_ARRAY, tex_coord_array[glstate->texture.client]);
        
        // Texture 1D and 3D
        enable(GL_TEXTURE_1D, texture_1d[glstate->texture.active]);
        enable(GL_TEXTURE_3D, texture_3d[glstate->texture.active]);
        
        default: errorGL(); next(cap); break;
    }
    #undef proxy_enable
    #undef enable
    #undef proxy_clientenable
    #undef clientenable
}

int Cap2BatchState(GLenum cap) {
        switch (cap) {
            case GL_ALPHA_TEST: return ENABLED_ALPHA;
            case GL_BLEND: return ENABLED_BLEND;
            case GL_CULL_FACE: return ENABLED_CULL;
            case GL_DEPTH_TEST: return ENABLED_DEPTH;
            case GL_TEXTURE_2D: return  ENABLED_TEX2D_TEX0
                +(glstate->statebatch.active_tex_changed?glstate->statebatch.active_tex-GL_TEXTURE0:glstate->texture.active); 
        }
        return ENABLED_LAST;
}

void glshim_glEnable(GLenum cap) {
    if (glstate->list.active && (glstate->gl_batch && !glstate->list.compiling))  {
        int which_cap = Cap2BatchState(cap);
        if (which_cap!=ENABLED_LAST) {
            if ((glstate->statebatch.enabled[which_cap] == 1))
                return; // nothing to do...
            if (!glstate->statebatch.enabled[which_cap]) {
                glstate->statebatch.enabled[which_cap] = 1;
            } else {
                flush();
            }
        }
    }
	PUSH_IF_COMPILING(glEnable)
        
	if (texstream && (cap==GL_TEXTURE_2D)) {
		if (glstate->texture.bound[glstate->texture.active])
			if (glstate->texture.bound[glstate->texture.active]->streamed)
				cap = GL_TEXTURE_STREAM_IMG;
	}

    LOAD_GLES(glEnable);
    proxy_glEnable(cap, true, gles_glEnable);
}
void glEnable(GLenum cap) AliasExport("glshim_glEnable");

void glshim_glDisable(GLenum cap) {
    if (glstate->list.active && (glstate->gl_batch && !glstate->list.compiling))  {
        int which_cap = Cap2BatchState(cap);
        if (which_cap!=ENABLED_LAST) {
            if ((glstate->statebatch.enabled[which_cap] == 2))
                return; // nothing to do...
            if (!glstate->statebatch.enabled[which_cap]) {
                glstate->statebatch.enabled[which_cap] = 2;
            } else {
                flush();
            }
        }
    }
	PUSH_IF_COMPILING(glDisable)
        
	if (texstream && (cap==GL_TEXTURE_2D)) {
		if (glstate->texture.bound[glstate->texture.active])
			if (glstate->texture.bound[glstate->texture.active]->streamed)
				cap = GL_TEXTURE_STREAM_IMG;
	}

    LOAD_GLES(glDisable);
    proxy_glEnable(cap, false, gles_glDisable);
}
void glDisable(GLenum cap) AliasExport("glshim_glDisable");

void glshim_glEnableClientState(GLenum cap) {
    LOAD_GLES(glEnableClientState);
    proxy_glEnable(cap, true, gles_glEnableClientState);
}
void glEnableClientState(GLenum cap) AliasExport("glshim_glEnableClientState");

void glshim_glDisableClientState(GLenum cap) {
    LOAD_GLES(glDisableClientState);
    proxy_glEnable(cap, false, gles_glDisableClientState);
}
void glDisableClientState(GLenum cap) AliasExport("glshim_glDisableClientState");


#define isenabled(what, where) \
    case what: return glstate->enable.where
#define clientisenabled(what, where) \
    case what: return glstate->vao->where
    
GLboolean glshim_glIsEnabled(GLenum cap) {
    // should flush for now... to be optimized later!
    if (glstate->gl_batch) flush();
    LOAD_GLES(glIsEnabled);
    noerrorShim();
    switch (cap) {
        isenabled(GL_AUTO_NORMAL, auto_normal);
        isenabled(GL_LINE_STIPPLE, line_stipple);
        isenabled(GL_TEXTURE_GEN_S, texgen_s[glstate->texture.active]);
        isenabled(GL_TEXTURE_GEN_T, texgen_t[glstate->texture.active]);
        isenabled(GL_TEXTURE_GEN_R, texgen_r[glstate->texture.active]);
        isenabled(GL_TEXTURE_GEN_Q, texgen_q[glstate->texture.active]);
		isenabled(GL_COLOR_SUM, color_sum);
		clientisenabled(GL_SECONDARY_COLOR_ARRAY, secondary_array);
        isenabled(GL_TEXTURE_1D, texture_1d[glstate->texture.active]);
        isenabled(GL_TEXTURE_3D, texture_3d[glstate->texture.active]);
        clientisenabled(GL_VERTEX_ARRAY, vertex_array);
        clientisenabled(GL_NORMAL_ARRAY, normal_array);
        clientisenabled(GL_COLOR_ARRAY, color_array);
        clientisenabled(GL_TEXTURE_COORD_ARRAY, tex_coord_array[glstate->texture.client]);
        default:
			errorGL();
            return gles_glIsEnabled(cap);
    }
}
#undef isenabled
#undef clientisenabled
GLboolean glIsEnabled(GLenum cap) AliasExport("glshim_glIsEnabled");

static renderlist_t *arrays_to_renderlist(renderlist_t *list, GLenum mode,
                                        GLsizei skip, GLsizei count) {
    if (! list)
        list = alloc_renderlist();
//if (glstate->list.compiling) LOGD("arrary_to_renderlist while compiling list, skip=%d, count=%d\n", skip, count);
    list->mode = mode;
    list->mode_init = mode;
    list->len = count-skip;
    list->cap = count-skip;
    
	if (glstate->vao->vertex_array) {
		list->vert = copy_gl_pointer_tex(&glstate->vao->pointers.vertex, 4, skip, count);
	}
	if (glstate->vao->color_array) {
		list->color = copy_gl_pointer_color(&glstate->vao->pointers.color, 4, skip, count);
	}
	if (glstate->vao->secondary_array/* && glstate->enable.color_array*/) {
		list->secondary = copy_gl_pointer(&glstate->vao->pointers.secondary, 4, skip, count);		// alpha chanel is always 0 for secondary...
	}
	if (glstate->vao->normal_array) {
		list->normal = copy_gl_pointer_raw(&glstate->vao->pointers.normal, 3, skip, count);
	}
	for (int i=0; i<MAX_TEX; i++) {
		if (glstate->vao->tex_coord_array[i]) {
		    list->tex[i] = copy_gl_pointer_tex(&glstate->vao->pointers.tex_coord[i], 4, skip, count);
		}
	}
    return list;
}

static inline bool should_intercept_render(GLenum mode) {
    // check bounded tex that will be used if one need some transformations
    for (int aa=0; aa<MAX_TEX; aa++) {
        if (glstate->enable.texture_2d[aa] || glstate->enable.texture_1d[aa] || glstate->enable.texture_3d[aa]) {
            if(glstate->texture.rect_arb[aa])
                return true;
            gltexture_t *bound = glstate->texture.bound[aa];
            if (bound && (bound->width!=bound->nwidth || bound->height!=bound->nheight))
                return true;
            if ((glstate->enable.texgen_s[0] || glstate->enable.texgen_t[0] || glstate->enable.texgen_r[0] || glstate->enable.texgen_q[0]))
                return true;
        }
    }
    if(glstate->polygon_mode == GL_LINE && mode>=GL_TRIANGLES)
        return true;
    if ((glstate->vao->secondary_array) && (glstate->vao->color_array))
        return true;
    if (glstate->vao->color_array && (glstate->vao->pointers.color.size != 4))
        return true;
    return (
        (glstate->vao->vertex_array && ! valid_vertex_type(glstate->vao->pointers.vertex.type)) ||
        (mode == GL_LINES && glstate->enable.line_stipple) ||
        (mode == GL_QUADS) || (glstate->list.active && (glstate->list.compiling || glstate->gl_batch))
    );
}

void glshim_glDrawElements(GLenum mode, GLsizei count, GLenum type, const GLvoid *indices) {
    //printf("glDrawElements(%s, %d, %s, %p), vtx=%p map=%p\n", PrintEnum(mode), count, PrintEnum(type), indices, (glstate->vao->vertex)?glstate->vao->vertex->data:NULL, (glstate->vao->elements)?glstate->vao->elements->data:NULL);
    // TODO: split for count > 65535?
    // special check for QUADS and TRIANGLES that need multiple of 4 or 3 vertex...
    if (mode == GL_QUADS) while(count%4) count--;
    else if (mode == GL_TRIANGLES) while(count%3) count--;
    
    if (count<0) {
		errorShim(GL_INVALID_VALUE);
		return;
	}
    if (count==0) {
        noerrorShim();
        return;
    }

	noerrorShim();
    GLushort *sindices;
    bool need_free = (type!=GL_UNSIGNED_SHORT);
    if(need_free)
        sindices = copy_gl_array((glstate->vao->elements)?glstate->vao->elements->data + (uintptr_t)indices:indices,
            type, 1, 0, GL_UNSIGNED_SHORT, 1, 0, count);
    else
        sindices = (glstate->vao->elements)?(glstate->vao->elements->data + (uintptr_t)indices):(GLvoid*)indices;
    bool compiling = (glstate->list.active && (glstate->list.compiling || glstate->gl_batch));

    if (compiling) {
        renderlist_t *list = NULL;
        GLsizei min, max;

		NewStage(glstate->list.active, STAGE_DRAW);
        list = glstate->list.active;

        normalize_indices(sindices, &max, &min, count);
        list = arrays_to_renderlist(list, mode, min, max + 1);
        list->indices = (need_free)?sindices:copy_gl_array(sindices, type, 1, 0, GL_UNSIGNED_SHORT, 1, 0, count);
        list->ilen = count;
        list->indice_cap = count;
        //end_renderlist(list);
        
        glstate->list.active = extend_renderlist(list);
        return;
     }

     if (should_intercept_render(mode)) {
        renderlist_t *list = NULL;
        GLsizei min, max;

        normalize_indices(sindices, &max, &min, count);
        list = arrays_to_renderlist(list, mode, min, max + 1);
        list->indices = (need_free)?sindices:copy_gl_array(sindices, type, 1, 0, GL_UNSIGNED_SHORT, 1, 0, count);
        list->ilen = count;
        list->indice_cap = count;
        list = end_renderlist(list);
        draw_renderlist(list);
        free_renderlist(list);
        
        return;
     } else {
		LOAD_GLES(glDrawElements);
		LOAD_GLES(glNormalPointer);
		LOAD_GLES(glVertexPointer);
		LOAD_GLES(glColorPointer);
		LOAD_GLES(glTexCoordPointer);
        LOAD_GLES(glEnable);
        LOAD_GLES(glDisable);
        LOAD_GLES(glEnableClientState);
        LOAD_GLES(glDisableClientState);
        GLuint len = 0;
        for (int i=0; i<count; i++)
            if (len<sindices[i]) len = sindices[i]; // get the len of the arrays
        len++;  // lenght is max(indices) + 1 !
#define client_state(A, B, C) \
            if(glstate->vao->A != glstate->clientstate.A) {           \
                C                                              \
                if((glstate->clientstate.A = glstate->vao->A)==true)  \
                    gles_glEnableClientState(B);                \
                else                                            \
                    gles_glDisableClientState(B);               \
            }


		GLenum mode_init = mode;
		if (glstate->polygon_mode == GL_LINE && mode>=GL_TRIANGLES)
			mode = GL_LINE_LOOP;
		if (glstate->polygon_mode == GL_POINT && mode>=GL_TRIANGLES)
			mode = GL_POINTS;

		if (mode == GL_QUAD_STRIP)
			mode = GL_TRIANGLE_STRIP;
		if (mode == GL_POLYGON)
			mode = GL_TRIANGLE_FAN;
		if (glstate->render_mode == GL_SELECT) {
			select_glDrawElements(&glstate->vao->pointers.vertex, mode, count, GL_UNSIGNED_SHORT, sindices);
		} else {
            // secondry color and color sizef != 4 are "intercepted" and draw using a list
            if (glstate->vao->color_array)
				gles_glColorPointer(glstate->vao->pointers.color.size, glstate->vao->pointers.color.type, glstate->vao->pointers.color.stride, glstate->vao->pointers.color.pointer);
            client_state(normal_array, GL_NORMAL_ARRAY, );
			if (glstate->vao->normal_array)
				gles_glNormalPointer(glstate->vao->pointers.normal.type, glstate->vao->pointers.normal.stride, glstate->vao->pointers.normal.pointer);
            client_state(vertex_array, GL_VERTEX_ARRAY, );
			if (glstate->vao->vertex_array)
				gles_glVertexPointer(glstate->vao->pointers.vertex.size, glstate->vao->pointers.vertex.type, glstate->vao->pointers.vertex.stride, glstate->vao->pointers.vertex.pointer);
			GLuint old_tex = glstate->texture.client;
            #define TEXTURE(A) glshim_glClientActiveTexture(A+GL_TEXTURE0);
            for (int aa=0; aa<MAX_TEX; aa++) {
                client_state(tex_coord_array[aa], GL_TEXTURE_COORD_ARRAY, TEXTURE(aa););
                if (!glstate->enable.texture_2d[aa] && (glstate->enable.texture_1d[aa] || glstate->enable.texture_3d[aa])) {
                    TEXTURE(aa);
                    gles_glEnable(GL_TEXTURE_2D);
                }
                if (glstate->vao->tex_coord_array[aa]) {
                    TEXTURE(aa);
                    tex_setup_texcoord(len);
                }
            }
			if (glstate->texture.client!=old_tex)
				TEXTURE(old_tex);
			// POLYGON mode as LINE is "intercepted" and drawn using list
			gles_glDrawElements(mode, count, GL_UNSIGNED_SHORT, sindices);
			
			for (int aa=0; aa<MAX_TEX; aa++) {
                if (!glstate->enable.texture_2d[aa] && (glstate->enable.texture_1d[aa] || glstate->enable.texture_3d[aa])) {
                    TEXTURE(aa);
                    gles_glDisable(GL_TEXTURE_2D);
                }
            }
			if (glstate->texture.client!=old_tex)
				TEXTURE(old_tex);
            #undef TEXTURE
		}
        if(need_free)
            free(sindices);
    }
}
void glDrawElements(GLenum mode, GLsizei count, GLenum type, const GLvoid *indices) AliasExport("glshim_glDrawElements");

void glshim_glDrawArrays(GLenum mode, GLint first, GLsizei count) {
    // special check for QUADS and TRIANGLES that need multiple of 4 or 3 vertex...
    if (mode == GL_QUADS) while(count%4) count--;
    else if (mode == GL_TRIANGLES) while(count%3) count--;

	if (count<0) {
		errorShim(GL_INVALID_VALUE);
		return;
	}
    if (count==0) {
        noerrorShim();
        return;
    }
    // special case for (very) large GL_QUADS array
    if ((mode==GL_QUADS) && (count>4*8000)) {
        // split the array in manageable slice
        int cnt = 4*8000;
        for (int i=0; i<count; i+=4*8000) {
            if (i+cnt>count) cnt = count-i;
            glshim_glDrawArrays(mode, i, cnt);
        }
        return;
    }
	noerrorShim();
	LOAD_GLES(glNormalPointer);
	LOAD_GLES(glVertexPointer);
	LOAD_GLES(glColorPointer);
	LOAD_GLES(glTexCoordPointer);
    LOAD_GLES(glEnable);
    LOAD_GLES(glDisable);
    LOAD_GLES(glEnableClientState);
    LOAD_GLES(glDisableClientState);

    if (glstate->list.active && (glstate->list.compiling || glstate->gl_batch)) {
        NewStage(glstate->list.active, STAGE_DRAW);
        glstate->list.active = arrays_to_renderlist(glstate->list.active, mode, first, count+first);
        glstate->list.active = extend_renderlist(glstate->list.active);
        return;
    }

    if (glstate->polygon_mode == GL_LINE && mode>=GL_TRIANGLES)
		mode = GL_LINE_LOOP;
    if (glstate->polygon_mode == GL_POINT && mode>=GL_TRIANGLES)
		mode = GL_POINTS;

    if (should_intercept_render(mode)) {
        renderlist_t *list;
        list = arrays_to_renderlist(NULL, mode, first, count+first);
        list = end_renderlist(list);
        draw_renderlist(list);
        free_renderlist(list);
    } else {
        // TODO: some draw states require us to use the full pipeline here
        // like texgen, stipple, npot
        LOAD_GLES(glDrawArrays);

		GLenum mode_init = mode;
		if (mode == GL_QUAD_STRIP)
			mode = GL_TRIANGLE_STRIP;
		if (mode == GL_POLYGON)
			mode = GL_TRIANGLE_FAN;
			
		if (glstate->render_mode == GL_SELECT) {
			select_glDrawArrays(&glstate->vao->pointers.vertex, mode, first, count);
		} else {
			// setup the Array Pointers
            if (glstate->vao->color_array)
				gles_glColorPointer(glstate->vao->pointers.color.size, glstate->vao->pointers.color.type, glstate->vao->pointers.color.stride, glstate->vao->pointers.color.pointer);
            client_state(normal_array, GL_NORMAL_ARRAY, );
			if (glstate->vao->normal_array)
				gles_glNormalPointer(glstate->vao->pointers.normal.type, glstate->vao->pointers.normal.stride, glstate->vao->pointers.normal.pointer);
            client_state(vertex_array, GL_VERTEX_ARRAY, );
			if (glstate->vao->vertex_array)
				gles_glVertexPointer(glstate->vao->pointers.vertex.size, glstate->vao->pointers.vertex.type, glstate->vao->pointers.vertex.stride, glstate->vao->pointers.vertex.pointer);
			GLuint old_tex = glstate->texture.client;
            #define TEXTURE(A) glshim_glClientActiveTexture(A+GL_TEXTURE0);
            for (int aa=0; aa<MAX_TEX; aa++) {
                client_state(tex_coord_array[aa], GL_TEXTURE_COORD_ARRAY, TEXTURE(aa););
                if (!glstate->enable.texture_2d[aa] && (glstate->enable.texture_1d[aa] || glstate->enable.texture_3d[aa])) {
                    TEXTURE(aa);
                    gles_glEnable(GL_TEXTURE_2D);
                }
				if (glstate->vao->tex_coord_array[aa]) {
                    TEXTURE(aa);
                    tex_setup_texcoord(count+first);
					/*glClientActiveTexture(aa+GL_TEXTURE0);
					gles_glTexCoordPointer(glstate->pointers.tex_coord[aa].size, glstate->pointers.tex_coord[aa].type, glstate->pointers.tex_coord[aa].stride, glstate->pointers.tex_coord[aa].pointer);*/
				}
            }
			if (glstate->texture.client!=old_tex)
				TEXTURE(old_tex);

			gles_glDrawArrays(mode, first, count);
			
			for (int aa=0; aa<MAX_TEX; aa++) {
                if (!glstate->enable.texture_2d[aa] && (glstate->enable.texture_1d[aa] || glstate->enable.texture_3d[aa])) {
                    TEXTURE(aa);
                    gles_glDisable(GL_TEXTURE_2D);
                }
            }
			if (glstate->texture.client!=old_tex)
				TEXTURE(old_tex);
            #undef TEXTURE
		}
    }
}
#undef client_state
void glDrawArrays(GLenum mode, GLint first, GLsizei count) AliasExport("glshim_glDrawArrays");

#ifndef USE_ES2
#define clone_gl_pointer(t, s)\
    t.size = s; t.type = type; t.stride = stride; t.pointer = pointer + (uintptr_t)((glstate->vao->vertex)?glstate->vao->vertex->data:0)
void glshim_glVertexPointer(GLint size, GLenum type,
                     GLsizei stride, const GLvoid *pointer) {
    noerrorShim();
    clone_gl_pointer(glstate->vao->pointers.vertex, size);
}
void glshim_glColorPointer(GLint size, GLenum type,
                     GLsizei stride, const GLvoid *pointer) {
    noerrorShim();
    clone_gl_pointer(glstate->vao->pointers.color, size);
}
void glshim_glNormalPointer(GLenum type, GLsizei stride, const GLvoid *pointer) {
    noerrorShim();
    clone_gl_pointer(glstate->vao->pointers.normal, 3);
}
void glshim_glTexCoordPointer(GLint size, GLenum type,
                     GLsizei stride, const GLvoid *pointer) {
    noerrorShim();
    clone_gl_pointer(glstate->vao->pointers.tex_coord[glstate->texture.client], size);
}
void glshim_glSecondaryColorPointer(GLint size, GLenum type, 
					GLsizei stride, const GLvoid *pointer) {
	if (size!=3)
		return;		// Size must be 3...
    clone_gl_pointer(glstate->vao->pointers.secondary, size);
    noerrorShim();
}

#undef clone_gl_pointer
#endif
void glVertexPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer) AliasExport("glshim_glVertexPointer");
void glColorPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer) AliasExport("glshim_glColorPointer");
void glNormalPointer(GLenum type, GLsizei stride, const GLvoid *pointer) AliasExport("glshim_glNormalPointer");
void glTexCoordPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer) AliasExport("glshim_glTexCoordPointer");
void glSecondaryColorPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer) AliasExport("glshim_glSecondaryColorPointer");

void glshim_glInterleavedArrays(GLenum format, GLsizei stride, const GLvoid *pointer) {
    uintptr_t ptr = (uintptr_t)pointer;
    // element lengths
    GLsizei tex=0, color=0, normal=0, vert=0;
    // element formats
    GLenum tf, cf, nf, vf;
    tf = cf = nf = vf = GL_FLOAT;
    noerrorShim();
    switch (format) {
        case GL_V2F: vert = 2; break;
        case GL_V3F: vert = 3; break;
        case GL_C4UB_V2F:
            color = 4; cf = GL_UNSIGNED_BYTE;
            vert = 2;
            break;
        case GL_C4UB_V3F:
            color = 4; cf = GL_UNSIGNED_BYTE;
            vert = 3;
            break;
        case GL_C3F_V3F:
            color = 3;
            vert = 4;
            break;
        case GL_N3F_V3F:
            normal = 3;
            vert = 3;
            break;
        case GL_C4F_N3F_V3F:
            color = 4;
            normal = 3;
            vert = 3;
            break;
        case GL_T2F_V3F:
            tex = 2;
            vert = 3;
            break;
        case GL_T4F_V4F:
            tex = 4;
            vert = 4;
            break;
        case GL_T2F_C4UB_V3F:
            tex = 2;
            color = 4; cf = GL_UNSIGNED_BYTE;
            vert = 3;
            break;
        case GL_T2F_C3F_V3F:
            tex = 2;
            color = 3;
            vert = 3;
            break;
        case GL_T2F_N3F_V3F:
            tex = 2;
            normal = 3;
            vert = 3;
            break;
        case GL_T2F_C4F_N3F_V3F:
            tex = 2;
            color = 4;
            normal = 3;
            vert = 3;
            break;
        case GL_T4F_C4F_N3F_V4F:
            tex = 4;
            color = 4;
            normal = 3;
            vert = 4;
            break;
        default:
            errorShim(GL_INVALID_ENUM);
            return;
    }
    if (!stride)
        stride = tex * gl_sizeof(tf) +
                 color * gl_sizeof(cf) +
                 normal * gl_sizeof(nf) +
                 vert * gl_sizeof(vf);
    if (tex) {
		glshim_glEnableClientState(GL_TEXTURE_COORD_ARRAY);
        glshim_glTexCoordPointer(tex, tf, stride, (GLvoid *)ptr);
        ptr += tex * gl_sizeof(tf);
    }
    if (color) {
		glshim_glEnableClientState(GL_COLOR_ARRAY);
        glshim_glColorPointer(color, cf, stride, (GLvoid *)ptr);
        ptr += color * gl_sizeof(cf);
    }
    if (normal) {
		glshim_glEnableClientState(GL_NORMAL_ARRAY);
        glshim_glNormalPointer(nf, stride, (GLvoid *)ptr);
        ptr += normal * gl_sizeof(nf);
    }
    if (vert) {
		glshim_glEnableClientState(GL_VERTEX_ARRAY);
        glshim_glVertexPointer(vert, vf, stride, (GLvoid *)ptr);
    }
}
void glInterleavedArrays(GLenum format, GLsizei stride, const GLvoid *pointer) AliasExport("glshim_glInterleavedArrays");

// immediate mode functions
void glshim_glBegin(GLenum mode) {
    if (!glstate->list.active)
        glstate->list.active = alloc_renderlist();
    NewStage(glstate->list.active, STAGE_DRAW);
    glstate->list.active->mode = mode;
    glstate->list.active->mode_init = mode;
    noerrorShim();	// TODO, check Enum validity
}
void glBegin(GLenum mode) AliasExport("glshim_glBegin");

void glshim_glEnd() {
    if (!glstate->list.active) return;
    // check if TEXTUREx is activate and no TexCoord (or texgen), in that case, create a dummy one base on glstate->..
    for (int a=0; a<MAX_TEX; a++)
		if (glstate->enable.texture_2d[a] && ((glstate->list.active->tex[a]==0) && (!glstate->enable.texgen_s[a])))
			rlMultiTexCoord4f(glstate->list.active, GL_TEXTURE0+a, glstate->texcoord[a][0], glstate->texcoord[a][1], glstate->texcoord[a][2], glstate->texcoord[a][3]);
    // render if we're not in a display list
    if (!(glstate->list.compiling || glstate->gl_batch)) {
        renderlist_t *mylist = glstate->list.active;
        glstate->list.active = NULL;
        mylist = end_renderlist(mylist);
        draw_renderlist(mylist);
        free_renderlist(mylist);
    } else {
        glstate->list.active = extend_renderlist(glstate->list.active);
    }
    noerrorShim();
}
void glEnd() AliasExport("glshim_glEnd");

void glshim_glNormal3f(GLfloat nx, GLfloat ny, GLfloat nz) {
    if (glstate->list.active) {
        if (glstate->list.active->stage != STAGE_DRAW) {
            if (glstate->list.active->stage != STAGE_DRAW) {
                if ((glstate->list.compiling || glstate->gl_batch) && glstate->list.active) {
                    glstate->list.active->lastNormal[0] = nx; glstate->list.active->lastNormal[1] = ny; glstate->list.active->lastNormal[2] = nz;
                }
                PUSH_IF_COMPILING(glNormal3f);
            }
        } else {
            rlNormal3f(glstate->list.active, nx, ny, nz);
            glstate->list.active->lastNormal[0] = nx; glstate->list.active->lastNormal[1] = ny; glstate->list.active->lastNormal[2] = nz;
            noerrorShim();
        }
    }
#ifndef USE_ES2
    else {
        LOAD_GLES(glNormal3f);
        gles_glNormal3f(nx, ny, nz);
        errorGL();
    }
#endif
    glstate->normal[0] = nx; glstate->normal[1] = ny; glstate->normal[2] = nz;
}
void glNormal3f(GLfloat nx, GLfloat ny, GLfloat nz) AliasExport("glshim_glNormal3f");

void glshim_glVertex4f(GLfloat x, GLfloat y, GLfloat z, GLfloat w) {
    if (glstate->list.active) {
        rlVertex4f(glstate->list.active, x, y, z, w);
        noerrorShim();
    }
}
void glVertex4f(GLfloat x, GLfloat y, GLfloat z, GLfloat w) AliasExport("glshim_glVertex4f");

void glshim_glColor4f(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha) {
    if (glstate->list.active) {
        if (glstate->list.active->stage != STAGE_DRAW) {
            glstate->list.active->lastColors[0] = red; glstate->list.active->lastColors[1] = green;
            glstate->list.active->lastColors[2] = blue; glstate->list.active->lastColors[3] = alpha;
            glstate->list.active->lastColorsSet = 1;
            PUSH_IF_COMPILING(glColor4f);
        }
        rlColor4f(glstate->list.active, red, green, blue, alpha);
        noerrorShim();
    }
#ifndef USE_ES2
    else {
        LOAD_GLES(glColor4f);
        gles_glColor4f(red, green, blue, alpha);
        errorGL();
    }
#endif
    // change the state last thing
    glstate->color[0] = red; glstate->color[1] = green;
    glstate->color[2] = blue; glstate->color[3] = alpha;
}
void glColor4f(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha) AliasExport("glshim_glColor4f");

void glshim_glSecondaryColor3f(GLfloat r, GLfloat g, GLfloat b) {
    if (glstate->list.active) {
        rlSecondary3f(glstate->list.active, r, g, b);
        glstate->list.active->lastSecondaryColors[0] = r; glstate->list.active->lastSecondaryColors[1] = g;
        glstate->list.active->lastSecondaryColors[2] = b;
        noerrorShim();
    } else {
        noerrorShim();
    }
    // change the state last thing
    glstate->secondary[0] = r; glstate->secondary[1] = g;
    glstate->secondary[2] = b;
}
void glSecondaryColor3f(GLfloat r, GLfloat g, GLfloat b) AliasExport("glshim_glSecondaryColor3f");

#ifndef USE_ES2
void glshim_glMaterialfv(GLenum face, GLenum pname, const GLfloat *params) {
    LOAD_GLES(glMaterialfv);
    if ((glstate->list.compiling || glstate->gl_batch) && glstate->list.active) {
		//TODO: Materialfv can be done per vertex, how to handle that ?!
		//NewStage(glstate->list.active, STAGE_MATERIAL);
        rlMaterialfv(glstate->list.active, face, pname, params);
        noerrorShim();
    } else {
	    if (face!=GL_FRONT_AND_BACK) {
		    face=GL_FRONT_AND_BACK;
		}
        gles_glMaterialfv(face, pname, params);
        errorGL();
    }
}
void glMaterialfv(GLenum face, GLenum pname, const GLfloat *params) AliasExport("glshim_glMaterialfv");
void glshim_glMaterialf(GLenum face, GLenum pname, const GLfloat param) {
    LOAD_GLES(glMaterialf);
    if ((glstate->list.compiling || glstate->gl_batch) && glstate->list.active) {
		GLfloat params[4];
		memset(params, 0, 4*sizeof(GLfloat));
		params[0] = param;
		NewStage(glstate->list.active, STAGE_MATERIAL);
        rlMaterialfv(glstate->list.active, face, pname, params);
        noerrorShim();
    } else {
	    if (face!=GL_FRONT_AND_BACK) {
		    face=GL_FRONT_AND_BACK;
		}
        gles_glMaterialf(face, pname, param);
        errorGL();
    }
}
void glMaterialf(GLenum face, GLenum pname, const GLfloat param) AliasExport("glshim_glMaterialf");
#endif

void glshim_glTexCoord4f(GLfloat s, GLfloat t, GLfloat r, GLfloat q) {
    if (glstate->list.active) {
        rlTexCoord4f(glstate->list.active, s, t, r, q);
    }
    noerrorShim();
    glstate->texcoord[0][0] = s; glstate->texcoord[0][1] = t;
    glstate->texcoord[0][2] = r; glstate->texcoord[0][3] = q;
}
void glTexCoord4f(GLfloat s, GLfloat t, GLfloat r, GLfloat q) AliasExport("glshim_glTexCoord4f");

void glshim_glMultiTexCoord4f(GLenum target, GLfloat s, GLfloat t, GLfloat r, GLfloat q) {
	// TODO, error if target is unsuported texture....
    if (glstate->list.active) {
        rlMultiTexCoord4f(glstate->list.active, target, s, t, r, q);
    }
    noerrorShim();
    glstate->texcoord[target-GL_TEXTURE0][0] = s; glstate->texcoord[target-GL_TEXTURE0][1] = t;
    glstate->texcoord[target-GL_TEXTURE0][2] = r; glstate->texcoord[target-GL_TEXTURE0][3] = q;
}
void glMultiTexCoord4f(GLenum target, GLfloat s, GLfloat t, GLfloat r, GLfloat q) AliasExport("glshim_glMultiTexCoord4f");
void glMultiTexCoord4fARB(GLenum target, GLfloat s, GLfloat t, GLfloat r, GLfloat q) AliasExport("glshim_glMultiTexCoord4f");

void glshim_glArrayElement(GLint i) {
    GLfloat *v;
    pointer_state_t *p;
    p = &glstate->vao->pointers.color;
    if (glstate->vao->color_array) {
        v = gl_pointer_index(p, i);
        GLfloat scale = 1.0f/gl_max_value(p->type);
        // color[3] defaults to 1.0f
        if (p->size < 4)
            v[3] = 1.0f;

        // scale color coordinates to a 0 - 1.0 range
        for (int i = 0; i < p->size; i++) {
            v[i] *= scale;
        }
        glshim_glColor4fv(v);
    }
    p = &glstate->vao->pointers.secondary;
    if (glstate->vao->secondary_array) {
        v = gl_pointer_index(p, i);
        GLfloat scale = 1.0f/gl_max_value(p->type);

        // scale color coordinates to a 0 - 1.0 range
        for (int i = 0; i < p->size; i++) {
            v[i] *= scale;
        }
        glshim_glSecondaryColor3fv(v);
    }
    p = &glstate->vao->pointers.normal;
    if (glstate->vao->normal_array) {
        v = gl_pointer_index(p, i);
        glshim_glNormal3fv(v);
    }
    p = &glstate->vao->pointers.tex_coord[0];
    if (glstate->vao->tex_coord_array[0]) {
        v = gl_pointer_index(p, i);
        if (p->size<4)
            glshim_glTexCoord2fv(v);
        else
            glshim_glTexCoord4fv(v);
    }
    int a;
    for (a=1; a<MAX_TEX; a++) {
	    p = &glstate->vao->pointers.tex_coord[a];
	    if (glstate->vao->tex_coord_array[a]) {
			v = gl_pointer_index(p, i);
            if (p->size<4)
                glshim_glMultiTexCoord2fv(GL_TEXTURE0+a, v);
            else
                glshim_glMultiTexCoord4fv(GL_TEXTURE0+a, v);
	    }
    }
    p = &glstate->vao->pointers.vertex;
    if (glstate->vao->vertex_array) {
        v = gl_pointer_index(p, i);
        if (p->size == 4) {
            glshim_glVertex4fv(v);
        } else if (p->size == 3) {
            glshim_glVertex3fv(v);
        } else {
            glshim_glVertex2fv(v);
        }
    }
}
void glArrayElement(GLint i) AliasExport("glshim_glArrayElement");

// TODO: between a lock and unlock, I can assume the array pointers are unchanged
// so I can build a renderlist_t on the first call and hold onto it
// maybe I need a way to call a renderlist_t with (first, count)
void glshim_glLockArrays(GLint first, GLsizei count) {
    glstate->list.locked = true;
    noerrorShim();
}
void glLockArraysEXT(GLint first, GLsizei count) AliasExport("glshim_glLockArrays");
void glshim_glUnlockArrays() {
    glstate->list.locked = false;
    noerrorShim();
}
void glUnlockArraysEXT() AliasExport("glshim_glUnlockArrays");
// display lists

static renderlist_t *glshim_glGetList(GLuint list) {
    if (glIsList(list))
        return glstate->lists[list - 1];

    return NULL;
}

GLuint glshim_glGenLists(GLsizei range) {
	if (range<0) {
		errorShim(GL_INVALID_VALUE);
		return 0;
	}
	noerrorShim();
    int start = glstate->list.count;
    if (glstate->lists == NULL) {
        glstate->list.cap += range + 100;
        glstate->lists = malloc(glstate->list.cap * sizeof(uintptr_t));
    } else if (glstate->list.count + range > glstate->list.cap) {
        glstate->list.cap += range + 100;
        glstate->lists = realloc(glstate->lists, glstate->list.cap * sizeof(uintptr_t));
    }
    glstate->list.count += range;

    for (int i = 0; i < range; i++) {
        glstate->lists[start+i] = NULL;
    }
    return start + 1;
}
GLuint glGenLists(GLsizei range) AliasExport("glshim_glGenLists");


void glshim_glNewList(GLuint list, GLenum mode) {
	errorShim(GL_INVALID_VALUE);
	if (list==0)
		return;
    if (! glIsList(list))
        return;
    noerrorShim();
    if (glstate->gl_batch) {
        glstate->gl_batch = 0;
        flush();
    }
    glstate->list.name = list;
    glstate->list.mode = mode;
    // TODO: if glstate->list.active is already defined, we probably need to clean up here
    glstate->list.active = alloc_renderlist();
    glstate->list.compiling = true;
}
void glNewList(GLuint list, GLenum mode) AliasExport("glshim_glNewList");

void glshim_glEndList() {
	noerrorShim();
    GLuint list = glstate->list.name;
    if (glstate->list.compiling) {
	// Free the previous list if it exist...
        free_renderlist(glstate->lists[list - 1]);
        glstate->lists[list - 1] = GetFirst(glstate->list.active);
        glstate->list.compiling = false;
        end_renderlist(glstate->list.active);
        glstate->list.active = NULL;
        if (gl_batch==1) {
            init_batch();
        } 
        if (glstate->list.mode == GL_COMPILE_AND_EXECUTE) {
            glCallList(list);
        }
    }
}
void glEndList() AliasExport("glshim_glEndList");

renderlist_t* append_calllist(renderlist_t *list, renderlist_t *a);
void glshim_glCallList(GLuint list) {
	noerrorShim();
    if ((glstate->list.compiling || glstate->gl_batch) && glstate->list.active) {
        glstate->list.active = append_calllist(glstate->list.active, glshim_glGetList(list));
		return;
	}
    // TODO: the output of this call can be compiled into another display list
    renderlist_t *l = glshim_glGetList(list);
    if (l)
        draw_renderlist(l);
}
void glCallList(GLuint list) AliasExport("glshim_glCallList");

void glPushCall(void *call) {
    if ((glstate->list.compiling || glstate->gl_batch) && glstate->list.active) {
		NewStage(glstate->list.active, STAGE_GLCALL);
        rlPushCall(glstate->list.active, call);
    }
}

void glshim_glCallLists(GLsizei n, GLenum type, const GLvoid *lists) {
    #define call(name, type) \
        case name: glCallList(((type *)lists)[i] + glstate->list.base); break

    // seriously wtf
    #define call_bytes(name, stride)                             \
        case name:                                               \
            l = (GLubyte *)lists;                                \
            list = 0;                                            \
            for (j = 0; j < stride; j++) {                       \
                list += *(l + (i * stride + j)) << (stride - j); \
            }                                                    \
            glshim_glCallList(list + glstate->list.base);                  \
            break

    unsigned int i, j;
    GLuint list;
    GLubyte *l;
    for (i = 0; i < n; i++) {
        switch (type) {
            call(GL_BYTE, GLbyte);
            call(GL_UNSIGNED_BYTE, GLubyte);
            call(GL_SHORT, GLshort);
            call(GL_UNSIGNED_SHORT, GLushort);
            call(GL_INT, GLint);
            call(GL_UNSIGNED_INT, GLuint);
            call(GL_FLOAT, GLfloat);
            call_bytes(GL_2_BYTES, 2);
            call_bytes(GL_3_BYTES, 3);
            call_bytes(GL_4_BYTES, 4);
        }
    }
    #undef call
    #undef call_bytes
}
void glCallLists(GLsizei n, GLenum type, const GLvoid *lists) AliasExport("glshim_glCallLists");

void glshim_glDeleteList(GLuint list) {
    if(glstate->gl_batch) {
        flush();
    }
    renderlist_t *l = glshim_glGetList(list);
    if (l) {
        free_renderlist(l);
        glstate->lists[list-1] = NULL;
    }

    // lists just grow upwards, maybe use a better storage mechanism?
}

void glshim_glDeleteLists(GLuint list, GLsizei range) {
	noerrorShim();
    for (int i = 0; i < range; i++) {
        glshim_glDeleteList(list+i);
    }
}
void glDeleteLists(GLuint list, GLsizei range) AliasExport("glshim_glDeleteLists");

void glshim_glListBase(GLuint base) {
	noerrorShim();
    glstate->list.base = base;
}
void glListBase(GLuint base) AliasExport("glshim_glListBase");

GLboolean glshim_glIsList(GLuint list) {
	noerrorShim();
    if (list - 1 < glstate->list.count) {
        return true;
    }
    return false;
}
GLboolean glIsList(GLuint list) AliasExport("glshim_glIsList");

void glshim_glPolygonMode(GLenum face, GLenum mode) {
	noerrorShim();
	if (face != GL_FRONT_AND_BACK)
		errorShim(GL_INVALID_ENUM);
	if (face == GL_BACK)
		return;		//TODO, handle face enum for polygon mode != GL_FILL
	if ((glstate->list.compiling || glstate->gl_batch) && (glstate->list.active)) {
		NewStage(glstate->list.active, STAGE_POLYGON);
		glstate->list.active->polygon_mode = mode;
		return;
	}
	switch(mode) {
		case GL_LINE:
		case GL_POINT:
			glstate->polygon_mode = mode;
			break;
		case GL_FILL:
			glstate->polygon_mode = 0;
			break;
		default:
			glstate->polygon_mode = 0;
	}
}
void glPolygonMode(GLenum face, GLenum mode) AliasExport("glshim_glPolygonMode");

void alloc_matrix(matrixstack_t **matrixstack, int depth) {
	*matrixstack = (matrixstack_t*)malloc(sizeof(matrixstack_t));
	(*matrixstack)->top = 0;
	(*matrixstack)->stack = (GLfloat*)malloc(sizeof(GLfloat)*depth*16);
}

void glshim_glPushMatrix() {
	PUSH_IF_COMPILING(glPushMatrix);
	LOAD_GLES(glPushMatrix);
	// Alloc matrix stacks if needed
	if (!glstate->projection_matrix)
		alloc_matrix(&glstate->projection_matrix, MAX_STACK_PROJECTION);
	if (!glstate->modelview_matrix)
		alloc_matrix(&glstate->modelview_matrix, MAX_STACK_MODELVIEW);
	if (!glstate->texture_matrix) {
		glstate->texture_matrix = (matrixstack_t**)malloc(sizeof(matrixstack_t*)*MAX_TEX);
		for (int i=0; i<MAX_TEX; i++)
			alloc_matrix(&glstate->texture_matrix[i], MAX_STACK_TEXTURE);
	}
	// get matrix mode
	GLint matrix_mode;
	glshim_glGetIntegerv(GL_MATRIX_MODE, &matrix_mode);
	noerrorShim();
	// go...
	switch(matrix_mode) {
		case GL_PROJECTION:
			if (glstate->projection_matrix->top<MAX_STACK_PROJECTION) {
				glshim_glGetFloatv(GL_PROJECTION_MATRIX, glstate->projection_matrix->stack+16*glstate->projection_matrix->top++);
			} else
				errorShim(GL_STACK_OVERFLOW);
			break;
		case GL_MODELVIEW:
			if (glstate->modelview_matrix->top<MAX_STACK_MODELVIEW) {
				glshim_glGetFloatv(GL_MODELVIEW_MATRIX, glstate->modelview_matrix->stack+16*glstate->modelview_matrix->top++);
			} else
				errorShim(GL_STACK_OVERFLOW);
			break;
		case GL_TEXTURE:
			if (glstate->texture_matrix[glstate->texture.active]->top<MAX_STACK_PROJECTION) {
				glshim_glGetFloatv(GL_TEXTURE_MATRIX, glstate->texture_matrix[glstate->texture.active]->stack+16*glstate->texture_matrix[glstate->texture.active]->top++);
			} else
				errorShim(GL_STACK_OVERFLOW);
			break;
			
		default:
			//Warning?
			errorShim(GL_INVALID_OPERATION);
			//LOGE("LIBGL: PushMatrix with Unrecognise matrix mode (0x%04X)\n", matrix_mode);
			//gles_glPushMatrix();
	}
}
void glPushMatrix() AliasExport("glshim_glPushMatrix");

void glshim_glPopMatrix() {
	PUSH_IF_COMPILING(glPopMatrix);
	LOAD_GLES(glPopMatrix);
	// Alloc matrix stacks if needed
	if (!glstate->projection_matrix)
		alloc_matrix(&glstate->projection_matrix, MAX_STACK_PROJECTION);
	if (!glstate->modelview_matrix)
		alloc_matrix(&glstate->modelview_matrix, MAX_STACK_MODELVIEW);
	if (!glstate->texture_matrix) {
		glstate->texture_matrix = (matrixstack_t**)malloc(sizeof(matrixstack_t*)*MAX_TEX);
		for (int i=0; i<MAX_TEX; i++)
			alloc_matrix(&glstate->texture_matrix[i], MAX_STACK_TEXTURE);
	}
	// get matrix mode
	GLint matrix_mode;
	glshim_glGetIntegerv(GL_MATRIX_MODE, &matrix_mode);
	// go...
	noerrorShim();
	switch(matrix_mode) {
		case GL_PROJECTION:
			if (glstate->projection_matrix->top) {
				glshim_glLoadMatrixf(glstate->projection_matrix->stack+16*--glstate->projection_matrix->top);
			} else
				errorShim(GL_STACK_UNDERFLOW);
			break;
		case GL_MODELVIEW:
			if (glstate->modelview_matrix->top) {
				glshim_glLoadMatrixf(glstate->modelview_matrix->stack+16*--glstate->modelview_matrix->top);
			} else
				errorShim(GL_STACK_UNDERFLOW);
			break;
		case GL_TEXTURE:
			if (glstate->texture_matrix[glstate->texture.active]->top) {
				glshim_glLoadMatrixf(glstate->texture_matrix[glstate->texture.active]->stack+16*--glstate->texture_matrix[glstate->texture.active]->top);
			} else
				errorShim(GL_STACK_UNDERFLOW);
			break;
			
		default:
			//Warning?
			errorShim(GL_INVALID_OPERATION);
			//LOGE("LIBGL: PopMatrix with Unrecognise matrix mode (0x%04X)\n", matrix_mode);
			//gles_glPopMatrix();
	}
}
void glPopMatrix() AliasExport("glshim_glPopMatrix");

GLenum glshim_glGetError() {
	LOAD_GLES(glGetError);
    if(glshim_noerror)
        return GL_NO_ERROR;
	if (glstate->shim_error) {
		GLenum tmp = glstate->last_error;
		glstate->last_error = GL_NO_ERROR;
		return tmp;
	}
	return gles_glGetError();
}
GLenum glGetError() AliasExport("glshim_glGetError");

void glshim_glBlendColor(GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha) {
    PUSH_IF_COMPILING(glBlendColor);
    LOAD_GLES_OES(glBlendColor);
	if  (gles_glBlendColor)
		gles_glBlendColor(red, green, blue, alpha);
	else {
        static int test = 1;
        if (test) {
            LOGD("stub glBlendColor(%f, %f, %f, %f)\n", red, green, blue, alpha);
            test = 0;
        }
    }
}
void glBlendColor(GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha) AliasExport("glshim_glBlendColor");
void glBlendColorEXT(GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha) AliasExport("glshim_glBlendColor");
void glBlendColorARB(GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha) AliasExport("glshim_glBlendColor");

void glshim_glBlendFuncSeparate(GLenum sfactorRGB, GLenum dfactorRGB, GLenum sfactorAlpha, GLenum dfactorAlpha)
{
    PUSH_IF_COMPILING(glBlendFuncSeparate);
    LOAD_GLES_OES(glBlendFuncSeparate);
#ifdef ODROID
    if(gles_glBlendFuncSeparate)
#endif
    gles_glBlendFuncSeparate(sfactorRGB, dfactorRGB, sfactorAlpha, dfactorAlpha);
}
void glBlendFuncSeparate(GLenum sfactorRGB, GLenum dfactorRGB, GLenum sfactorAlpha, GLenum dfactorAlpha) AliasExport("glshim_glBlendFuncSeparate");
void glBlendFuncSeparateEXT (GLenum sfactorRGB, GLenum dfactorRGB, GLenum sfactorAlpha, GLenum dfactorAlpha) AliasExport("glshim_glBlendFuncSeparate");

void glshim_glBlendEquationSeparate(GLenum modeRGB, GLenum modeA) {
    PUSH_IF_COMPILING(glBlendEquationSeparate);
    LOAD_GLES_OES(glBlendEquationSeparate);
#ifdef ODROID
    if(gles_glBlendEquationSeparate)
#endif
    gles_glBlendEquationSeparate(modeRGB, modeA);
}
void glBlendEquationSeparate(GLenum modeRGB, GLenum modeA) AliasExport("glshim_glBlendEquationSeparate");
void glBlendEquationSeparateEXT(GLenum modeRGB, GLenum modeA) AliasExport("glshim_glBlendEquationSeparate");

void glshim_glBlendFunc(GLenum sfactor, GLenum dfactor) {
    if (glstate->list.active && (glstate->gl_batch && !glstate->list.compiling))  {
        if ((glstate->statebatch.blendfunc_s == sfactor) && (glstate->statebatch.blendfunc_d == dfactor))
            return; // nothing to do...
        if (!glstate->statebatch.blendfunc_s) {
            glstate->statebatch.blendfunc_s = sfactor;
            glstate->statebatch.blendfunc_d = dfactor;
        } else {
            flush();
        }
    }
    PUSH_IF_COMPILING(glBlendFunc);
    LOAD_GLES(glBlendFunc);
    LOAD_GLES_OES(glBlendFuncSeparate);
    errorGL();
    // There are some limitations in GLES1.1 Blend functions
    switch(sfactor) {
        case GL_SRC_COLOR:
            if (gles_glBlendFuncSeparate) {
                gles_glBlendFuncSeparate(sfactor, dfactor, sfactor, dfactor);
                return;
            }
            sfactor = GL_ONE;   // approx...
            break;
        case GL_ONE_MINUS_SRC_COLOR:
            if (gles_glBlendFuncSeparate) {
                gles_glBlendFuncSeparate(sfactor, dfactor, sfactor, dfactor);
                return;
            }
            sfactor = GL_ONE;  // not sure it make sense...
            break;
        // here, we need support for glBlendColor...
        case GL_CONSTANT_COLOR:
        case GL_CONSTANT_ALPHA:
            sfactor = GL_ONE;
            break;
        case GL_ONE_MINUS_CONSTANT_COLOR:
        case GL_ONE_MINUS_CONSTANT_ALPHA:
            sfactor = GL_ZERO;
            break;
        default:
            break;
    }
    
    switch(dfactor) {
        case GL_DST_COLOR:
            sfactor = GL_ONE;   // approx...
            break;
        case GL_ONE_MINUS_DST_COLOR:
            sfactor = GL_ZERO;  // not sure it make sense...
            break;
        // here, we need support for glBlendColor...
        case GL_CONSTANT_COLOR:
        case GL_CONSTANT_ALPHA:
            sfactor = GL_ONE;
            break;
        case GL_ONE_MINUS_CONSTANT_COLOR:
        case GL_ONE_MINUS_CONSTANT_ALPHA:
            sfactor = GL_ZERO;
            break;
        default:
            break;
    }
    
    if ((blendhack) && (sfactor==GL_SRC_ALPHA) && (dfactor==GL_ONE)) {
        // special case, as seen in Xash3D, but it breaks torus_trooper, so behind a parameter
        sfactor = GL_ONE;
    }

#ifdef ODROID
    if(gles_glBlendFunc)
#endif
    gles_glBlendFunc(sfactor, dfactor);
}
void glBlendFunc(GLenum sfactor, GLenum dfactor) AliasExport("glshim_glBlendFunc");


void glshim_glStencilMaskSeparate(GLenum face, GLuint mask) {
    // fake function..., call it only for front or front_and_back, just ignore back (crappy, I know)
    if ((face==GL_FRONT) || (face==GL_FRONT_AND_BACK))
        glshim_glStencilMask(mask);
}
void glStencilMaskSeparate(GLenum face, GLuint mask) AliasExport("glshim_glStencilMaskSeparate");


void init_statebatch() {
    memset(&glstate->statebatch, 0, sizeof(statebatch_t));
}

void flush() {
    // flush internal list
    //LOGD("flush glstate->list.active=%p, gl_batch=%i(%i)\n", glstate->list.active, glstate->gl_batch, gl_batch);
    renderlist_t *mylist = glstate->list.active;
    if (mylist) {
        GLuint old = glstate->gl_batch;
        glstate->list.active = NULL;
        glstate->gl_batch = 0;
        mylist = end_renderlist(mylist);
        draw_renderlist(mylist);
        free_renderlist(mylist);
        glstate->gl_batch = old;
    }
    if (glstate->gl_batch) init_statebatch();
    glstate->list.active = (glstate->gl_batch)?alloc_renderlist():NULL;
}

void init_batch() {
    glstate->list.active = alloc_renderlist();
    init_statebatch();
    glstate->gl_batch = 1;
    glstate->init_batch = 1;
}
#ifndef ANDROID
extern void BlitEmulatedPixmap();
#endif
void glshim_glFlush() {
	LOAD_GLES(glFlush);
    
    if (glstate->list.active && !glstate->gl_batch) {
        errorShim(GL_INVALID_OPERATION);
        return;
    }
    
    if (glstate->gl_batch) flush();
    
    gles_glFlush();
    errorGL();

#ifndef ANDROID
    if(glstate->emulatedPixmap && !glstate->emulatedWin)
        BlitEmulatedPixmap();
#endif
}
void glFlush() AliasExport("glshim_glFlush");

void glshim_glFinish() {
	LOAD_GLES(glFinish);
    
    if (glstate->list.active && !glstate->gl_batch) {
        errorShim(GL_INVALID_OPERATION);
        return;
    }
    if (glstate->gl_batch) flush();
    
    gles_glFinish();
    errorGL();
}
void glFinish() AliasExport("glshim_glFinish");

void glshim_glLoadMatrixf(const GLfloat * m) {
    LOAD_GLES(glLoadMatrixf);
    
    if ((glstate->list.compiling || glstate->gl_batch) && glstate->list.active) {
        NewStage(glstate->list.active, STAGE_MATRIX);
        glstate->list.active->matrix_op = 1;
        memcpy(glstate->list.active->matrix_val, m, 16*sizeof(GLfloat));
        return;
    }
    gles_glLoadMatrixf(m);
}
void glLoadMatrixf(const GLfloat * m) AliasExport("glshim_glLoadMatrixf");

void glshim_glMultMatrixf(const GLfloat * m) {
    LOAD_GLES(glMultMatrixf);
    
    if ((glstate->list.compiling || glstate->gl_batch) && glstate->list.active) {
        NewStage(glstate->list.active, STAGE_MATRIX);
        glstate->list.active->matrix_op = 2;
        memcpy(glstate->list.active->matrix_val, m, 16*sizeof(GLfloat));
        return;
    }
    gles_glMultMatrixf(m);
}
void glMultMatrixf(const GLfloat * m) AliasExport("glshim_glMultMatrixf");

void glshim_glFogfv(GLenum pname, const GLfloat* params) {
    LOAD_GLES(glFogfv);

    if ((glstate->list.compiling || glstate->gl_batch) && glstate->list.active) {
        if (pname == GL_FOG_COLOR) {
            NewStage(glstate->list.active, STAGE_FOG);
            rlFogOp(glstate->list.active, 1, params);
            return;
        }
    }
    PUSH_IF_COMPILING(glFogfv);
    
    gles_glFogfv(pname, params);
}
void glFogfv(GLenum pname, const GLfloat* params) AliasExport("glshim_glFogfv");

void glshim_glIndexPointer(GLenum type, GLsizei stride, const GLvoid * pointer) {
    static bool warning = false;
    if(!warning) {
        LOGD("Warning, stubbed glIndexPointer\n");
        warning = true;
    }
}
void glIndexPointer(GLenum type, GLsizei stride, const GLvoid * pointer) AliasExport("glshim_glIndexPointer");

void glshim_glEdgeFlagPointer(GLsizei stride, const GLvoid * pointer) {
    static bool warning = false;
    if(!warning) {
        LOGD("Warning, stubbed glEdgeFlagPointer\n");
        warning = true;
    }
}
void glEdgeFlagPointer(GLsizei stride, const GLvoid * pointer) AliasExport("glshim_glEdgeFlagPointer");

void glshim_glGetPointerv(GLenum pname, GLvoid* *params) {
    noerrorShim();
    if (glstate->list.active && (glstate->gl_batch && !glstate->list.compiling)) flush();
    switch(pname) {
        case GL_COLOR_ARRAY_POINTER:
            *params = (void*)glstate->vao->pointers.color.pointer;
            break;
        case GL_EDGE_FLAG_ARRAY_POINTER:
            *params = NULL;
            break;
        case GL_FEEDBACK_BUFFER_POINTER:
            *params = NULL;
            break;
        case GL_INDEX_ARRAY_POINTER:
            *params = NULL;
        case GL_NORMAL_ARRAY_POINTER:
            *params = (void*)glstate->vao->pointers.normal.pointer;
            break;
        case GL_TEXTURE_COORD_ARRAY_POINTER:
            *params = (void*)glstate->vao->pointers.tex_coord[glstate->texture.client].pointer;
            break;
        case GL_SELECTION_BUFFER_POINTER:
            *params = glstate->selectbuf.buffer;
            break;
        case GL_VERTEX_ARRAY_POINTER :
            *params = (void*)glstate->vao->pointers.vertex.pointer;
            break;
        default:
            errorShim(GL_INVALID_ENUM);
    }
}
void glGetPointerv(GLenum pname, GLvoid* *params) AliasExport("glshim_glGetPointerv");

void glshim_glPointParameteri(GLenum pname, GLint param)
{
    glshim_glPointParameterf(pname, param);
}
void glPointParameteri(GLenum pname, GLint param) AliasExport("glshim_glPointParameteri");

void glshim_glPointParameteriv(GLenum pname, const GLint * params)
{
    GLfloat tmp[3];
    int v=(pname==GL_POINT_DISTANCE_ATTENUATION)?3:1;
    for (int i=0; i<v; i++) tmp[i] = params[i];
    glshim_glPointParameterfv(pname, tmp);
}
void glPointParameteriv(GLenum pname, const GLint * params) AliasExport("glshim_glPointParameteriv");



void glshim_glMultiDrawArrays(GLenum mode, const GLint *first, const GLsizei *count, GLsizei primcount)
{
    LOAD_GLES_EXT(glMultiDrawArrays);
    if((!gles_glMultiDrawArrays) || should_intercept_render(mode) || (glstate->list.active && (glstate->list.compiling || glstate->gl_batch)) 
        || (glstate->render_mode == GL_SELECT) || ((glstate->polygon_mode == GL_LINE) || (glstate->polygon_mode == GL_POINT)) )
    {
        // divide the call
        // TODO optimize with forcing Batch mode
        for (int i=0; i<primcount; i++)
            glshim_glDrawArrays(mode, first[i], count[i]);
    }
    else
    {
        if(mode==GL_QUAD_STRIP) mode=GL_TRIANGLE_STRIP;
        else if(mode==GL_POLYGON) mode=GL_TRIANGLE_FAN;
        gles_glMultiDrawArrays(mode, first, count, primcount);
        errorGL();
    }
}
void glMultiDrawArrays(GLenum mode, const GLint *first, const GLsizei *count, GLsizei primcount) AliasExport("glshim_glMultiDrawArrays");

void glshim_glMultiDrawElements( GLenum mode, GLsizei *count, GLenum type, const void * const *indices, GLsizei primcount)
{
    LOAD_GLES_EXT(glMultiDrawElements);
    if((!gles_glMultiDrawElements) || should_intercept_render(mode) || (glstate->list.active && (glstate->list.compiling || glstate->gl_batch)) 
        || (glstate->render_mode == GL_SELECT) || ((glstate->polygon_mode == GL_LINE) || (glstate->polygon_mode == GL_POINT)) || (type != GL_UNSIGNED_SHORT) )
    {
        // divide the call
        // TODO optimize with forcing Batch mode
        for (int i=0; i<primcount; i++)
            glshim_glDrawElements(mode, count[i], type, indices[i]);
    }
    else
    {
        if(mode==GL_QUAD_STRIP) mode=GL_TRIANGLE_STRIP;
        else if(mode==GL_POLYGON) mode=GL_TRIANGLE_FAN;
        gles_glMultiDrawElements(mode, count, type, indices, primcount);
        errorGL();
    }
}
void glMultiDrawElements( GLenum mode, GLsizei *count, GLenum type, const void * const *indices, GLsizei primcount) AliasExport("glshim_glMultiDrawElements");
