/****************************************************************************
 * apps/examples/hello/hello_main.c
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
#include <errno.h>

#include <nuttx/mtd/mtd.h>
#include <nuttx/fs/fs.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define MTD_TEST_NBLOCKS  2   /* Number of erase blocks to test */
#define MTD_TEST_NPAGES   4   /* Pages per erase block to verify */

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: fill_pattern
 *
 * Description:
 *   Fill buffer with a deterministic pattern based on page and offset.
 *
 ****************************************************************************/

static void fill_pattern(FAR uint8_t *buf, size_t len, int page)
{
  size_t i;

  for (i = 0; i < len; i++)
    {
      buf[i] = (uint8_t)((page ^ (i & 0xff) ^ 0xa5) & 0xff);
    }
}

/****************************************************************************
 * Name: verify_pattern
 *
 * Description:
 *   Verify buffer matches expected pattern. Returns offset of first
 *   mismatch or -1 on success.
 *
 ****************************************************************************/

static int verify_pattern(FAR const uint8_t *buf, size_t len, int page)
{
  size_t i;

  for (i = 0; i < len; i++)
    {
      uint8_t expected = (uint8_t)((page ^ (i & 0xff) ^ 0xa5) & 0xff);

      if (buf[i] != expected)
        {
          printf("[FAIL] mtd_pattern: page %d offset %zu: "
                 "got 0x%02x expected 0x%02x\n",
                 page, i, buf[i], expected);
          return (int)i;
        }
    }

  return -1;
}

/****************************************************************************
 * Name: verify_erased
 *
 * Description:
 *   Verify buffer is all 0xff (erased state for NAND/NOR).
 *   Returns offset of first non-0xff or -1 on success.
 *
 ****************************************************************************/

