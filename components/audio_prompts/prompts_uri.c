/*This is tone file*/

const char* tone_uri[] = {
   "flash://tone/0_ding.mp3",
   "flash://tone/1_fstart.mp3",
   "flash://tone/2_hello.mp3",
   "flash://tone/3_start.mp3",
   "flash://tone/4_stop.mp3",
   "flash://tone/5_vstart.mp3",
};

int get_tone_uri_num()
{
    return sizeof(tone_uri) / sizeof(char *) - 1;
}
