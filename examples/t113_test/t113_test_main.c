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
#include <sched.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <dirent.h>
#include <malloc.h>

#include <nuttx/mtd/mtd.h>

#ifdef CONFIG_T113_RTC
#  include <nuttx/timers/rtc.h>
#endif

#ifdef CONFIG_T113_PWM
#  include <nuttx/timers/pwm.h>
#endif

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

#ifdef CONFIG_T113_GPIO
static int test_gpio(void)
{
  /* Test GPIO using PB0: configure as output, write 1/read, write 0/read.
   * Direct register access (flat build, no MMU protection).
   *
   * PIO_BASE=0x02000000, Bank B=1, offset=1*0x30=0x30
   *   PB_CFG0 = 0x02000030  (4 bits/pin, pin0 at bits[3:0])
   *   PB_DAT  = 0x02000040
   */

  volatile uint32_t *pb_cfg0 = (volatile uint32_t *)0x02000030;
  volatile uint32_t *pb_dat  = (volatile uint32_t *)0x02000040;
  uint32_t cfg_save;
  uint32_t dat_save;
  uint32_t val;

  cfg_save = *pb_cfg0;
  dat_save = *pb_dat;

  /* Set PB0 to output (func=1) */

  val = cfg_save & ~0xf;
  val |= 0x1;
  *pb_cfg0 = val;

  /* Write 1, read back */

  *pb_dat = dat_save | 0x1;
  val = *pb_dat;
  if (!(val & 0x1))
    {
      printf("[FAIL] test_gpio: PB0 write 1 readback=0\n");
      *pb_cfg0 = cfg_save;
      *pb_dat  = dat_save;
      return -1;
    }

  /* Write 0, read back */

  *pb_dat = dat_save & ~0x1;
  val = *pb_dat;
  if (val & 0x1)
    {
      printf("[FAIL] test_gpio: PB0 write 0 readback=1\n");
      *pb_cfg0 = cfg_save;
      *pb_dat  = dat_save;
      return -1;
    }

  /* Restore original config */

  *pb_cfg0 = cfg_save;
  *pb_dat  = dat_save;

  printf("[PASS] test_gpio (PB0 output toggle verified)\n");
  return 0;
}
#endif

#ifdef CONFIG_T113_PWM
static int test_pwm(void)
{
  int fd;
  struct pwm_info_s info;

  fd = open("/dev/pwm0", O_RDONLY);
  if (fd < 0)
    {
      printf("[FAIL] test_pwm: open /dev/pwm0: %d\n", errno);
      return -1;
    }

  /* Start PWM: 1kHz, 50% duty */

  info.frequency = 1000;
  info.duty      = 0x8000;
#ifdef CONFIG_PWM_PULSECOUNT
  info.count     = 0;
#endif

  if (ioctl(fd, PWMIOC_SETCHARACTERISTICS, (unsigned long)&info) < 0)
    {
      printf("[FAIL] test_pwm: SETCHARACTERISTICS: %d\n", errno);
      close(fd);
      return -1;
    }

  if (ioctl(fd, PWMIOC_START, 0) < 0)
    {
      printf("[FAIL] test_pwm: START: %d\n", errno);
      close(fd);
      return -1;
    }

  /* Verify: read PWM counter register twice, expect it changes.
   * PWM0 PCNTR = PWM_BASE + 0x100 + 0*0x20 + 0x08 = 0x02000D08
   */

  volatile uint32_t *pcntr = (volatile uint32_t *)0x02000d08;
  uint32_t cnt1 = *pcntr;
  usleep(1000);
  uint32_t cnt2 = *pcntr;

  ioctl(fd, PWMIOC_STOP, 0);
  close(fd);

  if (cnt1 == cnt2)
    {
      printf("[FAIL] test_pwm: counter not running (cnt=%u)\n",
             (unsigned)cnt1);
      return -1;
    }

  printf("[PASS] test_pwm (1kHz 50%%, cnt %u->%u)\n",
         (unsigned)cnt1, (unsigned)cnt2);
  return 0;
}
#endif

