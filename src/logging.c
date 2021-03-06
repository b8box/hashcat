/**
 * Author......: See docs/credits.txt
 * License.....: MIT
 */

#if defined (__APPLE__)
#include <stdio.h>
#endif

#include "common.h"
#include "logging.h"

int SUPPRESS_OUTPUT = 0;

static int last_len = 0;

static int log_final (FILE *fp, const char *fmt, va_list ap)
{
  if (last_len)
  {
    fputc ('\r', fp);

    for (int i = 0; i < last_len; i++)
    {
      fputc (' ', fp);
    }

    fputc ('\r', fp);
  }

  char s[4096] = { 0 };

  int max_len = (int) sizeof (s);

  int len = vsnprintf (s, (size_t)max_len, fmt, ap);

  if (len > max_len) len = max_len;

  fwrite (s, len, 1, fp);

  fflush (fp);

  last_len = len;

  return len;
}

int log_out_nn (FILE *fp, const char *fmt, ...)
{
  if (SUPPRESS_OUTPUT) return 0;

  va_list ap;

  va_start (ap, fmt);

  const int len = log_final (fp, fmt, ap);

  va_end (ap);

  return len;
}

int log_info_nn (const char *fmt, ...)
{
  if (SUPPRESS_OUTPUT) return 0;

  va_list ap;

  va_start (ap, fmt);

  const int len = log_final (stdout, fmt, ap);

  va_end (ap);

  return len;
}

int log_error_nn (const char *fmt, ...)
{
  if (SUPPRESS_OUTPUT) return 0;

  va_list ap;

  va_start (ap, fmt);

  const int len = log_final (stderr, fmt, ap);

  va_end (ap);

  return len;
}

int log_out (FILE *fp, const char *fmt, ...)
{
  if (SUPPRESS_OUTPUT) return 0;

  va_list ap;

  va_start (ap, fmt);

  const int len = log_final (fp, fmt, ap);

  va_end (ap);

  fputc ('\n', fp);

  last_len = 0;

  return len;
}

int log_info (const char *fmt, ...)
{
  if (SUPPRESS_OUTPUT) return 0;

  va_list ap;

  va_start (ap, fmt);

  const int len = log_final (stdout, fmt, ap);

  va_end (ap);

  fputc ('\n', stdout);

  last_len = 0;

  return len;
}

int log_error (const char *fmt, ...)
{
  if (SUPPRESS_OUTPUT) return 0;

  fputc ('\n', stderr);
  fputc ('\n', stderr);

  va_list ap;

  va_start (ap, fmt);

  const int len = log_final (stderr, fmt, ap);

  va_end (ap);

  fputc ('\n', stderr);
  fputc ('\n', stderr);

  last_len = 0;

  return len;
}
