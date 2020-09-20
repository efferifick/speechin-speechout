#include <assert.h>
#include <cstdlib>
#include <curses.h>
#include "deepspeech.h"
#include <espeak-ng/speak_lib.h>
#include <errno.h>
#include <portaudio.h>
#include <stdio.h>
#include <string>
#include <unistd.h>

static void eff_initialize (void)  __attribute__((constructor));
static int eff_fprintf(FILE * __restrict__ stream, const char * __restrict__ format, ...);
static int eff_vfprintf(FILE * __restrict__ stream, const char * __restrict__ format, va_list ap); 

static constexpr unsigned sample_rate = 16000;
static constexpr unsigned max_seconds = 5;
static constexpr size_t buffer_len = sample_rate * max_seconds;
static const char* model_path = "deepspeech-0.8.1-models.pbmm";
static const char* scorer_path = "deepspeech-0.8.1-models.scorer";
static const char* language = "English";
static const espeak_AUDIO_OUTPUT output = AUDIO_OUTPUT_SYNCH_PLAYBACK;

struct data_t
{
  size_t index;
  size_t length;
  size_t rec_length;
  int16_t buffer[buffer_len];
};

thread_local static bool is_enabled = false;
thread_local static data_t data = { 0, buffer_len, 0, { 0 }};
thread_local static PaStream *stream = nullptr;
thread_local static WINDOW *w = nullptr;
thread_local static ModelState *ctx = nullptr;

static int
record(const void* in,
       __attribute__((unused)) void *out,
       unsigned long framesPerBuffer,
       __attribute__((unused)) const PaStreamCallbackTimeInfo *timeInfo,
       __attribute__((unused)) PaStreamCallbackFlags flags,
       void *data)
{
  assert(data && in);
  struct data_t *_local = static_cast<struct data_t *>(data);
  const int16_t *_in = static_cast<const int16_t *>(in);

  const bool has_enough_space = _local->index + framesPerBuffer < _local->length;
  // TODO: maybe a circular buffer in the future?
  if (!has_enough_space) return paComplete;

  for (unsigned long i = 0; i < framesPerBuffer; i++)
  {
    _local->buffer[_local->index++] = _in[i];
  }
  return paContinue;
}

static int
play(__attribute__((unused)) const void* in,
       void *out,
       unsigned long framesPerBuffer,
       __attribute__((unused)) const PaStreamCallbackTimeInfo *timeInfo,
       __attribute__((unused)) PaStreamCallbackFlags flags,
       void *data)
{
  assert(data);
  struct data_t *_local = static_cast<struct data_t *>(data);
  int16_t *_out = static_cast<int16_t *>(out);

  // TODO: maybe a circular buffer in the future?

  for (unsigned long i = 0; i < framesPerBuffer; i++)
  {
    _out[i] = _local->buffer[_local->index++];
    const bool end_of_recording = _local->index >= _local->length;
    if (end_of_recording) return paComplete;
  }
  return paContinue;
}


static void
record_audio()
{
  assert(NULL != w);
  int retval = 0;
  // reset buffer
  data.index = 0;

  retval = keypad(w, false);
  assert(ERR != retval);
  wtimeout(w, -1);
  retval = eff_fprintf(stdout, "Please press and hold the space bar to record your response\n");
  assert(0 != retval);
  wgetch(w);
  wtimeout(w, 500);
  retval = cbreak();
  assert(ERR != retval);
  retval = noecho();
  assert(ERR != retval);

  // Recording
  retval = Pa_OpenDefaultStream(&stream, 1, 0, paInt16, 16000, paFramesPerBufferUnspecified, record, &data);
  assert(0 == retval);
  retval = Pa_StartStream(stream);
  assert(0 == retval);
  while (wgetch(w) == ' ') ;

  wtimeout(w, -1);
  retval = echo();
  assert(ERR != retval);
  
  retval = Pa_StopStream(stream);
  assert(0 == retval);
  retval = Pa_CloseStream(stream);
  assert(0 == retval);
}

