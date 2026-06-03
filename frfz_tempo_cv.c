#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include <lv2/lv2plug.in/ns/lv2core/lv2.h>
#include <lv2/lv2plug.in/ns/ext/atom/atom.h>
#include <lv2/lv2plug.in/ns/ext/atom/forge.h>
#include <lv2/lv2plug.in/ns/ext/log/logger.h>
#include <lv2/lv2plug.in/ns/ext/midi/midi.h>
#include <lv2/lv2plug.in/ns/ext/time/time.h>
#include <lv2/lv2plug.in/ns/ext/urid/urid.h>

#ifndef DEBUG
#define DEBUG 0
#endif
#define debug_print(...) \
((void)((DEBUG) ? fprintf(stderr, __VA_ARGS__) : 0))

#ifndef M_PI
#    define M_PI 3.14159265
#endif

#define PLUGIN_URI "http://github.com/fragfz/frfz_tempo_cv"

/* Port indices - matching mod-cv-clock structure */
typedef enum {
    FREQ_OUT1       = 0,
    FREQ_OUT2       = 1,
    TIME_OUT1       = 2,
    TIME_OUT2       = 3,
    BPM_PORT        = 4,
    DIVISIONS_PORT  = 5,
    MIN_FREQ_PORT   = 6,
    MAX_FREQ_PORT   = 7,
    MIN_TIME_PORT   = 8,
    MAX_TIME_PORT   = 9,
    SCALING_MODE    = 10,
    SYNC_PORT       = 11,
    CONTROL_PORT    = 12
} PortIndex;

/* URIDs for time extension */
typedef struct {
    LV2_URID atom_Blank;
    LV2_URID atom_Float;
    LV2_URID atom_Object;
    LV2_URID atom_Sequence;
    LV2_URID time_Position;
    LV2_URID time_barBeat;
    LV2_URID time_beatsPerMinute;
    LV2_URID time_speed;
} TempoURIs;

/* Plugin state structure */
typedef struct {
    LV2_URID_Map*  map;
    LV2_Log_Log*   log;
    LV2_Log_Logger logger;
    TempoURIs      uris;

    /* Port pointers */
    float* freq_out1;
    float* freq_out2;
    float* time_out1;
    float* time_out2;
    float* bpm_param;          /* Manual BPM parameter */
    float* division_param;     /* Time division (0.05-16, matching mod-cv-clock) */
    float* min_freq;
    float* max_freq;
    float* min_time;
    float* max_time;
    float* scaling_mode;       /* Linear (0) or Logarithmic (1) for Hz */
    float* sync;               /* Host sync enable/disable */
    LV2_Atom_Sequence* control;

    /* State */
    double samplerate;
    float bpm;                 /* Current BPM (from host or parameter) */
    int prev_sync;             /* Track sync state changes */
} TempoCV;

static void
connect_port(LV2_Handle instance,
             uint32_t   port,
             void*      data)
{
    TempoCV* self = (TempoCV*)instance;

    switch ((PortIndex)port) {
        case FREQ_OUT1:
            self->freq_out1 = (float*)data;
            break;
        case FREQ_OUT2:
            self->freq_out2 = (float*)data;
            break;
        case TIME_OUT1:
            self->time_out1 = (float*)data;
            break;
        case TIME_OUT2:
            self->time_out2 = (float*)data;
            break;
        case BPM_PORT:
            self->bpm_param = (float*)data;
            break;
        case DIVISIONS_PORT:
            self->division_param = (float*)data;
            break;
        case MIN_FREQ_PORT:
            self->min_freq = (float*)data;
            break;
        case MAX_FREQ_PORT:
            self->max_freq = (float*)data;
            break;
        case MIN_TIME_PORT:
            self->min_time = (float*)data;
            break;
        case MAX_TIME_PORT:
            self->max_time = (float*)data;
            break;
        case SCALING_MODE:
            self->scaling_mode = (float*)data;
            break;
        case SYNC_PORT:
            self->sync = (float*)data;
            break;
        case CONTROL_PORT:
            self->control = (LV2_Atom_Sequence*)data;
            break;
    }
}

static void
activate(LV2_Handle instance)
{
    TempoCV* self = (TempoCV*)instance;
    self->bpm = 120.0f;
    self->prev_sync = 0;
}

static LV2_Handle
instantiate(const LV2_Descriptor*     descriptor,
            double                    rate,
            const char*               bundle_path,
            const LV2_Feature* const* features)
{
    TempoCV* self = (TempoCV*)calloc(1, sizeof(TempoCV));
    if (!self) {
        return NULL;
    }

    /* Query feature pointers */
    for (uint32_t i = 0; features[i]; ++i) {
        if (!strcmp(features[i]->URI, LV2_URID__map)) {
            self->map = (LV2_URID_Map*)features[i]->data;
        } else if (!strcmp(features[i]->URI, LV2_LOG__log)) {
            self->log = (LV2_Log_Log*)features[i]->data;
        }
    }

    lv2_log_logger_init(&self->logger, self->map, self->log);

    if (!self->map) {
        lv2_log_error(&self->logger, "frfz_tempo_cv error: Host does not support urid:map\n");
        free(self);
        return NULL;
    }

    /* Map URIS - identical to mod-cv-clock */
    TempoURIs* const    uris = &self->uris;
    LV2_URID_Map* const map  = self->map;
    uris->atom_Blank         = map->map(map->handle, LV2_ATOM__Blank);
    uris->atom_Float         = map->map(map->handle, LV2_ATOM__Float);
    uris->atom_Object        = map->map(map->handle, LV2_ATOM__Object);
    uris->atom_Sequence      = map->map(map->handle, LV2_ATOM__Sequence);
    uris->time_Position      = map->map(map->handle, LV2_TIME__Position);
    uris->time_barBeat       = map->map(map->handle, LV2_TIME__barBeat);
    uris->time_beatsPerMinute = map->map(map->handle, LV2_TIME__beatsPerMinute);
    uris->time_speed         = map->map(map->handle, LV2_TIME__speed);

    self->samplerate = rate;

    return (LV2_Handle)self;
}

/* Extract BPM from host transport info - identical to mod-cv-clock */
static void
update_position(TempoCV* self, const LV2_Atom_Object* obj)
{
    const TempoURIs* uris = &self->uris;

    LV2_Atom *bpm = NULL;
    lv2_atom_object_get(obj,
            uris->time_beatsPerMinute, &bpm,
            NULL);

    if (bpm && bpm->type == uris->atom_Float) {
        self->bpm = ((LV2_Atom_Float*)bpm)->body;
    }
}

/* Clamp float value */
static float
clampf(float value, float min_val, float max_val)
{
    if (value < min_val) return min_val;
    if (value > max_val) return max_val;
    return value;
}

/* Apply logarithmic scaling to frequency within min/max range */
static float
scale_frequency_log(float freq_hz, float min_freq, float max_freq)
{
    /* Clamp to range first */
    if (freq_hz < min_freq) freq_hz = min_freq;
    if (freq_hz > max_freq) freq_hz = max_freq;

    /* Logarithmic scaling: log(freq) maps linearly to log range */
    float log_min = logf(min_freq);
    float log_max = logf(max_freq);
    float log_freq = logf(freq_hz);

    /* Output normalized to CV range 0-10V (scaled by frequency ratio in log space) */
    if (log_max - log_min > 0.001f) {
        return (log_freq - log_min) / (log_max - log_min) * 10.0f;
    }
    return 5.0f;
}

/* Apply linear scaling to frequency within min/max range */
static float
scale_frequency_linear(float freq_hz, float min_freq, float max_freq)
{
    /* Clamp to range */
    if (freq_hz < min_freq) freq_hz = min_freq;
    if (freq_hz > max_freq) freq_hz = max_freq;

    /* Linear scaling */
    if (max_freq - min_freq > 0.001f) {
        return (freq_hz - min_freq) / (max_freq - min_freq) * 10.0f;
    }
    return 5.0f;
}