static int verify_erased(FAR const uint8_t *buf, size_t len)
{
  size_t i;

  for (i = 0; i < len; i++)
    {
      if (buf[i] != 0xff)
        {
          return (int)i;
        }
    }

  return -1;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: main
 *
 * Description:
 *   MTD erase/write/read test on /dev/mtd0.
 *   Uses last MTD_TEST_NBLOCKS erase blocks to avoid clobbering
 *   filesystem partitions.
 *
 ****************************************************************************/

int main(int argc, FAR char *argv[])
{
  FAR struct inode *node;
  FAR struct mtd_dev_s *mtd;
  struct mtd_geometry_s geo;
  FAR uint8_t *wbuf = NULL;
  FAR uint8_t *rbuf = NULL;
  int pages_per_erase;
  int test_erase_start;
  int test_page_start;
  int npages;
  int blk;
  int pg;
  int ret;
  int fail_count = 0;

  printf("=== T113 MTD Comprehensive Test ===\n");

  /* Open the MTD device */

  ret = find_mtddriver("/dev/mtd0", &node);
  if (ret < 0)
    {
      printf("[FAIL] mtd_open: find_mtddriver /dev/mtd0: %d\n", ret);
      return 1;
    }

  mtd = node->u.i_mtd;

  /* Get geometry */

  ret = MTD_IOCTL(mtd, MTDIOC_GEOMETRY, (unsigned long)&geo);
  if (ret < 0)
    {
      printf("[FAIL] mtd_geometry: MTDIOC_GEOMETRY failed: %d\n", ret);
      goto out;
    }

  printf("  blocksize=%lu erasesize=%lu neraseblocks=%lu\n",
         (unsigned long)geo.blocksize,
         (unsigned long)geo.erasesize,
         (unsigned long)geo.neraseblocks);

  pages_per_erase = geo.erasesize / geo.blocksize;

  /* Use last MTD_TEST_NBLOCKS erase blocks */

  if ((int)geo.neraseblocks < MTD_TEST_NBLOCKS)
    {
      printf("[FAIL] mtd_geometry: not enough erase blocks (%lu < %d)\n",
             (unsigned long)geo.neraseblocks, MTD_TEST_NBLOCKS);
      goto out;
    }

  test_erase_start = geo.neraseblocks - MTD_TEST_NBLOCKS;
  test_page_start = test_erase_start * pages_per_erase;
  npages = MTD_TEST_NBLOCKS * pages_per_erase;

  printf("  testing erase blocks %d..%d (pages %d..%d)\n",
         test_erase_start,
         (int)geo.neraseblocks - 1,
         test_page_start,
         test_page_start + npages - 1);

  /* Allocate buffers */

  wbuf = malloc(geo.blocksize);
  rbuf = malloc(geo.blocksize);
  if (wbuf == NULL || rbuf == NULL)
    {
      printf("[FAIL] mtd_alloc: malloc %lu bytes failed\n",
             (unsigned long)geo.blocksize);
      fail_count++;
      goto out;
    }

  /* Test 1: Erase and verify erased state */

  printf("--- Test 1: Erase and verify ---\n");

  for (blk = 0; blk < MTD_TEST_NBLOCKS; blk++)
    {
      int erase_blk = test_erase_start + blk;

      ret = MTD_ERASE(mtd, erase_blk, 1);
      if (ret < 0)
        {
          printf("[FAIL] mtd_erase: block %d: %d\n", erase_blk, ret);
          fail_count++;
          continue;
        }

      /* Read back first page and verify erased */

      memset(rbuf, 0, geo.blocksize);
      ret = MTD_BREAD(mtd, erase_blk * pages_per_erase, 1, rbuf);
      if (ret < 0)
        {
          printf("[FAIL] mtd_erase_read: block %d: %d\n", erase_blk, ret);
          fail_count++;
          continue;
        }

      {
        int bad = verify_erased(rbuf, geo.blocksize);
        if (bad >= 0)
          {
            printf("[FAIL] mtd_erase_verify: block %d byte %d = 0x%02x\n",
                   erase_blk, bad, rbuf[bad]);
            fail_count++;
          }
      }
    }

  if (fail_count == 0)
    {
      printf("[PASS] mtd_erase\n");
    }

  /* Test 2: Write pattern to multiple pages, read back and verify */

  printf("--- Test 2: Write/read pattern across %d pages ---\n", npages);

  for (pg = 0; pg < npages; pg++)
    {
      int abs_page = test_page_start + pg;

      fill_pattern(wbuf, geo.blocksize, abs_page);
      ret = MTD_BWRITE(mtd, abs_page, 1, wbuf);
      if (ret < 0)
        {
          printf("[FAIL] mtd_write: page %d: %d\n", abs_page, ret);
          fail_count++;
        }
    }

  for (pg = 0; pg < npages; pg++)
    {
      int abs_page = test_page_start + pg;

      memset(rbuf, 0, geo.blocksize);
      ret = MTD_BREAD(mtd, abs_page, 1, rbuf);
      if (ret < 0)
        {
          printf("[FAIL] mtd_read: page %d: %d\n", abs_page, ret);
          fail_count++;
          continue;
        }

      if (verify_pattern(rbuf, geo.blocksize, abs_page) >= 0)
        {
          fail_count++;
        }
    }

  if (fail_count == 0)
    {
      printf("[PASS] mtd_write_read\n");
    }

  /* Test 3: Block boundary — write last page of block N, first page
   * of block N+1, verify both survive.
   */

  printf("--- Test 3: Block boundary write ---\n");

  if (MTD_TEST_NBLOCKS >= 2)
    {
      int boundary_pages[2];
      int i;

      /* Last page of first test block */

      boundary_pages[0] = test_page_start + pages_per_erase - 1;

      /* First page of second test block */

      boundary_pages[1] = test_page_start + pages_per_erase;

      /* Re-erase both blocks first */

      MTD_ERASE(mtd, test_erase_start, 1);
      MTD_ERASE(mtd, test_erase_start + 1, 1);

      for (i = 0; i < 2; i++)
        {
          fill_pattern(wbuf, geo.blocksize, boundary_pages[i] + 0x100);
          ret = MTD_BWRITE(mtd, boundary_pages[i], 1, wbuf);
          if (ret < 0)
            {
              printf("[FAIL] mtd_boundary_write: page %d: %d\n",
                     boundary_pages[i], ret);
              fail_count++;
            }
        }

      for (i = 0; i < 2; i++)
        {
          memset(rbuf, 0, geo.blocksize);
          ret = MTD_BREAD(mtd, boundary_pages[i], 1, rbuf);
          if (ret < 0)
            {
              printf("[FAIL] mtd_boundary_read: page %d: %d\n",
                     boundary_pages[i], ret);
              fail_count++;
              continue;
            }

          if (verify_pattern(rbuf, geo.blocksize,
                             boundary_pages[i] + 0x100) >= 0)
            {
              fail_count++;
            }
        }

      if (fail_count == 0)
        {
          printf("[PASS] mtd_boundary\n");
        }
    }

  /* Summary */

  printf("=== MTD Test Complete: %s (%d failures) ===\n",
         fail_count == 0 ? "ALL PASS" : "FAIL", fail_count);

out:
  free(wbuf);
  free(rbuf);
  inode_release(node);
  return fail_count == 0 ? 0 : 1;
}
