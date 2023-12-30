#include <sys/resource.h>
#include <stdio.h>
#include <unistd.h>
#include <termios.h>
#include "finyl.h"
#include "util.h"
#include "digger.h"
#include "dev.h"
#include "interface.h"
#include <pthread.h>

double max(double a, double b) {
  return a>b ? a : b;
}

double min(double a, double b) {
  return a<b ? a : b;
}

void slide_right(finyl_track* t) {
  double backup = t->speed;
  double y = 0;
  double x = 0;
  while (y>=0) {
    y  = -x*(x-2);
    t->speed = y;
    x = 0.01 + x;
    usleep(1000);
    printf("t is %lf\n", t->speed);
  }
  t->speed = backup;
}

//load track 5 to adeck
void load_track(finyl_track** dest, int tid, finyl_track_target deck) {
  finyl_track* before = *dest;
  
  finyl_track* t = new finyl_track;

  if (get_track(t, usb, tid) == -1) {
    printf("failed\n");
    return;
  }
  
  char* files[1] = {t->meta.filepath};
  if (finyl_read_channels_from_files(files, 1, t) == -1) {
    return;
  }
  
  print_track(t);
  
  *dest = t;
  if (deck == finyl_a) {
    render_adeck = true;
  } else if (deck == finyl_b) {
    render_bdeck = true;
  }
  
  if (before != NULL) {
    add_track_to_free(before);
    struct rusage usage;
    getrusage(RUSAGE_SELF, &usage);
    printf("Memory usage: %ld kilobytes\n", usage.ru_maxrss);
  }
}

void load_track_2channels(finyl_track** dest, int tid, finyl_track_target deck) {
  finyl_track* before = *dest;
  
  finyl_track* t = new finyl_track;
  if (get_track(t, usb, tid) == -1) {
    printf("failed\n");
    return;
  }
  
  
  if (t->meta.channels_size < 2) {
    finyl_free_track(t);
    printf("not enough channels\n");
    return;
  }
  
  if (finyl_read_channels_from_files(t->meta.channel_filepaths, 2, t) == -1) {
    return;
  }
  
  print_track(t);
  
  *dest = t;
  if (deck == finyl_a) {
    render_adeck = true;
  } else if (deck == finyl_b) {
    render_bdeck = true;
  }

  if (before != NULL) {
    add_track_to_free(before);
    struct rusage usage;
    getrusage(RUSAGE_SELF, &usage);
    printf("Memory usage: %ld kilobytes\n", usage.ru_maxrss);
  }
  
}

void magic(int port, struct termios* tty) {
  //reads existing settings
  if(tcgetattr(port, tty) != 0) {
    printf("Error %i from tcgetattr: %s\n", errno, strerror(errno));
    return;
  }

  cfsetispeed(tty, B9600);
  cfsetospeed(tty, B9600);
  
  tty->c_cflag &= ~PARENB; // Clear parity bit
  tty->c_cflag &= ~CSTOPB; // Clear stop field
  tty->c_cflag &= ~CSIZE; // Clear all bits that set the data size 
  tty->c_cflag |= CS8; // 8 bits per byte
  tty->c_cflag &= ~CRTSCTS; // Disable RTS/CTS hardware flow control
  tty->c_cflag |= CREAD | CLOCAL; // Turn on READ & ignore ctrl lines

  tty->c_lflag &= ~ICANON;
  tty->c_lflag &= ~ECHO; // Disable echo
  tty->c_lflag &= ~ECHOE; // Disable erasure
  tty->c_lflag &= ~ECHONL; // Disable new-line echo
  tty->c_lflag &= ~ISIG; // Disable interpretation of INTR, QUIT and SUSP
  tty->c_iflag &= ~(IXON | IXOFF | IXANY); // Turn off s/w flow ctrl
  tty->c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL); // Disable any special handling of received bytes

  tty->c_oflag &= ~OPOST; // Prevent special interpretation of output bytes (e.g. newline chars)
  tty->c_oflag &= ~ONLCR; // Prevent conversion of newline to carriage return/line feed

  tty->c_cc[VTIME] = 10;    // Wait for up to 1s (10 deciseconds), returning as soon as any data is received.
  tty->c_cc[VMIN] = 0;
}

void pot(int v) {
  //v: 0 to 4095
  double gain = v / 4095.0;
  a0_gain = gain;
}

void handle_pot(char* s) {
  int len = strlen(s);
  int i = find_char_last(s, ':');
  if (i != strlen("analog")) {
    //no valid
    return;
  }
  int vlen = len - i - 1;
  char v[vlen+1];
  strncpy(v, &s[i+1], vlen);
  v[vlen] = '\0';
  int n = atoi(v);
  pot(n);
}

