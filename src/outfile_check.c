/**
 * Author......: See docs/credits.txt
 * License.....: MIT
 */

#include "common.h"
#include "types.h"
#include "memory.h"
#include "interface.h"
#include "timer.h"
#include "folder.h"
#include "ext_OpenCL.h"
#include "ext_ADL.h"
#include "ext_nvapi.h"
#include "ext_nvml.h"
#include "ext_xnvctrl.h"
#include "mpsp.h"
#include "rp_cpu.h"
#include "tuningdb.h"
#include "opencl.h"
#include "hwmon.h"
#include "restore.h"
#include "hash_management.h"
#include "outfile.h"
#include "potfile.h"
#include "debugfile.h"
#include "loopback.h"
#include "data.h"
#include "status.h"
#include "convert.h"
#include "shared.h"
#include "thread.h"
#include "outfile_check.h"

extern hc_global_data_t data;

void *thread_outfile_remove (void *p)
{
  // some hash-dependent constants

  opencl_ctx_t *opencl_ctx = data.opencl_ctx;
  hashconfig_t *hashconfig = data.hashconfig;
  hashes_t     *hashes     = data.hashes;

  uint dgst_size  = hashconfig->dgst_size;
  uint is_salted  = hashconfig->is_salted;
  uint esalt_size = hashconfig->esalt_size;
  uint hash_mode  = hashconfig->hash_mode;
  char separator  = hashconfig->separator;

  char *outfile_dir        = data.outfile_check_directory;
  uint outfile_check_timer = data.outfile_check_timer;

  // buffers
  hash_t hash_buf = { 0, 0, 0, 0, 0 };

  hash_buf.digest = mymalloc (dgst_size);

  if (is_salted)  hash_buf.salt =  (salt_t *) mymalloc (sizeof (salt_t));
  if (esalt_size) hash_buf.esalt = (void   *) mymalloc (esalt_size);

  uint digest_buf[64] = { 0 };

  outfile_data_t *out_info = NULL;

  char **out_files = NULL;

  time_t folder_mtime = 0;

  int  out_cnt = 0;

  uint check_left = outfile_check_timer; // or 1 if we want to check it at startup

  while (data.shutdown_inner == 0)
  {
    hc_sleep (1);

    if (opencl_ctx->devices_status != STATUS_RUNNING) continue;

    check_left--;

    if (check_left == 0)
    {
      struct stat outfile_check_stat;

      if (stat (outfile_dir, &outfile_check_stat) == 0)
      {
        uint is_dir = S_ISDIR (outfile_check_stat.st_mode);

        if (is_dir == 1)
        {
          if (outfile_check_stat.st_mtime > folder_mtime)
          {
            char **out_files_new = scan_directory (outfile_dir);

            int out_cnt_new = count_dictionaries (out_files_new);

            outfile_data_t *out_info_new = NULL;

            if (out_cnt_new > 0)
            {
              out_info_new = (outfile_data_t *) mycalloc (out_cnt_new, sizeof (outfile_data_t));

              for (int i = 0; i < out_cnt_new; i++)
              {
                out_info_new[i].file_name = out_files_new[i];

                // check if there are files that we have seen/checked before (and not changed)

                for (int j = 0; j < out_cnt; j++)
                {
                  if (strcmp (out_info[j].file_name, out_info_new[i].file_name) == 0)
                  {
                    struct stat outfile_stat;

                    if (stat (out_info_new[i].file_name, &outfile_stat) == 0)
                    {
                      if (outfile_stat.st_ctime == out_info[j].ctime)
                      {
                        out_info_new[i].ctime = out_info[j].ctime;
                        out_info_new[i].seek  = out_info[j].seek;
                      }
                    }
                  }
                }
              }
            }

            local_free (out_info);
            local_free (out_files);

            out_files = out_files_new;
            out_cnt   = out_cnt_new;
            out_info  = out_info_new;

            folder_mtime = outfile_check_stat.st_mtime;
          }

          for (int j = 0; j < out_cnt; j++)
          {
            FILE *fp = fopen (out_info[j].file_name, "rb");

            if (fp != NULL)
            {
              //hc_thread_mutex_lock (mux_display);

              #if defined (_POSIX)
              struct stat outfile_stat;

              fstat (fileno (fp), &outfile_stat);
              #endif

              #if defined (_WIN)
              struct stat64 outfile_stat;

              _fstat64 (fileno (fp), &outfile_stat);
              #endif

              if (outfile_stat.st_ctime > out_info[j].ctime)
              {
                out_info[j].ctime = outfile_stat.st_ctime;
                out_info[j].seek  = 0;
              }

              fseek (fp, out_info[j].seek, SEEK_SET);

              char *line_buf = (char *) mymalloc (HCBUFSIZ_LARGE);

              while (!feof (fp))
              {
                char *ptr = fgets (line_buf, HCBUFSIZ_LARGE - 1, fp);

                if (ptr == NULL) break;

                int line_len = strlen (line_buf);

                if (line_len <= 0) continue;

                int iter = MAX_CUT_TRIES;

                for (uint i = line_len - 1; i && iter; i--, line_len--)
                {
                  if (line_buf[i] != separator) continue;

                  int parser_status = PARSER_OK;

                  if ((hash_mode != 2500) && (hash_mode != 6800))
                  {
                    parser_status = hashconfig->parse_func (line_buf, line_len - 1, &hash_buf, hashconfig);
                  }

                  uint found = 0;

                  if (parser_status == PARSER_OK)
                  {
                    for (uint salt_pos = 0; (found == 0) && (salt_pos < hashes->salts_cnt); salt_pos++)
                    {
                      if (hashes->salts_shown[salt_pos] == 1) continue;

                      salt_t *salt_buf = &hashes->salts_buf[salt_pos];

                      for (uint digest_pos = 0; (found == 0) && (digest_pos < salt_buf->digests_cnt); digest_pos++)
                      {
                        uint idx = salt_buf->digests_offset + digest_pos;

                        if (hashes->digests_shown[idx] == 1) continue;

                        uint cracked = 0;

                        if (hash_mode == 6800)
                        {
                          if (i == salt_buf->salt_len)
                          {
                            cracked = (memcmp (line_buf, salt_buf->salt_buf, salt_buf->salt_len) == 0);
                          }
                        }
                        else if (hash_mode == 2500)
                        {
                          // BSSID : MAC1 : MAC2 (:plain)
                          if (i == (salt_buf->salt_len + 1 + 12 + 1 + 12))
                          {
                            cracked = (memcmp (line_buf, salt_buf->salt_buf, salt_buf->salt_len) == 0);

                            if (!cracked) continue;

                            // now compare MAC1 and MAC2 too, since we have this additional info
                            char *mac1_pos = line_buf + salt_buf->salt_len + 1;
                            char *mac2_pos = mac1_pos + 12 + 1;

                            wpa_t *wpas = (wpa_t *) hashes->esalts_buf;
                            wpa_t *wpa  = &wpas[salt_pos];

                            // compare hex string(s) vs binary MAC address(es)

                            for (uint i = 0, j = 0; i < 6; i++, j += 2)
                            {
                              if (wpa->orig_mac1[i] != hex_to_u8 ((const u8 *) &mac1_pos[j]))
                              {
                                cracked = 0;

                                break;
                              }
                            }

                            // early skip ;)
                            if (!cracked) continue;

                            for (uint i = 0, j = 0; i < 6; i++, j += 2)
                            {
                              if (wpa->orig_mac2[i] != hex_to_u8 ((const u8 *) &mac2_pos[j]))
                              {
                                cracked = 0;

                                break;
                              }
                            }
                          }
                        }
                        else
                        {
                          char *digests_buf_ptr = (char *) hashes->digests_buf;

                          memcpy (digest_buf, digests_buf_ptr + (hashes->salts_buf[salt_pos].digests_offset * dgst_size) + (digest_pos * dgst_size), dgst_size);

                          cracked = (sort_by_digest_p0p1 (digest_buf, hash_buf.digest) == 0);
                        }

                        if (cracked == 1)
                        {
                          found = 1;

                          hashes->digests_shown[idx] = 1;

                          hashes->digests_done++;

                          salt_buf->digests_done++;

                          if (salt_buf->digests_done == salt_buf->digests_cnt)
                          {
                            hashes->salts_shown[salt_pos] = 1;

                            hashes->salts_done++;

                            if (hashes->salts_done == hashes->salts_cnt) mycracked (opencl_ctx);
                          }
                        }
                      }

                      if (opencl_ctx->devices_status == STATUS_CRACKED) break;
                    }
                  }

                  if (found) break;

                  if (opencl_ctx->devices_status == STATUS_CRACKED) break;

                  iter--;
                }

                if (opencl_ctx->devices_status == STATUS_CRACKED) break;
              }

              myfree (line_buf);

              out_info[j].seek = ftell (fp);

              //hc_thread_mutex_unlock (mux_display);

              fclose (fp);
            }
          }
        }
      }

      check_left = outfile_check_timer;
    }
  }

  if (esalt_size) local_free (hash_buf.esalt);

  if (is_salted)  local_free (hash_buf.salt);

  local_free (hash_buf.digest);

  local_free (out_info);

  local_free (out_files);

  p = NULL;

  return (p);
}
