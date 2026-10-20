#ifndef GLUE_H
#define GLUE_H

void glue_setup(int samples);
void glue_crt(int on);
void glue_begin(int width, int height, int x, int y, int area_width, int area_height, float r, float g, float b);
void glue_layer(int layer);
void glue_text(int size);
void glue_end(void);
void glue_shutdown(void);

#endif
