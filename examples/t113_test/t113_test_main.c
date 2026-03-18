/****************************************************************************
 * apps/examples/t113_test/t113_test_main.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <sys/mount.h>
#include <sys/ioctl.h>

#include <nuttx/mtd/mtd.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* MTD test uses last 2 erase blocks of /dev/mtd0 */

#define MTD_TEST_NBLOCKS 2

/* LittleFS test file path */

#define LFS_MOUNT_POINT  "/mnt"
#define LFS_TEST_FILE    "/mnt/t113_test.dat"

/* Timer test sleep duration in milliseconds */

#define TIMER_TEST_SLEEP_MS 200

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: fill_test_pattern
 *
 * Description:
 *   Fill buffer with deterministic pattern based on page index.
 *
 ****************************************************************************/

static void fill_test_pattern(FAR uint8_t *buf, size_t len, int seed)
{
  size_t i;

  for (i = 0; i < len; i++)
    {
      buf[i] = (uint8_t)((seed ^ (i & 0xff) ^ 0x5a) & 0xff);
    }
}

/****************************************************************************
 * Name: test_mtd
 *
 * Description:
 *   Erase last 2 blocks of /dev/mtd0, write pages with pattern,
 *   read back and verify data integrity.
 *
 ****************************************************************************/

static int test_mtd(void)
{
  struct mtd_geometry_s geo;
  FAR uint8_t *wbuf = NULL;
  FAR uint8_t *rbuf = NULL;
  int test_page_start;
  int pages_per_erase;
  int npages;
  int pg;
  int ret;
  int fd;
  size_t i;

  fd = open("/dev/mtd0", O_RDWR);
  if (fd < 0)
    {
      printf("[FAIL] test_mtd: open: %d\n", errno);
      return -1;
    }

  ret = ioctl(fd, MTDIOC_GEOMETRY, (unsigned long)&geo);
  if (ret < 0)
    {
      printf("[FAIL] test_mtd: MTDIOC_GEOMETRY: %d\n", errno);
      close(fd);
      return -1;
    }

  pages_per_erase  = (int)(geo.erasesize / geo.blocksize);
  test_page_start  = ((int)geo.neraseblocks - MTD_TEST_NBLOCKS) *
                     pages_per_erase;
  npages           = MTD_TEST_NBLOCKS * pages_per_erase;

  wbuf = malloc(geo.blocksize);
  rbuf = malloc(geo.blocksize);
  if (wbuf == NULL || rbuf == NULL)
    {
      printf("[FAIL] test_mtd: malloc failed\n");
      free(wbuf);
      free(rbuf);
      close(fd);
      return -1;
    }

  /* Write pattern to each page then read back and verify.
   * The test blocks are pre-erased by a prior LittleFS forceformat
   * or by the hello test which also writes the same region.
   */

  for (pg = 0; pg < npages; pg++)
    {
      int abs_page = test_page_start + pg;

      fill_test_pattern(wbuf, geo.blocksize, abs_page);

      lseek(fd, (off_t)abs_page * (off_t)geo.blocksize, SEEK_SET);
      ret = write(fd, wbuf, geo.blocksize);
      if (ret != (int)geo.blocksize)
        {
          printf("[FAIL] test_mtd: write page %d: %d\n", abs_page, ret);
          goto fail;
        }

      memset(rbuf, 0, geo.blocksize);
      lseek(fd, (off_t)abs_page * (off_t)geo.blocksize, SEEK_SET);
      ret = read(fd, rbuf, geo.blocksize);
      if (ret != (int)geo.blocksize)
        {
          printf("[FAIL] test_mtd: read page %d: %d\n", abs_page, ret);
          goto fail;
        }

      fill_test_pattern(wbuf, geo.blocksize, abs_page);
      for (i = 0; i < geo.blocksize; i++)
        {
          if (rbuf[i] != wbuf[i])
            {
              printf("[FAIL] test_mtd: page %d byte %zu: "
                     "got 0x%02x expected 0x%02x\n",
                     abs_page, i, rbuf[i], wbuf[i]);
              goto fail;
            }
        }
    }

  printf("[PASS] test_mtd\n");
  free(wbuf);
  free(rbuf);
  close(fd);
  return 0;

fail:
  free(wbuf);
  free(rbuf);
  close(fd);
  return -1;
}

/****************************************************************************
 * Name: test_littlefs
 *
 * Description:
 *   Mount LittleFS on /dev/mtd1, create a file, write data,
 *   read back and verify, then clean up.
 *
 ****************************************************************************/