static void
run(LV2_Handle instance, uint32_t n_samples)
{
    TempoCV* self = (TempoCV*)instance;

    const TempoURIs* uris = &self->uris;
    const LV2_Atom_Sequence* in = self->control;

    /* Process incoming atoms (time position from host) */
    for (const LV2_Atom_Event* ev = lv2_atom_sequence_begin(&in->body);
            !lv2_atom_sequence_is_end(&in->body, in->atom.size, ev);
            ev = lv2_atom_sequence_next(ev)) {

        if (ev->body.type == uris->atom_Object ||
                ev->body.type == uris->atom_Blank) {
            const LV2_Atom_Object* obj = (const LV2_Atom_Object*)&ev->body;
            if (obj->body.otype == uris->time_Position) {
                update_position(self, obj);
            }
        }
    }

    /* Determine BPM source: sync enabled uses host BPM, otherwise parameter */
    float bpm = self->bpm;
    if ((int)*self->sync == 0) {
        /* Use manual BPM parameter when sync disabled */
        bpm = *self->bpm_param;
    }
    /* Ensure valid BPM range */
    bpm = clampf(bpm, 20.0f, 280.0f);

    /* Get division value (0.05 to 16, matching mod-cv-clock) */
    float division = *self->division_param;
    division = clampf(division, 0.05f, 16.0f);

    /* Get min/max parameters */
    float min_freq = *self->min_freq;
    float max_freq = *self->max_freq;
    float min_time = *self->min_time;
    float max_time = *self->max_time;
    int mode = (int)(*self->scaling_mode + 0.5f);

    /* Ensure min <= max */
    if (min_freq > max_freq) {
        float tmp = min_freq;
        min_freq = max_freq;
        max_freq = tmp;
    }
    if (min_time > max_time) {
        float tmp = min_time;
        min_time = max_time;
        max_time = tmp;
    }

    /* 
     * Tempo calculation using mod-ui formula:
     * value_in_seconds = 240 / (BPM * divider)
     */
    float period_sec = 240.0f / (bpm * division);

    /* Calculate frequency (Hz) = 1 / period */
    float freq_hz = 1.0f / period_sec;
    
    /* Clamp frequency to safe range */
    freq_hz = clampf(freq_hz, 0.1f, 20.0f);

    /* Calculate time in milliseconds */
    float time_ms = period_sec * 1000.0f;

    /* Scale frequency outputs based on mode */
    float freq_cv1, freq_cv2;
    if (mode == 0) {
        /* Linear scaling */
        freq_cv1 = scale_frequency_linear(freq_hz, min_freq, max_freq);
        freq_cv2 = scale_frequency_linear(freq_hz * 2.0f, min_freq, max_freq);
    } else {
        /* Logarithmic scaling */
        freq_cv1 = scale_frequency_log(freq_hz, min_freq, max_freq);
        freq_cv2 = scale_frequency_log(freq_hz * 2.0f, min_freq, max_freq);
    }

    /* Clamp time outputs to user-specified ranges */
    float time_cv1 = clampf(time_ms, min_time, max_time);
    float time_cv2 = clampf(time_ms * 2.0f, min_time, max_time);

    /* Write outputs for all samples */
    for (uint32_t i = 0; i < n_samples; ++i) {
        self->freq_out1[i] = freq_cv1;
        self->freq_out2[i] = freq_cv2;
        self->time_out1[i] = time_cv1;
        self->time_out2[i] = time_cv2;
    }
}

static void
deactivate(LV2_Handle instance)
{
    /* Nothing to do */
}

static void
cleanup(LV2_Handle instance)
{
    free(instance);
}

static const void*
extension_data(const char* uri)
{
    return NULL;
}

static const LV2_Descriptor descriptor = {
    PLUGIN_URI,
    instantiate,
    connect_port,
    activate,
    run,
    deactivate,
    cleanup,
    extension_data
};

LV2_SYMBOL_EXPORT
const LV2_Descriptor*
lv2_descriptor(uint32_t index)
{
    switch (index) {
        case 0:
            return &descriptor;
        default:
            return NULL;
    }
}
