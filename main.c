#include <alsa/asoundlib.h>
#include <math.h>
#include <stdbool.h>

/* sysdefault:CARD=XDJRX */
/* char* device = "sysdefault:CARD=PCH"; */
char* device = "default";            /* playback device */

#define maximum_chunks  32
#define chunk_size 2097152 //(2048 * 1024);

typedef enum {
  no_stem = 0, //normal track
  two_stems, //vocal and inst track
  three_stems, //vocal and inst and drum
} stem;


typedef signed short sample;
typedef sample* chunks;
typedef struct {
  int id;
  char name[300];
  chunks std_channel[maximum_chunks]; //array of chunks, chunk is array of pcm
  chunks voc_channel[maximum_chunks];
  chunks inst_channel[maximum_chunks]; //include drum when two_stems
  chunks drum_channel[maximum_chunks];
  int nchunks; //the number of chunks in a channel
  int length;
  double index;
  double speed;
  stem stem;
} track;

track left_track;
track right_track;

typedef struct {
  int id;
  char name[300];
} playlist;

void init_track(track* t) {
  t->stem = no_stem;
  t->nchunks = 0;
  t->length = 0;
  t->index = 0;
  t->speed = 0;
}

void _print_track(track* t) {
  printf("track {\n");
  printf("\tnchunks: %d\n", t->nchunks);
  printf("\tlength: %d\n", t->length);
  printf("\tindex: %lf\n", t->index);
  printf("\tspeed: %lf\n", t->speed);
  printf("\tstem: %d\n", t->stem);
  printf("}\n");
}

sample get_sample(track* t, chunks* channel) {
  int position = floor(t->index);
  int ichunk = position / chunk_size;
  int isample = position - (chunk_size * ichunk);
  sample* chunk = channel[ichunk];
  return chunk[isample];
}

sample calc_sample(track* t, stem stem) {
  sample smpl = 0;
  if (stem == no_stem) {
    smpl = get_sample(t, t->std_channel);
  } else {
    sample voc = get_sample(t, t->voc_channel);
    sample inst = get_sample(t, t->inst_channel);
        
    smpl = (voc + inst) / 2;
  }
  return smpl;
}

void reset_index(track* t) {
  t->index = 0;
}

sample* make_chunk() {
  sample* chunk = malloc(chunk_size * sizeof(sample));
  if (chunk == NULL) return NULL;
  return chunk;
}

FILE* open_pcm_stream(char* filename) {
  char command[1000];
  snprintf(command, sizeof(command), "ffmpeg -i %s -f s16le -ar 44100 -ac 1 -v quiet -", filename);
  FILE* fp = popen(command, "r");
  if (fp == NULL) {
    printf("failed to open stream for %s\n", filename);
    return NULL;
  }

  return fp;
}

void read_pcm(FILE* fp, int* nchunks, int* length, chunks* channel) {
  sample* chunk = make_chunk();
  while (1) {
    signed long count = fread(chunk, sizeof(sample), chunk_size, fp);
    channel[*nchunks] = chunk;
    (*nchunks)++;
    *length += count;
    if (count < chunk_size) {
      break;
    }
    if (*nchunks == maximum_chunks) {
      printf("File is too large");
      break;
    }
    chunk = make_chunk();
    /* fwrite(path, 1, 1, stdout); */
  }
}

bool has_stems(char* filename) {
  if (strstr(filename, "TODO")) {
    return true;
  }
  return false;
}

int read_file(char* filename, track* t) {
    t->stem = no_stem;
    FILE* fp = open_pcm_stream(filename);
    if (fp == NULL) {
      return -1;
    }
    read_pcm(fp, &t->nchunks, &t->length, t->std_channel);
    pclose(fp);
    return 0;
}

int read_stem(char* filename, int* nchunks, int* length, chunks* channel) {
  FILE* stem = open_pcm_stream(filename); //TODO fix filename
  if (stem == NULL) {
    return -1;
  }
  read_pcm(stem, nchunks, length, channel);
  pclose(stem);
  return 0;
}

