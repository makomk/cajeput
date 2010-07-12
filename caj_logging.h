/* Copyright (c) 2010 Aidan Thornton, all rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *    * Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *    * Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY AIDAN THORNTON ''AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL AIDAN THORNTON BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF 
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef CAJ_LOGGING_H
#define CAJ_LOGGING_H

#ifdef __cplusplus
extern "C" {
#endif

#include <glib.h>

struct caj_logger;

struct caj_logger *caj_new_logger(void);
void caj_log(struct caj_logger *log, int src, int level, const char *format, ...) 
  G_GNUC_PRINTF(4, 5);

#define CAJ_LOG_LEVEL_DEBUG (1<<0)
#define CAJ_LOG_LEVEL_INFO (1<<1)
#define CAJ_LOG_LEVEL_NORMAL (1<<2)
#define CAJ_LOG_LEVEL_WARNING (1<<3)
#define CAJ_LOG_LEVEL_ERROR (1<<4)
#define CAJ_LOG_LEVEL_CRITICAL (1<<5)

#define CAJ_DEBUG_LS(logger, src, format, ...) caj_log(logger, src, \
						       CAJ_LOG_LEVEL_DEBUG, \
						       format, ##__VA_ARGS__)
#define CAJ_INFO_LS(logger, src, format, ...) caj_log(logger, src, \
						       CAJ_LOG_LEVEL_INFO, \
						       format, ##__VA_ARGS__)
#define CAJ_PRINT_LS(logger, src, format, ...) caj_log(logger, src, \
						       CAJ_LOG_LEVEL_NORMAL, \
						       format, ##__VA_ARGS__)
#define CAJ_WARN_LS(logger, src, format, ...) caj_log(logger, src, \
						       CAJ_LOG_LEVEL_WARNING, \
						       format, ##__VA_ARGS__)
#define CAJ_ERROR_LS(logger, src, format, ...) caj_log(logger, src, \
						       CAJ_LOG_LEVEL_ERROR, \
						       format, ##__VA_ARGS__)

#define CAJ_DEBUG_L(logger, format, ...) CAJ_DEBUG_LS(logger, 0, format, ##__VA_ARGS__)
#define CAJ_INFO_L(logger, format, ...) CAJ_INFO_LS(logger, 0, format, ##__VA_ARGS__)
#define CAJ_PRINT_L(logger, format, ...) CAJ_PRINT_LS(logger, 0, format, ##__VA_ARGS__)
#define CAJ_WARN_L(logger, format, ...) CAJ_WARN_LS(logger, 0, format, ##__VA_ARGS__)
#define CAJ_ERROR_L(logger, format, ...) CAJ_ERROR_LS(logger, 0, format, ##__VA_ARGS__)

#define CAJ_DEBUG(format, ...) CAJ_DEBUG_LS(CAJ_LOGGER, 0, format, ##__VA_ARGS__)
#define CAJ_INFO(format, ...) CAJ_INFO_LS(CAJ_LOGGER, 0, format, ##__VA_ARGS__)
#define CAJ_PRINT(format, ...) CAJ_PRINT_LS(CAJ_LOGGER, 0, format, ##__VA_ARGS__)
#define CAJ_WARN(format, ...) CAJ_WARN_LS(CAJ_LOGGER, 0, format, ##__VA_ARGS__)
#define CAJ_ERROR(format, ...) CAJ_ERROR_LS(CAJ_LOGGER, 0, format, ##__VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif
