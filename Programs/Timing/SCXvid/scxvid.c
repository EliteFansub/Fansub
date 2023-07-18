#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <xvid.h>
#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#endif

#define SCXVID_BUFFER_SIZE (1024*1024*4)

typedef struct {
    unsigned int width;
    unsigned int height;

    size_t frame_size;
    size_t y_plane_size;
    size_t uv_plane_size;
} videoinfo_t;

typedef struct {
    char *logname;

    videoinfo_t vi;

    void *xvid_handle;
    xvid_enc_frame_t xvid_enc_frame;
    xvid_enc_create_t xvid_enc_create;

    void *output_buffer;
    void *input_buffer;
} scxvid_state_t;


static int scxvid_create(scxvid_state_t *state)
{
    int error = 0;
    xvid_gbl_init_t xvid_init;
    memset(&xvid_init, 0, sizeof(xvid_gbl_init_t));
    xvid_init.version = XVID_VERSION;
    xvid_init.debug = ~0;
    error = xvid_global(NULL, XVID_GBL_INIT, &xvid_init, NULL);
    if (error)
    {
        fprintf(stderr, "SCXvid: Failed to initialize Xvid\n");
        return error;
    }

    xvid_gbl_info_t xvid_info;
    memset(&xvid_info, 0, sizeof(xvid_gbl_info_t));
    xvid_info.version = XVID_VERSION;
    error = xvid_global(NULL, XVID_GBL_INFO, &xvid_info, NULL);
    if (error)
    {
        fprintf(stderr, "SCXvid: Failed to initialize Xvid\n");
        return error;
    }

    memset(&state->xvid_enc_create, 0, sizeof(xvid_enc_create_t));
    state->xvid_enc_create.version = XVID_VERSION;
    state->xvid_enc_create.profile = 0;
    state->xvid_enc_create.width = state->vi.width;
    state->xvid_enc_create.height = state->vi.height;
    state->xvid_enc_create.num_threads = xvid_info.num_threads;
    state->xvid_enc_create.num_slices = xvid_info.num_threads;
    state->xvid_enc_create.fincr = 1;
    state->xvid_enc_create.fbase = 1;
    state->xvid_enc_create.max_key_interval = 10000000; //huge number
    xvid_enc_plugin_t plugins[1];
    xvid_plugin_2pass1_t xvid_rc_plugin;
    memset(&xvid_rc_plugin, 0, sizeof(xvid_plugin_2pass1_t));
    xvid_rc_plugin.version = XVID_VERSION;
    xvid_rc_plugin.filename = state->logname;
    plugins[0].func = xvid_plugin_2pass1;
    plugins[0].param = &xvid_rc_plugin;
    state->xvid_enc_create.plugins = plugins;
    state->xvid_enc_create.num_plugins = 1;

    error = xvid_encore(NULL, XVID_ENC_CREATE, &state->xvid_enc_create, NULL);
    if (error)
    {
        fprintf(stderr,"SCXvid: Failed to initialize Xvid encoder\n");
        return error;
    }
    state->xvid_handle = state->xvid_enc_create.handle;

    //default identical(?) to xvid 1.1.2 vfw general preset
    memset(&state->xvid_enc_frame, 0, sizeof(xvid_enc_frame_t));
    state->xvid_enc_frame.version = XVID_VERSION;
    state->xvid_enc_frame.vol_flags = 0;
    state->xvid_enc_frame.vop_flags = XVID_VOP_MODEDECISION_RD
                                    | XVID_VOP_HALFPEL
                                    | XVID_VOP_HQACPRED
                                    | XVID_VOP_TRELLISQUANT
                                    | XVID_VOP_INTER4V;

    state->xvid_enc_frame.motion = XVID_ME_CHROMA_PVOP
                                 | XVID_ME_CHROMA_BVOP
                                 | XVID_ME_HALFPELREFINE16
                                 | XVID_ME_EXTSEARCH16
                                 | XVID_ME_HALFPELREFINE8
                                 | 0
                                 | XVID_ME_USESQUARES16;

    state->xvid_enc_frame.type = XVID_TYPE_AUTO;
    state->xvid_enc_frame.quant = 0;
    state->xvid_enc_frame.input.csp = XVID_CSP_YV12;

    if (!(state->output_buffer = malloc(SCXVID_BUFFER_SIZE)))
    {
        fprintf(stderr,"SCXvid: Failed to allocate buffer\n");
        perror(NULL);
        return XVID_ERR_MEMORY;
    }
    return 0;
}

