#include "utils.h"

void remove_right_space(char *s)
{
    char *c = &(s[strlen(s) - 1]);
    while (*c == '\n' || *c == '\r' || *c == '\t' || *c == ' ')
    {
        *c = '\0';
        --c;
    }
}