void* serial(void* args) {
  int serialPort = open("/dev/ttyUSB0", O_RDWR);

  printf("serial\n");
  
  if (serialPort < 0) {
    printf("Error %i from open: %s\n", errno, strerror(errno));
    return NULL;
  }

  struct termios tty;
  magic(serialPort, &tty);
  
  if (tcsetattr(serialPort, TCSANOW, &tty) != 0) {
    printf("Error %i from tcsetattr: %s\n", errno, strerror(errno));
    return NULL;
  }
  
  char tmp[100];
  int tmplen = 0;
  while(1) {
    char read_buf [256]; //could contain error
    int num_bytes = read(serialPort, &read_buf, sizeof(read_buf));
    if (num_bytes < 0) {
      printf("Error reading: %s\n", strerror(errno));
      return NULL;
    }
    
    int start_i = 0;
    for (int i = 0; i<num_bytes; i++) {
      int linelen = i - start_i; //\n not counted
      char c = read_buf[i];
      if (c != '\n') {
        continue;
      }
      if (tmplen > 0) {
        int len = tmplen + linelen;
        char s[len+1];
        strncpy(s, tmp, tmplen);
        s[tmplen] = '\0';
        strncat(s, &read_buf[start_i], linelen);
        handle_pot(s);
        tmplen = 0;
      } else {
        if (linelen == 0) continue;
        //no need to care tmp
        char s[linelen+1];
        strncpy(s, &read_buf[start_i], linelen);
        s[linelen] = '\0';
        handle_pot(s);
      }

      start_i = i+1; //i+1 is next to newline
    }

    tmplen = num_bytes-start_i;
    strncpy(tmp, &read_buf[start_i], tmplen);
  }
  
  close(serialPort);
  return NULL;
}

void* _interface(void*) {
  interface();
  return NULL;
}

void start_interface() {
  pthread_t interface_thread;
  pthread_create(&interface_thread, NULL, _interface, NULL);
}

void handleKey(char x) {
  switch (x) {
  case 'h':
    bdeck->playing = !bdeck->playing;
    printf("bdeck is playing:%d\n", bdeck->playing);
    break;
  case 'g':
    adeck->playing = !adeck->playing;
    printf("adeck is playing:%d\n", adeck->playing);
    break;
  case 'G':
    adeck->speed = 1;
    break;
  case 'H':
    bdeck->speed = 1;
    break;
  case 'N':
    a0_gain = max(a0_gain-0.05, 0);
    printf("a0_gain %lf\n", a0_gain);
    break;
  case 'J':
    a0_gain = min(a0_gain+0.05, 1);
    printf("a0_gain %lf\n", a0_gain);
    break;
  case 'n':
    a1_gain = max(a1_gain-0.05, 0);
    printf("a1_gain %lf\n", a1_gain);
    break;
  case 'j':
    a1_gain = min(a1_gain+0.05, 1);
    printf("a1_gain %lf\n", a1_gain);
    break;
  case 'M':
    b0_gain = max(b0_gain-0.05, 0);
    printf("b0_gain %lf\n", b0_gain);
    break;
  case 'K':
    b0_gain = min(b0_gain+0.1, 1);
    printf("b0_gain %lf\n", b0_gain);
    break;
  case 'm':
    b1_gain = max(b1_gain-0.05, 0);
    printf("b1_gain %lf\n", b1_gain);
    break;
  case 'k':
    b1_gain = min(b1_gain+0.05, 1);
    printf("b1_gain %lf\n", b1_gain);
    break;

  case 'c':
    if (adeck->cues.size() > 0) {
      adeck->index = adeck->cues[0].time * 44.1;
      printf("jumped to %lf\n", adeck->index);
    }
    break;
  case 'C':
    if (bdeck->cues.size() > 0) {
      bdeck->index = bdeck->cues[0].time * 44.1;
      printf("jumped to %lf\n", bdeck->index);
    }
    break;
  case 'p':
    print_track(adeck);
    print_track(bdeck);
    break;
  case 'a':
    adeck->speed = adeck->speed + 0.01;
    break;
  case 's':
    adeck->speed = adeck->speed - 0.01;
    break;
  case 'A':
    bdeck->speed = bdeck->speed + 0.01;
    break;
  case 'S':
    bdeck->speed = bdeck->speed - 0.01;
    break;
  case 't': {
    int millisec = finyl_get_quantized_time(adeck, adeck->index);
    adeck->index = millisec * 44.1;
    printf("adeck->index %lf\n", adeck->index);
    break;
  }
  case 'T': {
    int millisec = finyl_get_quantized_time(bdeck, bdeck->index);
    bdeck->index = millisec * 44.1;
    printf("bdeck->index %lf\n", bdeck->index);
    break;
  }
  case 'L':
    list_playlists();
    break;
  case 'l': {
    int pid;
    printf("pid:");
    scanf("%d", &pid);
    printf("listing...\n");
    list_playlist_tracks(pid);
    break;
  }
  case '4':
    free_tracks();
    break;
  case 'i': {
    int tid;
    printf("tid:");
    scanf("%d", &tid);
    printf("loading...%d\n", tid);
    load_track_2channels(&adeck, tid, finyl_a);
    break;
  }
  case 'o': {
    int tid;
    printf("tid:");
    scanf("%d", &tid);
    printf("loading...%d\n", tid);
    load_track_2channels(&bdeck, tid, finyl_b);
    break;
  }
  case '9': {
    int tid;
    printf("tid:");
    scanf("%d", &tid);
    printf("loading...%d\n", tid);
    load_track(&adeck, tid, finyl_a);
    break;
  }
  case '0': {
    int tid;
    printf("tid:");
    scanf("%d", &tid);
    printf("loading...%d\n", tid);
    load_track(&bdeck, tid, finyl_b);
    break;
  }
  case 'q': {
    bdeck->speed = adeck->speed * ((double)adeck->meta.bpm / bdeck->meta.bpm);
    printf("synced bdeck->speed: %lf\n", bdeck->speed);
    break;
  }
  case 'Q': {
    adeck->speed = bdeck->speed * ((double)bdeck->meta.bpm / adeck->meta.bpm);
    printf("synced adeck->speed: %lf\n", adeck->speed);
    break;
  }
  case '1':
    /* mark loop in */
    adeck->loop_in = 44.1 * finyl_get_quantized_time(adeck, adeck->index);
    adeck->loop_out = -1.0;
    printf("adeck loop in: %lf\n", adeck->loop_in);
    break;
  case '2': {
    double now = 44.1 * finyl_get_quantized_time(adeck, adeck->index);
    if (adeck->loop_in > now) {
      adeck->loop_in = -1;
    } else {
      adeck->loop_out = now;
      printf("adeck loop out: %lf\n", adeck->loop_out);
    }
    break;
  }
  case '!':
    /* mark loop in */
    bdeck->loop_in = 44.1 * finyl_get_quantized_time(bdeck, bdeck->index);
    bdeck->loop_out = -1.0;
    printf("bdeck loop in: %lf\n", bdeck->loop_in);
    break;
  case '`': {
    double now = 44.1 * finyl_get_quantized_time(bdeck, bdeck->index);
    if (bdeck->loop_in > now) {
      bdeck->loop_in = -1;
    } else {
      bdeck->loop_out = now;
      printf("bdeck loop out: %lf\n", bdeck->loop_out);
    }
    break;
  }
  case '3':{
    start_interface();
    break;
  }
  case 'v':
    adeck->index += 300;
    break;
  case 'b':
    bdeck->index += 300;
    break;
  case 'V':
    adeck->index -= 300;
    break;
  case 'B':
    bdeck->index -= 300;
    break;
  case '5':
    adeck->index += 3000;
    break;
  case '6':
    bdeck->index += 3000;
    break;
  case '%':
    adeck->index -= 3000;
    break;
  case '&':
    bdeck->index -= 3000;
    break;
  case '7': {
    set_wave_range(wave_range*2);
    break;
  }
  case '\'': {
    set_wave_range(wave_range/2);
    break;
  }
  }
}

void* key_input(void* args) {
  static struct termios oldt, newt;
  
  tcgetattr(STDIN_FILENO, &oldt);
  newt = oldt;
  newt.c_lflag &= ~(ICANON);          
  tcsetattr(STDIN_FILENO, TCSANOW, &newt);
  while(finyl_running) {
    handleKey(getchar());                 
  }
  tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
  printf("key_input closed\n");
  return NULL;
}

void* controller(void* args) {
  printf("deck initializing\n");
  
  load_track(&adeck, 1, finyl_a);
  load_track(&bdeck, 1, finyl_b);
  
  printf("deck initialized\n");
  
  pthread_t k;
  pthread_create(&k, NULL, key_input, NULL);
  pthread_t s;
  pthread_create(&s, NULL, serial, NULL);
  
  pthread_join(k, NULL);
  pthread_cancel(s);
  
  return NULL;
}