static int scxvid_process_frame(scxvid_state_t *state,
 void *plane_y, int stride_y, void *plane_u, int stride_u, void *plane_v, int stride_v)
{
    state->xvid_enc_frame.input.plane[0] = plane_y;
    state->xvid_enc_frame.input.stride[0] = stride_y;
    state->xvid_enc_frame.input.plane[1] = plane_u;
    state->xvid_enc_frame.input.stride[1] = stride_u;
    state->xvid_enc_frame.input.plane[2] = plane_v;
    state->xvid_enc_frame.input.stride[2] = stride_v;

    state->xvid_enc_frame.length = SCXVID_BUFFER_SIZE;
    state->xvid_enc_frame.bitstream = state->output_buffer;

    int error = xvid_encore(state->xvid_handle, XVID_ENC_ENCODE, &state->xvid_enc_frame, NULL);
    if (error < 0)
    {
        fprintf(stderr,"SCXvid: xvid_encore returned an error code\n");
        return error;
    }
    return 0;
}

static int read_y4m_header(scxvid_state_t *state, FILE *y4m)
{
    char header[255];
    if (!(fgets(header, 255, y4m)))
    {
        fprintf(stderr,"SCXvid: Failed to read input file\n");
        return XVID_ERR_FAIL;
    }

    if (sscanf(header, "YUV4MPEG2 W%d H%d", &state->vi.width, &state->vi.height) < 2)
    {
        fprintf(stderr, "SCXvid: Failed to parse input file header: probably not a Y4M file\n");
        return XVID_ERR_FAIL;
    }

    state->vi.frame_size = state->vi.width * state->vi.height * 3 / 2;
    state->vi.y_plane_size = state->vi.width * state->vi.height;
    state->vi.uv_plane_size = state->vi.width * state->vi.height / 4;

    if (!(state->input_buffer = malloc(state->vi.frame_size)))
    {
        fprintf(stderr,"SCXvid: Failed to allocate buffer\n");
        perror(NULL);
        return XVID_ERR_MEMORY;
    }

    return 0;
}

static int read_y4m_frame(scxvid_state_t *state, FILE *y4m)
{
    char frame_header[255];
    if (!(fgets(frame_header, 255, y4m)))
    {
        if (feof(y4m))
        {
            return XVID_ERR_END;
        }
        else
        {
            fprintf(stderr, "SCXvid: Failed to read input file\n");
            return XVID_ERR_FAIL;
        }
    }

    if (strncmp(frame_header, "FRAME", 5) != 0)
    {
        fprintf(stderr,"SCXvid: Failed to parse frame header: input file corrupted\n");
        return XVID_ERR_FAIL;
    }

    if (!(fread(state->input_buffer, state->vi.frame_size, 1, y4m)))
    {
        if (feof(y4m))
        {
            fprintf(stderr, "SCXvid: Unexpected end-of-file\n");
        }
        else
        {
            fprintf(stderr, "SCXvid: Failed to read frame from input\n");
            
        }
        return XVID_ERR_FAIL;
    }

    return 0;
}

static void print_usage()
{
    printf("SCXvid - a standalone port of the AviSynth SCXvid plugin.\n\n"
        "Usage: scxvid {output_log_file} < {input_file}\n\n"
        "Only YUV420P Y4M input is supported.\n\n");
}

int main(int argc, char* argv[])
{

#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
    _setmode(_fileno(stderr), _O_BINARY);
#endif

    scxvid_state_t state;
    memset(&state, 0, sizeof(scxvid_state_t));
    int error = 0;

    if (argc < 2)
    {
        print_usage();
        return 0;
    }

    state.logname = argv[1];

    error = read_y4m_header(&state, stdin);
    if (error)
    {
        return error;
    }

    error = scxvid_create(&state);
    if (error)
    {
        free(state.input_buffer);
        return error;
    }

    while ((error = read_y4m_frame(&state, stdin)) != XVID_ERR_END)
    {
        if (error)
        {
            xvid_encore(state.xvid_handle, XVID_ENC_DESTROY, NULL, NULL);
            free(state.output_buffer);
            free(state.input_buffer);
            return error;
        }

        error = scxvid_process_frame(&state, state.input_buffer, state.vi.width,
            (char *)(state.input_buffer) + state.vi.y_plane_size, state.vi.width / 2,
            (char *)(state.input_buffer) + state.vi.y_plane_size + state.vi.uv_plane_size, state.vi.width / 2);
        if (error)
        {
            xvid_encore(state.xvid_handle, XVID_ENC_DESTROY, NULL, NULL);
            free(state.output_buffer);
            free(state.input_buffer);
            return error;
        }
    }

    xvid_encore(state.xvid_handle, XVID_ENC_DESTROY, NULL, NULL);

    free(state.input_buffer);
    free(state.output_buffer);

    return 0;
}
