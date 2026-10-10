#ifndef GLUE_H
#define GLUE_H

void glue_setup(int samples);
void glue_begin(int width, int height, float r, float g, float b);
void glue_viewport(int x, int y, int width, int height);
void glue_text(int size);
void glue_end(void);
void glue_shutdown(void);

#endif
