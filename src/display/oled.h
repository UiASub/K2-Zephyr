#ifndef OLED_H
#define OLED_H

#ifdef CONFIG_K2_OLED
int display_print(const char *line1, const char *line2);
void display_start(void);
#else
static inline int display_print(const char *line1, const char *line2)
{
	(void)line1;
	(void)line2;
	return 0;
}

static inline void display_start(void)
{
}
#endif

#endif /* OLED_H */