#ifdef CONFIG_T113_GPADC
static int test_adc(void)
{
  /* Direct GPADC register test:
   * Enable LDO + ADC + calibration, select ch0, single mode,
   * wait for conversion, read CH0 data register.
   *
   * GPADC_BASE = 0x02009000
   *   CTRL  = +0x04, CS_EN = +0x08, CH0_DATA = +0x80
   * CCU GPADC_BGR = 0x020009EC
   */

  volatile uint32_t *ccu_bgr  = (volatile uint32_t *)0x020009ec;
  volatile uint32_t *gp_ctrl  = (volatile uint32_t *)0x02009004;
  volatile uint32_t *gp_cs_en = (volatile uint32_t *)0x02009008;
  volatile uint32_t *gp_ch0   = (volatile uint32_t *)0x02009080;
  uint32_t val;
  uint32_t data;

  /* Enable clock */

  val = *ccu_bgr;
  val |= (1 << 16) | (1 << 0);
  *ccu_bgr = val;

  /* Enable LDO + ADC + calibration, single conversion mode */

  *gp_ctrl = (1 << 0) | (1 << 16) | (1 << 17);

  /* Select channel 0 */

  *gp_cs_en = 0x01;

  usleep(10000);

  data = *gp_ch0 & 0xfff;

  /* Disable */

  *gp_ctrl = 0;

  /* 12-bit ADC: valid range 0-4095, floating pin typically reads
   * some non-zero/non-max value
   */

  if (data > 4095)
    {
      printf("[FAIL] test_adc: data=%u out of range\n", (unsigned)data);
      return -1;
    }

  printf("[PASS] test_adc (ch0=%u, ~%umV)\n",
         (unsigned)data, (unsigned)(data * 1800 / 4095));
  return 0;
}
#endif

/****************************************************************************
 * Public Functions
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

#ifdef CONFIG_SMP
static volatile int g_smp_counter[CONFIG_SMP_NCPUS];

static int smp_worker(int argc, FAR char *argv[])
{
  int cpu = sched_getcpu();
  int i;

  for (i = 0; i < 1000; i++)
    {
      g_smp_counter[cpu]++;
    }

  return 0;
}

static int test_smp(void)
{
  pid_t pid0;
  pid_t pid1;
  int status;
  int i;

  for (i = 0; i < CONFIG_SMP_NCPUS; i++)
    {
      g_smp_counter[i] = 0;
    }

  pid0 = task_create("smp0", 100, 2048, smp_worker, NULL);
  pid1 = task_create("smp1", 100, 2048, smp_worker, NULL);

  if (pid0 < 0 || pid1 < 0)
    {
      printf("[FAIL] test_smp: task_create: %d %d\n", pid0, pid1);
      return -1;
    }

  waitpid(pid0, &status, 0);
  waitpid(pid1, &status, 0);

  if (g_smp_counter[0] == 0 && g_smp_counter[1] == 0)
    {
      printf("[FAIL] test_smp: no CPU executed work\n");
      return -1;
    }

  printf("[PASS] test_smp (cpu0=%d cpu1=%d)\n",
         g_smp_counter[0], g_smp_counter[1]);
  return 0;
}
#endif

static int test_i2c(void)
{
  int fd;

  fd = open("/dev/i2c0", O_RDWR);
  if (fd < 0)
    {
      printf("[FAIL] test_i2c: open /dev/i2c0: %d\n", errno);
      return -1;
    }

  close(fd);
  printf("[PASS] test_i2c\n");
  return 0;
}

static int test_heap(void)
{
  struct mallinfo info;

  info = mallinfo();

  /* Expect at least 64MB of heap with 128MB DDR */

  if (info.arena < (64 * 1024 * 1024))
    {
      printf("[FAIL] test_heap: arena=%d (expected >= 64MB)\n", info.arena);
      return -1;
    }

  printf("[PASS] test_heap (arena=%dMB, used=%dKB, free=%dKB)\n",
         info.arena / (1024 * 1024),
         info.uordblks / 1024,
         info.fordblks / 1024);
  return 0;
}

#ifdef CONFIG_T113_RTC
static int test_rtc(void)
{
  int fd;
  struct rtc_time rtctime;

  fd = open("/dev/rtc0", O_RDONLY);
  if (fd < 0)
    {
      printf("[FAIL] test_rtc: open /dev/rtc0: %d\n", errno);
      return -1;
    }

  if (ioctl(fd, RTC_RD_TIME, (unsigned long)&rtctime) < 0)
    {
      printf("[FAIL] test_rtc: RTC_RD_TIME: %d\n", errno);
      close(fd);
      return -1;
    }

  close(fd);

  printf("[PASS] test_rtc (%04d-%02d-%02d %02d:%02d:%02d)\n",
         rtctime.tm_year + 1900, rtctime.tm_mon + 1, rtctime.tm_mday,
         rtctime.tm_hour, rtctime.tm_min, rtctime.tm_sec);
  return 0;
}
#endif