int read_two_stems(char* filename, track* t) {
  t->stem = two_stems;
  
  if (read_stem(filename, &t->nchunks, &t->length, t->voc_channel) == -1) {
    return -1;
  }
  
  int inst_nchunks = 0;
  int inst_length = 0;
  
  if (read_stem(filename, &t->nchunks, &t->length, t->voc_channel) == -1) {
    return -1;
  }
  
  if (!(inst_nchunks == t->nchunks && inst_length == t->nchunks)) {
    printf("inst and voc has to be same length\n");
    return -1;
  }
  return 0;
}

int read_three_stems(char* filename, track* t) {
  t->stem = three_stems;
      
  read_stem(filename, &t->nchunks, &t->length, t->voc_channel);
  
  int inst_nchunks = 0;
  int inst_length = 0;
  read_stem(filename, &inst_nchunks, &inst_length, t->inst_channel);
  if (!(inst_nchunks == t->nchunks && inst_length == t->nchunks)) {
    printf("inst and voc has to be same length\n");
    return -1;
  }
  int drum_nchunks = 0;
  int drum_length = 0;
  read_stem(filename, &drum_nchunks, &drum_length, t->drum_channel);
  if (!(drum_nchunks == t->nchunks && drum_length == t->nchunks)) {
    printf("drum and voc has to be same length\n");
    return -1;
  }
  return 0;
}

int load_track(char* filename, track* t) {
  //do matching to tell stem
  if (read_two_stems(filename, t) == -1) {
    return -1;
  }

  _print_track(t);
  return 0;
}

int get_playlists() {
  char command[1000];
  FILE* fp = popen("java -jar crate-digger/target/finyl-1.0-SNAPSHOT.jar badge /media/null/22BC-F655/", "r");
  if (fp == NULL) {
    printf("failed to get playlists\n");
    return -1;
  }

  return 0;
}

void setup_alsa_params(snd_pcm_t* handle, snd_pcm_uframes_t* buffer_size, snd_pcm_uframes_t* period_size) {
  snd_pcm_hw_params_t* hw_params;
  snd_pcm_hw_params_malloc(&hw_params);
  snd_pcm_hw_params_any(handle, hw_params);
  snd_pcm_hw_params_set_access(handle, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED);
  snd_pcm_hw_params_set_format(handle, hw_params, SND_PCM_FORMAT_S16_LE);
  snd_pcm_hw_params_set_rate(handle, hw_params, 44100, 0);
  snd_pcm_hw_params_set_channels(handle, hw_params, 2);
  snd_pcm_hw_params_set_buffer_size_near(handle, hw_params, buffer_size);
  snd_pcm_hw_params_set_period_size_near(handle, hw_params, period_size, 0);
  snd_pcm_hw_params(handle, hw_params);
  snd_pcm_hw_params_free(hw_params);
  snd_pcm_get_params(handle, buffer_size, period_size);
}

void run(track* t, snd_pcm_t* handle, snd_pcm_uframes_t period_size) {
  t->speed = 1;
  int err = 0;
  sample buffer[period_size*2];
  while (1) {
    for (signed int i = 0; i < period_size*2; i=i+2) {
      //even sample i  is for headphone.
      //odd sample i+1 is for speaker.
      buffer[i] = calc_sample(t, t->stem);
      buffer[i+1] = calc_sample(t, t->stem);
      
      t->index = t->index + 1;
      if (t->index >= t->length) {
        reset_index(t);
      }
    }
    
    err = snd_pcm_writei(handle, buffer, period_size);
    if (err == -EPIPE) {
      printf("Underrun occurred: %s\n", snd_strerror(err));
      snd_pcm_prepare(handle);
    } else if (err == -EAGAIN) {
    } else if (err < 0) {
      printf("error %s\n", snd_strerror(err));
      goto cleanup;
    }
  }
  
 cleanup:
  err = snd_pcm_drain(handle);
  if (err < 0)
    printf("snd_pcm_drain failed: %s\n", snd_strerror(err));
  snd_pcm_close(handle);
  printf("closed\n");
}

void setup_alsa(snd_pcm_t** handle, snd_pcm_uframes_t* buffer_size, snd_pcm_uframes_t* period_size) {
  int err;
  if ((err = snd_pcm_open(handle, device, SND_PCM_STREAM_PLAYBACK, 0)) < 0) {
    printf("Playback open error!: %s\n", snd_strerror(err));
    exit(1);
  }
  setup_alsa_params(*handle, buffer_size, period_size);
  printf("buffer_size %ld, period_size %ld\n", *buffer_size, *period_size);
}