static void
play_audio()
{
  // record buffer length
  assert(NULL != w);
  data.rec_length = data.index;
  data.index = 0;
  int retval = eff_fprintf(stdout, "You will now hear a recording of your response\n");
  assert(0 != retval);
  retval = Pa_OpenDefaultStream(&stream, 0, 1, paInt16, 16000, paFramesPerBufferUnspecified, play, &data);
  assert(0 == retval);
  retval = Pa_StartStream(stream);
  assert(0 == retval);
  Pa_Sleep(data.rec_length / 16);
  retval = Pa_StopStream(stream);
  assert(0 == retval);
  retval = Pa_CloseStream(stream);
  assert(0 == retval);
}

static bool
is_retry()
{
  assert(NULL != w);
  int retval = keypad(w, true);
  assert(ERR != retval);
  retval = cbreak();
  assert(ERR != retval);
  retval = eff_fprintf(stdout, "Press enter if you want to record again. Press any other key to continue.\n");
  assert(0 != retval);
  int key = wgetch(w);
  assert(ERR != retval);
  retval = wrefresh(w);
  assert(ERR != retval);
  retval = keypad(w, false);
  assert(ERR != retval);
  return key == '\n';
}

static char*
eff_fgets(char * __restrict__ str, int size, FILE * __restrict__ stream)
{
  if (!is_enabled || stream != stdin) return fgets(str, size, stream);

  assert(NULL != w);
  char* input = NULL;
  int retval = 0;
  do
  {
    record_audio();
    play_audio();
    
    unsigned int buffer_size = static_cast<unsigned int>(data.rec_length);
    input = DS_SpeechToText(ctx, data.buffer, buffer_size);
    assert(NULL != input);
    retval = eff_fprintf(stdout, "Deep-speech understood: %s\n", input);
    assert(0 <= retval);
    retval = wrefresh(w);
    assert(ERR != retval);
    strncpy(str, input, size);
    DS_FreeString(input);
  } while (is_retry());

  return str;
}

static int
eff_vfprintf(FILE * __restrict__ stream, const char * __restrict__ format, va_list ap)
{
  assert(stream && format);
  if (!is_enabled || stream != stdout) return vfprintf(stream, format, ap);

  char *temp;
  int retval = vasprintf(&temp, format, ap);
  assert(NULL != temp);
  assert(0 <= retval);
  int status = wprintw(w, temp);
  assert(ERR != status);
  status = wrefresh(w);
  assert(ERR != status);
  status = espeak_Synth(temp, strlen(temp) + 1, 0, POS_CHARACTER, 0, espeakCHARS_AUTO, NULL, NULL);
  assert(EE_OK == status);
  free(temp);
  assert(NULL != w);
  return retval;
}

static int
eff_fprintf(FILE * __restrict__ stream, const char * __restrict__ format, ...)
{
  assert(stream && format);
  va_list args;
  va_start (args, format);
  int retval = 0;
  if (!is_enabled) goto stdexec;
  if (stream != stdout) goto stdexec;

  retval = eff_vfprintf(stream, format, args);
  goto finish;

stdexec: 
  retval = vfprintf(stream, format, args);
finish:
  va_end(args);
  return retval;
}

static void eff_shutdown();

static void
eff_initialize()
{
  int fd = fileno(stdout);
  is_enabled = isatty(fd);
  if (!is_enabled) return;

  // We are only going to do this if isatty
  int retval = Pa_Initialize();
  assert(0 == retval);
  w = initscr();
  assert(NULL != w);
  retval = DS_CreateModel(model_path, &ctx);
  assert(0 == retval);
  retval = DS_EnableExternalScorer(ctx, scorer_path);
  assert(0 == retval);
  retval = espeak_Initialize(output, 0, NULL, 0);
  assert(0 < retval);
  retval = espeak_SetVoiceByName(language);
  assert(0 < retval);
  atexit(eff_shutdown);
}

static void
eff_shutdown()
{
  if (!is_enabled) return;

  // Once per program...
  int retval = Pa_Terminate();
  assert(0 == retval);
  retval = endwin();
  assert(ERR != retval);
  DS_FreeModel(ctx);
}

int
fprintf(FILE * __restrict__ stream, const char * __restrict__ format, ...)
{
  va_list args;
  va_start (args, format);
  int retval = eff_vfprintf(stream, format, args);
  va_end (args);
  return retval;
}

char*
fgets(char * __restrict__ str, int size, FILE * __restrict__ stream)
{
  return eff_fgets(str, size, stream);
}