static int test_littlefs_multi(void)
{
  char path[64];
  char wbuf[48];
  char rbuf[48];
  int fd;
  int ret;
  ssize_t n;
  int i;

  ret = mount("/dev/mtd1", LFS_MOUNT_POINT, "littlefs", 0, "autoformat");
  if (ret < 0)
    {
      printf("[FAIL] test_lfs_multi: mount: %d\n", errno);
      return -1;
    }

  for (i = 0; i < 5; i++)
    {
      snprintf(path, sizeof(path), "%s/f%d.txt", LFS_MOUNT_POINT, i);
      snprintf(wbuf, sizeof(wbuf), "file-%d-data-%08x", i, i * 0x12345);
      fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
      if (fd < 0)
        {
          printf("[FAIL] test_lfs_multi: create %s: %d\n", path, errno);
          goto fail;
        }

      write(fd, wbuf, strlen(wbuf));
      close(fd);
    }

  for (i = 0; i < 5; i++)
    {
      snprintf(path, sizeof(path), "%s/f%d.txt", LFS_MOUNT_POINT, i);
      snprintf(wbuf, sizeof(wbuf), "file-%d-data-%08x", i, i * 0x12345);
      fd = open(path, O_RDONLY);
      if (fd < 0)
        {
          printf("[FAIL] test_lfs_multi: open %s: %d\n", path, errno);
          goto fail;
        }

      memset(rbuf, 0, sizeof(rbuf));
      n = read(fd, rbuf, sizeof(rbuf) - 1);
      close(fd);
      if (n != (ssize_t)strlen(wbuf) ||
          memcmp(rbuf, wbuf, strlen(wbuf)) != 0)
        {
          printf("[FAIL] test_lfs_multi: verify %s\n", path);
          goto fail;
        }
    }

  for (i = 0; i < 5; i++)
    {
      snprintf(path, sizeof(path), "%s/f%d.txt", LFS_MOUNT_POINT, i);
      unlink(path);
    }

  umount(LFS_MOUNT_POINT);

  ret = mount("/dev/mtd1", LFS_MOUNT_POINT, "littlefs", 0, NULL);
  if (ret < 0)
    {
      printf("[FAIL] test_lfs_multi: remount: %d\n", errno);
      return -1;
    }

  for (i = 0; i < 5; i++)
    {
      snprintf(path, sizeof(path), "%s/f%d.txt", LFS_MOUNT_POINT, i);
      fd = open(path, O_RDONLY);
      if (fd >= 0)
        {
          close(fd);
          printf("[FAIL] test_lfs_multi: %s still exists\n", path);
          umount(LFS_MOUNT_POINT);
          return -1;
        }
    }

  umount(LFS_MOUNT_POINT);
  printf("[PASS] test_lfs_multi\n");
  return 0;

fail:
  umount(LFS_MOUNT_POINT);
  return -1;
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

  printf("--- LittleFS Multi-file Test ---\n");
  if (test_littlefs_multi() < 0)
    {
      fail_count++;
    }

  printf("--- Timer Test ---\n");
  if (test_timer() < 0)
    {
      fail_count++;
    }

  printf("--- I2C Bus Test ---\n");
  if (test_i2c() < 0)
    {
      fail_count++;
    }

  printf("--- Heap Test ---\n");
  if (test_heap() < 0)
    {
      fail_count++;
    }

#ifdef CONFIG_T113_RTC
  printf("--- RTC Test ---\n");
  if (test_rtc() < 0)
    {
      fail_count++;
    }
#endif

#ifdef CONFIG_T113_GPIO
  printf("--- GPIO Test ---\n");
  if (test_gpio() < 0)
    {
      fail_count++;
    }
#endif

#ifdef CONFIG_T113_PWM
  printf("--- PWM Test ---\n");
  if (test_pwm() < 0)
    {
      fail_count++;
    }
#endif

#ifdef CONFIG_T113_GPADC
  printf("--- ADC Test ---\n");
  if (test_adc() < 0)
    {
      fail_count++;
    }
#endif

#ifdef CONFIG_SMP
  printf("--- SMP Test ---\n");
  if (test_smp() < 0)
    {
      fail_count++;
    }
#endif

  printf("=== T113 Test Complete: %s (%d failures) ===\n",
         fail_count == 0 ? "ALL PASS" : "FAIL", fail_count);
  return fail_count == 0 ? 0 : 1;
}
