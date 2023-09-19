/*
 * Copyright (c) 2022 Andreas Rheinhardt <andreas.rheinhardt@outlook.com>
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#ifndef AVCODEC_THREADPROGRESS_H
#define AVCODEC_THREADPROGRESS_H

#include <stdatomic.h>

struct AVCodecContext;

typedef struct ThreadProgress {
    atomic_int progress;
    struct PerThreadContext *owner;
} ThreadProgress;

void ff_thread_progress_init(ThreadProgress *pro, struct AVCodecContext *owner);
void ff_thread_progress_report2(ThreadProgress *pro, int progress);
void ff_thread_progress_await2(const ThreadProgress *pro, int progress);

#endif /* AVCODEC_THREADPROGRESS_H */