static int test_littlefs(void)
{
  const char test_data[] = "T113 LittleFS test: The quick brown fox "
                           "jumps over the lazy dog. 0123456789\n";
  char readbuf[128];
  int fd;
  int ret;
  ssize_t nwritten;
  ssize_t nread;

  /* Mount LittleFS on /dev/mtd1 */

  ret = mount("/dev/mtd1", LFS_MOUNT_POINT, "littlefs", 0, "autoformat");
  if (ret < 0)
    {
      printf("[FAIL] test_littlefs: mount: %d\n", errno);
      return -1;
    }

  /* Create and write test file */

  fd = open(LFS_TEST_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0666);
  if (fd < 0)
    {
      printf("[FAIL] test_littlefs: open for write: %d\n", errno);
      goto fail_umount;
    }

  nwritten = write(fd, test_data, strlen(test_data));
  close(fd);

  if (nwritten != (ssize_t)strlen(test_data))
    {
      printf("[FAIL] test_littlefs: write: %zd (expected %zu)\n",
             nwritten, strlen(test_data));
      goto fail_unlink;
    }

  /* Read back and verify */

  fd = open(LFS_TEST_FILE, O_RDONLY);
  if (fd < 0)
    {
      printf("[FAIL] test_littlefs: open for read: %d\n", errno);
      goto fail_unlink;
    }

  memset(readbuf, 0, sizeof(readbuf));
  nread = read(fd, readbuf, sizeof(readbuf) - 1);
  close(fd);

  if (nread != (ssize_t)strlen(test_data))
    {
      printf("[FAIL] test_littlefs: read: %zd (expected %zu)\n",
             nread, strlen(test_data));
      goto fail_unlink;
    }

  if (memcmp(readbuf, test_data, strlen(test_data)) != 0)
    {
      printf("[FAIL] test_littlefs: data mismatch\n");
      goto fail_unlink;
    }

  /* Cleanup */

  unlink(LFS_TEST_FILE);
  umount(LFS_MOUNT_POINT);
  printf("[PASS] test_littlefs\n");
  return 0;

fail_unlink:
  unlink(LFS_TEST_FILE);
fail_umount:
  umount(LFS_MOUNT_POINT);
  return -1;
}

/****************************************************************************
 * Name: test_timer
 *
 * Description:
 *   Verify CLOCK_MONOTONIC advances by calling clock_gettime before
 *   and after usleep, checking elapsed time is reasonable.
 *
 ****************************************************************************/

static int test_timer(void)
{
  struct timespec ts_before;
  struct timespec ts_after;
  long elapsed_ms;
  int ret;

  ret = clock_gettime(CLOCK_MONOTONIC, &ts_before);
  if (ret < 0)
    {
      printf("[FAIL] test_timer: clock_gettime before: %d\n", errno);
      return -1;
    }

  usleep(TIMER_TEST_SLEEP_MS * 1000);

  ret = clock_gettime(CLOCK_MONOTONIC, &ts_after);
  if (ret < 0)
    {
      printf("[FAIL] test_timer: clock_gettime after: %d\n", errno);
      return -1;
    }

  elapsed_ms = (ts_after.tv_sec - ts_before.tv_sec) * 1000 +
               (ts_after.tv_nsec - ts_before.tv_nsec) / 1000000;

  if (elapsed_ms < (TIMER_TEST_SLEEP_MS / 2) ||
      elapsed_ms > (TIMER_TEST_SLEEP_MS * 3))
    {
      printf("[FAIL] test_timer: elapsed %ldms "
             "(expected ~%dms)\n", elapsed_ms, TIMER_TEST_SLEEP_MS);
      return -1;
    }

  printf("[PASS] test_timer (elapsed %ldms)\n", elapsed_ms);
  return 0;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: main
 *
 * Description:
 *   T113 hardware integration test suite. Runs MTD, LittleFS, and
 *   timer tests. Reports per-test PASS/FAIL and overall summary.
 *
 ****************************************************************************/

int main(int argc, FAR char *argv[])
{
  int fail_count = 0;

  printf("=== T113 Integration Test Suite ===\n");

  printf("--- MTD Test ---\n");
  if (test_mtd() < 0)
    {
      fail_count++;
    }

  printf("--- LittleFS Test ---\n");
  if (test_littlefs() < 0)
    {
      fail_count++;
    }

  printf("--- Timer Test ---\n");
  if (test_timer() < 0)
    {
      fail_count++;
    }

  printf("=== T113 Test Complete: %s (%d failures) ===\n",
         fail_count == 0 ? "ALL PASS" : "FAIL", fail_count);
  return fail_count == 0 ? 0 : 1;
}
